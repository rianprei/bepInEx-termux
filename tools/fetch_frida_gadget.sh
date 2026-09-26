#!/bin/sh
# Baixa o frida-gadget android-arm64 pinado (versão + sha256 verificados).
# Roda no HOST (curl + xz). O binário NÃO vai pro git (.gitignore cobre
# mods/u_frida/gadget/): tools/deploy_frida.sh instala daqui no device
# (plano F5: Manager empacotar nos assets — ainda sem código).
# Uso: sh tools/fetch_frida_gadget.sh
set -eu
# Procedência dos hashes (medidos 2026-09-26, release oficial):
#   URL: https://github.com/frida/frida/releases/download/17.19.0/frida-gadget-17.19.0-android-arm64.so.xz
#   (página da release: https://github.com/frida/frida/releases/tag/17.19.0)
# Comandos usados:
#   curl -sL -o fg.xz <URL> && sha256sum fg.xz && wc -c fg.xz
#   unxz -k fg.xz && sha256sum fg && ls -l fg   (.so: 25233136 bytes)
LOCK="$(dirname "$0")/deps.lock"
IFS='|' read -r _ VERSION XZ_SHA256 BASE _ <<EOF
$(grep '^frida-gadget-xz|' "$LOCK")
EOF
BASE="${BASE%/*}"
XZ_SIZE="6969732"
# sha256 do .so já desempatado (referência, medido 2026-09-26):
IFS='|' read -r _ _ SO_SHA256 _ _ <<EOF
$(grep '^frida-gadget-so|' "$LOCK")
EOF
OUTDIR="$(dirname "$0")/../mods/u_frida/gadget"
mkdir -p "$OUTDIR"
cd "$OUTDIR"
XZ="frida-gadget-${VERSION}-android-arm64.so.xz"
# .xz ruim (download cortado/corrompido) é apagado: senão o próximo run
# pula o download e falha em loop no sha.
bad_xz() { rm -f "$XZ"; echo "ERRO: $1 — $XZ apagado, rode de novo" >&2; exit 1; }
if [ ! -f "$XZ" ]; then
    curl -sL --fail -o "$XZ" "${BASE}/${XZ}" || bad_xz "download falhou"
fi
echo "${XZ_SHA256}  ${XZ}" | sha256sum -c - || bad_xz "sha256 do .xz não confere"
ACTUAL_SIZE="$(wc -c < "$XZ" | tr -d ' ')"
[ "$ACTUAL_SIZE" = "$XZ_SIZE" ] || bad_xz "tamanho inesperado: $ACTUAL_SIZE"
command -v unxz >/dev/null 2>&1 || { echo "ERRO: unxz não encontrado (pacote xz-utils)."; exit 1; }
unxz -k -f "$XZ"
SO="frida-gadget-${VERSION}-android-arm64.so"
echo "${SO_SHA256}  ${SO}" | sha256sum -c -
# Nome final SEM .so (ver u_frida_config.h: loader não pode dlopen'ar sozinho).
mv -f "$SO" frida-gadget.bin
echo "$VERSION" > VERSION.txt
ls -l frida-gadget.bin
# Instalar no device (bin + .js + .config com su+chcon): tools/deploy_frida.sh
