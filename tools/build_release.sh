#!/usr/bin/env bash
# Gera a árvore local de release reproduzível. Não publica, assina nem faz push.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

ALLOW_DIRTY=0
OUTPUT="${RELEASE_OUT:-$ROOT/out/release}"
while [ "$#" -gt 0 ]; do
    case "$1" in
        --dirty) ALLOW_DIRTY=1 ;;
        --output) shift; [ "$#" -gt 0 ] || { echo "uso: --output DIR" >&2; exit 2; }; OUTPUT=$1 ;;
        *) echo "uso: $0 [--dirty] [--output DIR]" >&2; exit 2 ;;
    esac
    shift
done

if [ "$ALLOW_DIRTY" -ne 1 ] && [ -n "$(git status --porcelain --untracked-files=all)" ]; then
    echo "ERRO: árvore git suja; use --dirty para uma build marcada DIRTY." >&2
    exit 1
fi

version_line="$(cat VERSION)"
read -r VERSION VERSION_CODE <<< "$version_line"
EPOCH="${SOURCE_DATE_EPOCH:-$(git log -1 --format=%ct)}"
export SOURCE_DATE_EPOCH="$EPOCH"
RELEASE_DIR="$OUTPUT/$VERSION"
WORK="$RELEASE_DIR/.work"
rm -rf "$RELEASE_DIR"
mkdir -p "$RELEASE_DIR" "$WORK" "$RELEASE_DIR/mods"

NDK="${NDK:-$HOME/Android/Sdk/ndk/23.2.8568313}"
NDK_BUILD="${NDK_BUILD:-$NDK/ndk-build}"
SDK_DIR="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-$HOME/Android/Sdk}}"
BUILD_TOOLS="${BUILD_TOOLS:-$SDK_DIR/build-tools/37.0.0}"
[ -x "$NDK_BUILD" ] || { echo "ERRO: ndk-build ausente: $NDK_BUILD" >&2; exit 1; }
[ -x "$BUILD_TOOLS/aapt2" ] || { echo "ERRO: build-tools ausente: $BUILD_TOOLS" >&2; exit 1; }
[ -x "$BUILD_TOOLS/d8" ] || { echo "ERRO: d8 ausente: $BUILD_TOOLS" >&2; exit 1; }
[ -x "$BUILD_TOOLS/apksigner" ] || { echo "ERRO: apksigner ausente: $BUILD_TOOLS" >&2; exit 1; }
[ -x "$BUILD_TOOLS/zipalign" ] || { echo "ERRO: zipalign ausente: $BUILD_TOOLS" >&2; exit 1; }
command -v python3 >/dev/null || { echo "ERRO: python3 ausente" >&2; exit 1; }
command -v javac >/dev/null || { echo "ERRO: javac ausente" >&2; exit 1; }
command -v java >/dev/null || { echo "ERRO: java ausente" >&2; exit 1; }

# Chave de assinatura, em ordem de precedência:
#   1) MANAGER_KEYSTORE (o que o CI ou o usuário passa explicitamente)
#   2) ~/.config/bepinex-termux/manager-release.jks — o padrão do DESKTOP,
#      que é onde a chave de release do usuário mora. Existe? Usa. Não
#      existe? Cai no unsigned-debug de sempre (build de CI, sandbox).
# A senha nunca é lida por este script: ela vem do ambiente (MANAGER_KS_PASS)
# ou é pedida no terminal pelo apksigner. NUNCA pass:<literal> na linha de
# comando, e este script nunca copia/abre/imprime o .jks.
RELEASE_KEY_DEFAULT="$HOME/.config/bepinex-termux/manager-release.jks"
if [ -n "${MANAGER_KEYSTORE:-}" ]; then
    [ -f "$MANAGER_KEYSTORE" ] || { echo "ERRO: MANAGER_KEYSTORE ausente: $MANAGER_KEYSTORE" >&2; exit 1; }
    MANAGER_UNSIGNED=0
    SIGNING_STATUS="SIGNED_EXTERNAL_KEY"
