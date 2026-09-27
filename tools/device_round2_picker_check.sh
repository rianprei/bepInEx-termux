#!/bin/sh
set -eu

PKG=${1:?uso: tools/device_round2_picker_check.sh <pacote>}
case "$PKG" in
    *[!A-Za-z0-9_.]*|.*|*..*|*.) echo "FAIL: pacote inválido"; exit 2;;
esac
printf '%s\n' "$PKG" | grep -Eq '^[A-Za-z0-9_]+(\.[A-Za-z0-9_]+)+$' \
    || { echo "FAIL: pacote inválido"; exit 2; }

ADB=${ADB:-adb}
"$ADB" get-state 2>/dev/null | grep -qx device \
    || { echo "FAIL: device não conectado"; exit 2; }
SOURCE=/sdcard/Download/round2-picker-probe.bpatch
DEST="/data/local/tmp/mods/$PKG/round2-picker-probe.bpatch"
OUTPUT=$(printf 'if [ -f %s ] && [ ! -L %s ] && [ -f %s ] && [ ! -L %s ] && cmp -s %s %s; then echo ROUND2_PICKER_PASS; else echo ROUND2_PICKER_FAIL; fi\n' \
    "$SOURCE" "$SOURCE" "$DEST" "$DEST" "$SOURCE" "$DEST" | "$ADB" shell su 2>&1 | tr -d '\r')
case "$OUTPUT" in
    *ROUND2_PICKER_PASS*) echo "PASS: arquivo selecionado em Download foi instalado byte a byte em $DEST";;
    *) echo "FAIL: origem ou destino ausente/diferente; root respondeu: $OUTPUT"; exit 1;;
esac
