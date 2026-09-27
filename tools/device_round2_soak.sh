#!/bin/sh
set -eu

PKG=${1:?uso: tools/device_round2_soak.sh <pacote>}
case "$PKG" in
    *[!A-Za-z0-9_.]*|.*|*..*|*.) echo "FAIL: pacote inválido"; exit 2;;
esac
printf '%s\n' "$PKG" | grep -Eq '^[A-Za-z0-9_]+(\.[A-Za-z0-9_]+)+$' \
    || { echo "FAIL: pacote inválido"; exit 2; }

ADB=${ADB:-adb}
EVIDENCE_ROOT=${ROUND2_EVIDENCE_DIR:-"$(cd "$(dirname "$0")/.." && pwd)/out/device-round2"}
STAMP=$(date +%Y%m%d-%H%M%S)
RUN_DIR="$EVIDENCE_ROOT/soak-$PKG-$STAMP"
PKG_RE=$(printf '%s' "$PKG" | sed 's/[.]/\\./g')
mkdir -p "$RUN_DIR"

"$ADB" get-state 2>/dev/null | grep -qx device \
    || { echo "FAIL: device não conectado"; exit 2; }
printf 'true\n' | "$ADB" shell su >/dev/null 2>&1 \
    || { echo "FAIL: root Magisk não respondeu"; exit 2; }
ENFORCE=$("$ADB" shell getenforce 2>/dev/null | tr -d '\r') \
    || { echo "FAIL: não consegui ler SELinux"; exit 2; }
printf '%s\n' "$ENFORCE" > "$RUN_DIR/selinux.txt"
"$ADB" logcat -d -b all > "$RUN_DIR/logcat-before.txt" \
    || { echo "FAIL: não consegui salvar logcat anterior"; exit 2; }

"$ADB" logcat -c || { echo "FAIL: não consegui limpar logcat"; exit 2; }
"$ADB" shell monkey -p "$PKG" -c android.intent.category.LAUNCHER 1 \
    > "$RUN_DIR/launch.txt" 2>&1 || { echo "FAIL: não consegui abrir o jogo"; exit 1; }

PID=
for _ in 1 2 3 4 5; do
    sleep 3
    PID=$("$ADB" shell pidof "$PKG" 2>/dev/null | tr -d '\r' | awk '{print $1}')
    [ -n "$PID" ] && break
done
[ -n "$PID" ] || { echo "FAIL: jogo não subiu em até 15s"; exit 1; }
printf '%s\n' "$PID" > "$RUN_DIR/pid.txt"
printf 'SOAK: jogue agora por 10 minutos no jogo em primeiro plano (PID %s).\n' "$PID"
sleep 600

END_PIDS=$("$ADB" shell pidof "$PKG" 2>/dev/null | tr -d '\r' || true)
printf '%s\n' "$END_PIDS" > "$RUN_DIR/pid-after.txt"
"$ADB" logcat -d -b all > "$RUN_DIR/logcat.txt" \
    || { echo "FAIL: não consegui coletar logcat"; exit 1; }
"$ADB" logcat -d --pid="$PID" > "$RUN_DIR/game-logcat.txt" \
    || { echo "FAIL: não consegui coletar logcat do processo"; exit 1; }
printf 'cat /data/data/%s/files/bepinex/log.txt\n' "$PKG" \
    | "$ADB" shell su > "$RUN_DIR/mod-log.txt" 2>&1 \
    || { echo "FAIL: não consegui coletar log.txt"; exit 1; }

FAIL=0
case " $END_PIDS " in
    *" $PID "*) echo "PASS: processo original continua vivo";;
    *) echo "FAIL: processo original não está mais ativo"; FAIL=1;;
esac
if grep -E 'Fatal signal|FATAL EXCEPTION' "$RUN_DIR/game-logcat.txt" > "$RUN_DIR/crashes.txt"; then
    echo "FAIL: crash nativo/exceção durante o soak"
    FAIL=1
else
    : > "$RUN_DIR/crashes.txt"
    echo "PASS: sem Fatal signal/FATAL EXCEPTION no PID inicial"
fi
if grep -E "ANR in .*${PKG_RE}([[:space:]]|$)" "$RUN_DIR/logcat.txt" > "$RUN_DIR/anr.txt"; then
    echo "FAIL: ANR do pacote durante o soak"
    FAIL=1
else
    : > "$RUN_DIR/anr.txt"
    echo "PASS: sem ANR do pacote no logcat"
fi
grep -E 'avc: denied.*permissive=0' "$RUN_DIR/logcat.txt" > "$RUN_DIR/avc-enforcing.txt" || true
if grep -E "$PKG_RE|/data/local/tmp/mods|/data/adb/bepinex" \
    "$RUN_DIR/avc-enforcing.txt" > "$RUN_DIR/avc-project.txt"; then
    echo "FAIL: AVC do pacote/caminho dos mods em Enforcing (examinar avc-project.txt)"
    FAIL=1
else
    : > "$RUN_DIR/avc-project.txt"
    if [ -s "$RUN_DIR/avc-enforcing.txt" ]; then
        echo "INFO: AVCs de outros componentes salvos em avc-enforcing.txt"
    else
        echo "PASS: sem avc: denied permissive=0"
    fi
fi
printf 'package=%s\npid=%s\nselinux=%s\nresult=%s\n' \
    "$PKG" "$PID" "$ENFORCE" "$([ "$FAIL" -eq 0 ] && echo PASS || echo FAIL)" \
    > "$RUN_DIR/summary.txt"
echo "Evidências: $RUN_DIR"
[ "$FAIL" -eq 0 ] || exit 1
echo "RESULTADO: PASS (600s; sem crash/ANR/AVC do pacote ou dos mods; processo original vivo)"
