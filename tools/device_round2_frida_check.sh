#!/bin/sh
set -eu

PKG=${1:?uso: tools/device_round2_frida_check.sh <pacote>}
case "$PKG" in
    *[!A-Za-z0-9_.]*|.*|*..*|*.) echo "FAIL: pacote inválido"; exit 2;;
esac
printf '%s\n' "$PKG" | grep -Eq '^[A-Za-z0-9_]+(\.[A-Za-z0-9_]+)+$' \
    || { echo "FAIL: pacote inválido"; exit 2; }

ROOT=$(cd "$(dirname "$0")/.." && pwd)
ADB=${ADB:-adb}
VERSION=$(awk -F'|' '$1 == "frida-gadget-xz" { print $2; exit }' "$ROOT/tools/deps.lock")
EXPECTED=$(awk -F'|' '$1 == "frida-gadget-so" { print $3; exit }' "$ROOT/tools/deps.lock")
[ "$VERSION" = 17.18.0 ] && [ -n "$EXPECTED" ] \
    || { echo "FAIL: deps.lock não aponta para Frida 17.18.0"; exit 1; }
"$ADB" get-state 2>/dev/null | grep -qx device \
    || { echo "FAIL: device não conectado"; exit 2; }
OUTPUT=$(printf 'sha256sum /data/adb/bepinex/mods/%s/frida-gadget.bin\n' "$PKG" \
    | "$ADB" shell su 2>&1 | tr -d '\r')
ACTUAL=$(printf '%s\n' "$OUTPUT" | awk 'NF { print $1; exit }')
if [ "$ACTUAL" != "$EXPECTED" ]; then
    echo "FAIL: hash do gadget no device não corresponde ao pin 17.18.0"
    echo "$OUTPUT"
    exit 1
fi
echo "PASS: frida-gadget.bin no device corresponde ao SHA-256 pinado 17.18.0"
