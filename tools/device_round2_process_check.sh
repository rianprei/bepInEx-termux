#!/bin/sh
set -eu

PKG=${1:?uso: tools/device_round2_process_check.sh <pacote> <mod.so>}
MOD=${2:?uso: tools/device_round2_process_check.sh <pacote> <mod.so>}
case "$PKG" in
    *[!A-Za-z0-9_.]*|.*|*..*|*.) echo "FAIL: pacote inválido"; exit 2;;
esac
printf '%s\n' "$PKG" | grep -Eq '^[A-Za-z0-9_]+(\.[A-Za-z0-9_]+)+$' \
    || { echo "FAIL: pacote inválido"; exit 2; }
case "$MOD" in
    *[!A-Za-z0-9_.-]*) echo "FAIL: nome do mod inválido"; exit 2;;
esac
case "$MOD" in *.so) ;; *) echo "FAIL: informe o nome de um .so"; exit 2;; esac

ADB=${ADB:-adb}
"$ADB" get-state 2>/dev/null | grep -qx device \
    || { echo "FAIL: device não conectado"; exit 2; }
printf 'true\n' | "$ADB" shell su >/dev/null 2>&1 \
    || { echo "FAIL: root Magisk não respondeu"; exit 2; }
PROCESSES=$("$ADB" shell ps -A -o PID,NAME 2>/dev/null | tr -d '\r') \
    || { echo "FAIL: não consegui listar processos"; exit 2; }
PIDS=$(printf '%s\n' "$PROCESSES" | awk -v pkg="$PKG" '
    NR > 1 && index($2, pkg ":") == 1 && length($2) > length(pkg) + 1 {
        print $1, $2
    }
')
if [ -z "$PIDS" ]; then
    echo "SKIP: nenhum processo normal com :sufixo está ativo para $PKG"
    exit 3
fi

FOUND=0
FAILED=0
while read -r PID NAME; do
    [ -n "$PID" ] || continue
    printf 'processo encontrado: %s pid=%s\n' "$NAME" "$PID"
    MAPS=$(printf 'grep -F %s /proc/%s/maps\n' \
        "/data/local/tmp/mods/$PKG/$MOD" "$PID" | "$ADB" shell su 2>/dev/null | tr -d '\r') \
        || MAPS=
    if printf '%s\n' "$MAPS" | grep -Fq "/data/local/tmp/mods/$PKG/$MOD"; then
        echo "PASS: $MOD está mapeado no processo $NAME"
        FOUND=1
    else
        echo "FAIL: $MOD não está mapeado no processo $NAME"
        FAILED=1
    fi
done <<EOF
$PIDS
EOF

[ "$FAILED" -eq 0 ] && [ "$FOUND" -eq 1 ] || exit 1
