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

BUILD_DIR="${ROOT_DIR}/build"
GEN_DIR="${ROOT_DIR}/gen"
OUTPUT_APK="${ROOT_DIR}/bepinex-manager.apk"
KEYSTORE="${ROOT_DIR}/.debug.keystore"

# Versao: vem do VERSION da RAIZ do repo, nunca de numero solto aqui. O
# manifest tambem nao fixa nada — quem passa para o aapt2 e este script.
VERSION_FILE="${ROOT_DIR}/../VERSION"
read -r VERSION_CODE VERSION_NAME < <(bash "${ROOT_DIR}/version.sh" "${VERSION_FILE}")
echo "[*] Versao (VERSION da raiz): ${VERSION_NAME} (versionCode ${VERSION_CODE})"

echo "[*] Limpando diretorios de build..."
rm -rf "${BUILD_DIR}" "${GEN_DIR}"
mkdir -p "${BUILD_DIR}/compiled_res" "${BUILD_DIR}/classes" "${BUILD_DIR}/dex" "${GEN_DIR}"

# Garante keystore debug se nao existir
if [[ ! -f "${KEYSTORE}" ]]; then
    echo "[*] Gerando keystore debug..."
    keytool -genkeypair -v -keystore "${KEYSTORE}" \
        -storepass android -alias androiddebugkey -keypass android \
        -keyalg RSA -keysize 2048 -validity 10000 \
        -dname "CN=Android Debug,O=Android,C=US" >/dev/null 2>&1
fi

echo "[*] 1/6. Compilando recursos com aapt2 compile..."
"${BUILD_TOOLS}/aapt2" compile --dir "${ROOT_DIR}/res" -o "${BUILD_DIR}/compiled_res.zip"

echo "[*] 2/6. Vinculando pacote com aapt2 link..."
"${BUILD_TOOLS}/aapt2" link \
    -I "${PLATFORM_JAR}" \
    --manifest "${ROOT_DIR}/AndroidManifest.xml" \
    --version-code "${VERSION_CODE}" \
    --version-name "${VERSION_NAME}" \
    -A "${ROOT_DIR}/assets" \
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

echo "[*] 6/6. Alinhando e assinando APK final..."
"${BUILD_TOOLS}/zipalign" -p -f 4 "${BUILD_DIR}/unaligned.apk" "${BUILD_DIR}/aligned.apk"
"${BUILD_TOOLS}/apksigner" sign \
    --ks "${KEYSTORE}" \
    --ks-pass pass:android \
    --key-pass pass:android \
    --out "${OUTPUT_APK}" \
    "${BUILD_DIR}/aligned.apk"

echo "[+] Build concluido com sucesso: ${OUTPUT_APK}"
echo "--- Informacoes do APK gerado ---"
"${BUILD_TOOLS}/aapt2" dump badging "${OUTPUT_APK}" | grep -E "package|minSdkVersion|targetSdkVersion|application-label"
# A versao do APK tem que ser a do VERSION da raiz: se divergir, o build falhou.
"${BUILD_TOOLS}/aapt2" dump badging "${OUTPUT_APK}" | grep -q "versionCode='${VERSION_CODE}' versionName='${VERSION_NAME}'" \
    || { echo "ERRO: o APK saiu com outra versao (esperado ${VERSION_NAME}/${VERSION_CODE})." >&2; exit 1; }
echo "[-] APK confere com o VERSION da raiz: ${VERSION_NAME} (${VERSION_CODE})"
