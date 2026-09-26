#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="${SCRIPT_DIR}"

SDK_DIR="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-$HOME/Android/Sdk}}"
BUILD_TOOLS="${SDK_DIR}/build-tools/37.0.0"
PLATFORM_JAR="${SDK_DIR}/platforms/android-36/android.jar"

if [[ ! -d "${BUILD_TOOLS}" ]]; then
    echo "[-] build-tools 37.0.0 nao encontrado em: ${BUILD_TOOLS}"
    exit 1
fi

if [[ ! -f "${PLATFORM_JAR}" ]]; then
    echo "[-] android.jar (platform 36) nao encontrado em: ${PLATFORM_JAR}"
    exit 1
fi

BUILD_DIR="${MANAGER_BUILD_DIR:-${ROOT_DIR}/build}"
GEN_DIR="${MANAGER_GEN_DIR:-${ROOT_DIR}/gen}"
OUTPUT_APK="${MANAGER_OUTPUT_APK:-${ROOT_DIR}/bepinex-manager.apk}"
KEYSTORE="${MANAGER_KEYSTORE:-${ROOT_DIR}/.debug.keystore}"
SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH:-$(git -C "${ROOT_DIR}/.." log -1 --format=%ct)}"
NDK_BUILD="${NDK_BUILD:-${ANDROID_NDK_HOME:-$HOME/Android/Sdk/ndk/23.2.8568313}/ndk-build}"
U_DUMP_DIR="${ROOT_DIR}/../mods/u_dump"

# Versao: vem do VERSION da RAIZ do repo, nunca de numero solto aqui. O
# manifest tambem nao fixa nada — quem passa para o aapt2 e este script.
VERSION_FILE="${ROOT_DIR}/../VERSION"
read -r VERSION_CODE VERSION_NAME < <(bash "${ROOT_DIR}/version.sh" "${VERSION_FILE}")
echo "[*] Versao (VERSION da raiz): ${VERSION_NAME} (versionCode ${VERSION_CODE})"

echo "[*] Limpando diretorios de build..."
rm -rf "${BUILD_DIR}" "${GEN_DIR}"
mkdir -p "${BUILD_DIR}/compiled_res" "${BUILD_DIR}/classes" "${BUILD_DIR}/dex" "${GEN_DIR}"

echo "[*] Compilando u_dump.so para os assets do APK..."
if [[ ! -x "${NDK_BUILD}" ]]; then
    echo "[-] ndk-build nao encontrado em: ${NDK_BUILD}"
    exit 1
fi
(
    cd "${U_DUMP_DIR}"
    "${NDK_BUILD}" APP_CFLAGS+="-Wall -Wextra" APP_CPPFLAGS+="-Wall -Wextra" -B -j4
) > "${BUILD_DIR}/u_dump-build.log" 2>&1
if grep -E 'warning:|error:' "${BUILD_DIR}/u_dump-build.log" >/dev/null; then
    cat "${BUILD_DIR}/u_dump-build.log" >&2
    echo "[-] warning/error ao compilar u_dump.so"
    exit 1
fi
U_DUMP_SO="${U_DUMP_DIR}/libs/arm64-v8a/libu_dump.so"
if [[ ! -f "${U_DUMP_SO}" ]]; then
    echo "[-] u_dump.so nao foi gerado em: ${U_DUMP_SO}"
    exit 1
fi
ASSET_DIR="${BUILD_DIR}/assets"
mkdir -p "${ASSET_DIR}"
cp "${U_DUMP_SO}" "${ASSET_DIR}/u_dump.so"

# Garante keystore debug se nao existir
if [[ "${MANAGER_UNSIGNED:-0}" != 1 && ! -f "${KEYSTORE}" ]]; then
    echo "[*] Gerando keystore debug..."
    keytool -genkeypair -v -keystore "${KEYSTORE}" \
        -storepass android -alias androiddebugkey -keypass android \
        -keyalg RSA -keysize 2048 -validity 10000 \
        -dname "CN=Android Debug,O=Android,C=US" >/dev/null 2>&1
fi

echo "[*] 1/6. Compilando recursos com aapt2 compile..."
"${BUILD_TOOLS}/aapt2" compile --dir "${ROOT_DIR}/res" -o "${BUILD_DIR}/compiled_res.zip"

echo "[*] 2/6. Vinculando pacote com aapt2 link..."
ASSET_ARGS=(-A "${ASSET_DIR}")
"${BUILD_TOOLS}/aapt2" link \
    -I "${PLATFORM_JAR}" \
    --manifest "${ROOT_DIR}/AndroidManifest.xml" \
    --version-code "${VERSION_CODE}" \
    --version-name "${VERSION_NAME}" \
    "${ASSET_ARGS[@]}" \
    --java "${GEN_DIR}" \
    -o "${BUILD_DIR}/unaligned.apk" \
    "${BUILD_DIR}/compiled_res.zip" \
    --auto-add-overlay