elif [ -f "$RELEASE_KEY_DEFAULT" ]; then
    MANAGER_KEYSTORE="$RELEASE_KEY_DEFAULT"
    export MANAGER_KEYSTORE
    MANAGER_UNSIGNED=0
    SIGNING_STATUS="release-key"
    echo "[*] chave de release em $RELEASE_KEY_DEFAULT (senha: env MANAGER_KS_PASS ou prompt)"
else
    MANAGER_KEYSTORE=""
    MANAGER_UNSIGNED=1
    SIGNING_STATUS="UNSIGNED-DEBUG"
fi

OUT_DIR="$WORK/module" SYMBOLS_DIR="$RELEASE_DIR/symbols" \
    "$ROOT/tools/build_module.sh" >"$WORK/build_module.log"
cp "$WORK/module/bepinex-termux-$VERSION.zip" "$RELEASE_DIR/bepinex-termux-$VERSION.zip"

MANAGER_OUTPUT_APK="$WORK/manager/bepinex-manager-$VERSION.apk" \
MANAGER_BUILD_DIR="$WORK/manager/build" \
MANAGER_GEN_DIR="$WORK/manager/gen" \
MANAGER_UNSIGNED="$MANAGER_UNSIGNED" \
MANAGER_KEYSTORE="${MANAGER_KEYSTORE:-}" \
SOURCE_DATE_EPOCH="$SOURCE_DATE_EPOCH" \
bash "$ROOT/manager/build.sh" >"$WORK/manager.log"
cp "$WORK/manager/bepinex-manager-$VERSION.apk" "$RELEASE_DIR/bepinex-manager-$VERSION.apk"

# Fingerprint SHA-256 do CERTIFICADO que assinou o APK. Sai do APK já
# assinado (apksigner verify --print-certs) e NUNCA do .jks: abrir a chave
# para extraircertificado exigiria a senha e não há motivo para isso — o
# certificado é público e está dentro do próprio APK.
CERT_SHA256=""
if [ "$MANAGER_UNSIGNED" != 1 ] && [ -x "$BUILD_TOOLS/apksigner" ]; then
    CERT_SHA256="$("$BUILD_TOOLS/apksigner" verify --print-certs \
        "$RELEASE_DIR/bepinex-manager-$VERSION.apk" 2>/dev/null |
        awk -F': *' '/certificate SHA-256 digest/ {print $NF; exit}')" || CERT_SHA256=""
fi
if [ "$MANAGER_UNSIGNED" != 1 ] && [ -z "$CERT_SHA256" ]; then
    echo "ERRO: APK assinado mas o fingerprint do certificado nao veio (apksigner verify falhou?)" >&2
    exit 1
fi

# These are the native examples explicitly described as working/distributed:
# sa2ammo + sa2content in README, and mechabun as the SDK reference mod.
#
# Cada mod segue a mesma divisão do loader: o .so NÃO-stripado vai para
# symbols/<build-id>/ (indexado, para o tools/symbolize.sh transformar um
# tombstone do device em função:linha), e o que vai para release/mods/ é o
# STRIPPED. O binário que o usuário copia para o celular é o mesmo de sempre.
# shellcheck source=tools/symbols.sh
# shellcheck disable=SC1091
. "$ROOT/tools/symbols.sh"
STRIP_BIN="$(symbols_strip_bin)"
[ -n "$STRIP_BIN" ] || { echo "ERRO: llvm-strip ausente: instale o NDK" >&2; exit 1; }
for mod in sa2ammo sa2content mechabun; do
    (
        cd "$ROOT/mods/$mod"
        "$NDK_BUILD" APP_CFLAGS+="-Wall -Wextra" APP_CPPFLAGS+="-Wall -Wextra" -B -j4
    ) >"$WORK/ndk-$mod.log" 2>&1
    so="$ROOT/mods/$mod/libs/arm64-v8a/lib$mod.so"
    [ -f "$so" ] || { echo "ERRO: artefato ausente: $so" >&2; exit 1; }
    symbols_add "$so" "$RELEASE_DIR/symbols" "lib$mod" >/dev/null
    "$STRIP_BIN" --strip-unneeded -o "$RELEASE_DIR/mods/$mod.so" "$so"