echo "[*] 3/6. Compilando codigo Java com javac --release 17..."
# Constante de versao para o codigo (StatusChecker mostra na tela de status).
mkdir -p "${GEN_DIR}/io/github/rianprei/bepinex/manager/core"
cat > "${GEN_DIR}/io/github/rianprei/bepinex/manager/core/BuildVersion.java" <<JAVA_EOF
package io.github.rianprei.bepinex.manager.core;

// Gerado por manager/build.sh a partir do VERSION da raiz do repo. Nao editar.
public final class BuildVersion {
    public static final String NAME = "${VERSION_NAME}";
    public static final int CODE = ${VERSION_CODE};
    private BuildVersion() {}
}
JAVA_EOF
find "${ROOT_DIR}/src" "${GEN_DIR}" -name "*.java" > "${BUILD_DIR}/sources.txt"
javac --release 17 -cp "${PLATFORM_JAR}" -d "${BUILD_DIR}/classes" @"${BUILD_DIR}/sources.txt"

echo "[*] 4/6. Gerando DEX com d8 (min-api 26)..."
find "${BUILD_DIR}/classes" -name "*.class" > "${BUILD_DIR}/class_files.txt"
"${BUILD_TOOLS}/d8" --release --min-api 26 --output "${BUILD_DIR}/dex" @"${BUILD_DIR}/class_files.txt"

echo "[*] 5/6. Empacotando classes.dex no APK..."
jar -uf "${BUILD_DIR}/unaligned.apk" -C "${BUILD_DIR}/dex" classes.dex

echo "[*] Normalizando timestamps do APK..."
python3 - "${BUILD_DIR}/unaligned.apk" "${BUILD_DIR}/normalized.apk" "${SOURCE_DATE_EPOCH}" <<'PY'
import sys
import time
import zipfile

source, target, epoch = sys.argv[1], sys.argv[2], int(sys.argv[3])
date_time = time.gmtime(epoch)[0:6]
date_time = (max(1980, date_time[0]),) + date_time[1:]
with zipfile.ZipFile(source, "r") as zin, zipfile.ZipFile(
    target, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9
) as zout:
    for old in zin.infolist():
        info = zipfile.ZipInfo(old.filename, date_time=date_time)
        info.compress_type = zipfile.ZIP_DEFLATED
        info.create_system = old.create_system
        info.external_attr = old.external_attr
        info.flag_bits = old.flag_bits & 0x800
        zout.writestr(info, zin.read(old.filename))
PY
mv "${BUILD_DIR}/normalized.apk" "${BUILD_DIR}/unaligned.apk"

echo "[*] 6/6. Alinhando e assinando APK final..."
"${BUILD_TOOLS}/zipalign" -p -f 4 "${BUILD_DIR}/unaligned.apk" "${BUILD_DIR}/aligned.apk"
if [[ "${MANAGER_UNSIGNED:-0}" == 1 ]]; then
    cp "${BUILD_DIR}/aligned.apk" "${OUTPUT_APK}"
    echo "[!] APK UNSIGNED-DEBUG: MANAGER_KEYSTORE não foi fornecida"
else
    [[ -f "${KEYSTORE}" ]] || {
        echo "ERRO: keystore ausente: ${KEYSTORE}; forneça MANAGER_KEYSTORE." >&2
        exit 1
    }
    "${BUILD_TOOLS}/apksigner" sign \
        --ks "${KEYSTORE}" \
        --ks-pass pass:android \
        --key-pass pass:android \
        --out "${OUTPUT_APK}" \
        "${BUILD_DIR}/aligned.apk"
fi

echo "[+] Build concluido com sucesso: ${OUTPUT_APK}"
echo "--- Informacoes do APK gerado ---"
"${BUILD_TOOLS}/aapt2" dump badging "${OUTPUT_APK}" | grep -E "package|minSdkVersion|targetSdkVersion|application-label"
# A versao do APK tem que ser a do VERSION da raiz: se divergir, o build falhou.
"${BUILD_TOOLS}/aapt2" dump badging "${OUTPUT_APK}" | grep -q "versionCode='${VERSION_CODE}' versionName='${VERSION_NAME}'" \
    || { echo "ERRO: o APK saiu com outra versao (esperado ${VERSION_NAME}/${VERSION_CODE})." >&2; exit 1; }
echo "[-] APK confere com o VERSION da raiz: ${VERSION_NAME} (${VERSION_CODE})"