done

while IFS= read -r bmod; do
    cp "$bmod" "$RELEASE_DIR/mods/$(basename "$bmod")"
done < <(find "$ROOT/mods" -type f -name '*.bmod' -print | sort)

DIRTY_STATUS=CLEAN
[ "$ALLOW_DIRTY" -eq 1 ] && DIRTY_STATUS=DIRTY
{
    printf 'commit=%s\n' "$(git rev-parse HEAD)"
    printf 'VERSION=%s\nversionCode=%s\n' "$VERSION" "$VERSION_CODE"
    printf 'SOURCE_DATE_EPOCH=%s\nstatus=%s\n' "$SOURCE_DATE_EPOCH" "$DIRTY_STATUS"
    printf 'manager_signing=%s\n' "$SIGNING_STATUS"
    [ -n "$CERT_SHA256" ] && printf 'manager_signing_cert_sha256=%s\n' "$CERT_SHA256"
    printf 'ndk=%s\n' "$(awk -F= '$1 ~ /^Pkg.Revision/ {gsub(/[[:space:]]/, "", $2); print $2}' "$NDK/source.properties")"
    printf 'build_tools=%s\n' "$(awk -F= '$1 ~ /^Pkg.Revision/ {gsub(/[[:space:]]/, "", $2); print $2}' "$BUILD_TOOLS/source.properties")"
    printf 'javac=%s\n' "$(javac -version 2>&1)"
    printf 'java=%s\n' "$(java -version 2>&1 | head -n 1)"
    printf 'deps.lock.sha256=%s\n' "$(sha256sum tools/deps.lock | awk '{print $1}')"
    awk -F'|' 'BEGIN {OFS="="} !/^#/ && NF >= 4 {print "dep." $1 ".version", $2; print "dep." $1 ".sha256", $3}' tools/deps.lock
} >"$RELEASE_DIR/BUILD-INFO.txt"

# O INDEX dos símbolos vai ordenado e sem duplicata: é o que o symbolize.sh
# consulta, e duas linhas iguais fariam a busca por nome devolver lixo.
sort -u "$RELEASE_DIR/symbols/INDEX" -o "$RELEASE_DIR/symbols/INDEX"
# Um nome com DOIS build-ids é o build-id voltando a depender do diretório (ou
# duas versões do mesmo binário na mesma release). Fala alto, porque aí a busca
# por nome de um tombstone sem BuildId fica ambígua.
ambiguous=$(awk -F'\t' '{c[$2]++} END {for (n in c) if (c[n] > 1) print n}' "$RELEASE_DIR/symbols/INDEX")
if [ -n "$ambiguous" ]; then
    echo "ERRO: mesmos nomes com build-ids diferentes em symbols/INDEX:" >&2
    while IFS= read -r nome; do printf '  %s\n' "$nome" >&2; done <<<"$ambiguous"
    echo "  (o build-id não está mais reproduzível — veja jni/repro.mk)" >&2
    exit 1
fi
{
    printf 'symbols_index=1\n'
    awk -F'\t' '{printf "symbol.%s.build_id=%s\n", $2, $1}' "$RELEASE_DIR/symbols/INDEX"
} >"$RELEASE_DIR/BUILD-INFO.txt.symbols"
cat "$RELEASE_DIR/BUILD-INFO.txt" "$RELEASE_DIR/BUILD-INFO.txt.symbols" >"$RELEASE_DIR/BUILD-INFO.tmp"
mv "$RELEASE_DIR/BUILD-INFO.tmp" "$RELEASE_DIR/BUILD-INFO.txt"
rm -f "$RELEASE_DIR/BUILD-INFO.txt.symbols"

(cd "$RELEASE_DIR" && find . -type f ! -name SHA256SUMS ! -path './.work/*' -printf '%P\n' | sort | xargs sha256sum) >"$RELEASE_DIR/SHA256SUMS"
rm -rf "$WORK"
echo "release: $RELEASE_DIR"
cat "$RELEASE_DIR/SHA256SUMS"
