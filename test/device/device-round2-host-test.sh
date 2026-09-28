#!/bin/sh
set -eu

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT HUP INT TERM
mkdir -p "$TMP/bin" "$TMP/evidence"

cat > "$TMP/bin/adb" <<'EOF'
#!/bin/sh
set -eu
case "$1" in
    get-state) echo device;;
    logcat)
        case "$2" in
            -c) exit 0;;
            -d)
                if [ "${3:-}" = "--pid=1234" ]; then
                    [ "${FAKE_CRASH:-0}" = 1 ] && echo "Fatal signal 11"
                else
                    echo "I fake: logcat captured"
                fi
                if [ "${FAKE_AVC:-}" = unrelated ]; then
                    echo 'avc: denied { read } for name="other" permissive=0'
                elif [ "${FAKE_AVC:-}" = project ]; then
                    echo 'avc: denied { read } for path="/data/adb/bepinex/mods/com.example.game/u_patch.so" permissive=0'
                fi
                :
                ;;
        esac
        ;;
    shell)
        case "$2" in
            getenforce) echo Enforcing;;
            pidof) echo 1234;;
            monkey) echo "Events injected: 1";;
            ps)
                if [ "${FAKE_SUFFIX:-1}" = 1 ]; then
                    printf 'PID NAME\n567 com.example.game:unity\n'
                else
                    printf 'PID NAME\n567 com.example.game\n'
                fi
                ;;
            su)
                command=$(cat)
                case "$command" in
                    if*) echo ROUND2_PICKER_PASS;;
                    sha256sum*)
                        echo "${FAKE_FRIDA_HASH:-c87c53efc10a9b6f2f4259b7b962d403cac729d6ee6f6a83f679489f92671a3a}  frida-gadget.bin"
                        ;;
                    grep*)
                        if [ "${FAKE_MAPS:-1}" = 1 ]; then
                            echo "1000-2000 r-xp /data/adb/bepinex/mods/com.example.game/sa2ammo.so"
                        fi
                        ;;
                    cat*) echo "fake mod log";;
                esac
                ;;
        esac
        ;;
esac
EOF
cat > "$TMP/bin/sleep" <<'EOF'
#!/bin/sh
printf '%s\n' "$1" >> "$FAKE_SLEEP_LOG"
EOF
chmod +x "$TMP/bin/adb" "$TMP/bin/sleep"

export PATH="$TMP/bin:$PATH"
export ADB="$TMP/bin/adb"
export FAKE_SLEEP_LOG="$TMP/sleeps.txt"
export ROUND2_EVIDENCE_DIR="$TMP/evidence"

echo "[device-round2] hold manual do device_test"
DRY_OUTPUT=$("$ROOT/tools/device_test.sh" com.example.game \
    "$ROOT/test/device/sa2-field" --dry-run --hold-after-pass=60 2>&1)
printf '%s\n' "$DRY_OUTPUT" | grep -Fq \
    'manteria o jogo aberto por 60s depois das expectativas' \
    || { echo "FAIL: dry-run não anunciou a pausa manual"; exit 1; }
if "$ROOT/tools/device_test.sh" com.example.game \
    "$ROOT/test/device/sa2-field" --dry-run --hold-after-pass=601 >/dev/null 2>&1; then
    echo "FAIL: pausa maior que 600s aceita"
    exit 1
fi
echo "PASS: hold manual validado e limitado"

echo "[device-round2] expectativa Frida"
FRIDA_DRY_OUTPUT=$("$ROOT/tools/device_test.sh" com.example.game \
    "$ROOT/test/device/sa2" --dry-run 2>&1)
printf '%s\n' "$FRIDA_DRY_OUTPUT" | grep -Fq \
    'DRY-WOULD-CHECK: \[u_frida\] gadget ativo' \
    || { echo "FAIL: smoke não exige log de gadget ativo"; exit 1; }
echo "PASS: smoke exige gadget ativo no log do jogo"

echo "[device-round2] cópia do picker"
"$ROOT/tools/device_round2_picker_check.sh" com.example.game

echo "[device-round2] pin Frida"
"$ROOT/tools/device_round2_frida_check.sh" com.example.game
FAKE_FRIDA_HASH=invalid
export FAKE_FRIDA_HASH
set +e
"$ROOT/tools/device_round2_frida_check.sh" com.example.game >/dev/null 2>&1
FRIDA_RC=$?
set -e
[ "$FRIDA_RC" -eq 1 ] || { echo "FAIL: hash incorreto do Frida passou"; exit 1; }
echo "PASS: hash fora do pin Frida falha"

echo "[device-round2] soak host simulado"
"$ROOT/tools/device_round2_soak.sh" com.example.game
grep -qx 600 "$FAKE_SLEEP_LOG" || { echo "FAIL: script não esperou 600s"; exit 1; }
grep -qx 'result=PASS' "$ROUND2_EVIDENCE_DIR"/soak-*/summary.txt \
    || { echo "FAIL: resumo do soak não é PASS"; exit 1; }
echo "PASS: janela obrigatória de 600s e evidências registradas"

echo "[device-round2] processo :sufixo"
"$ROOT/tools/device_round2_process_check.sh" com.example.game sa2ammo.so
FAKE_SUFFIX=0
export FAKE_SUFFIX
set +e
SKIP_OUTPUT=$("$ROOT/tools/device_round2_process_check.sh" \
    com.example.game sa2ammo.so 2>&1)
SKIP_RC=$?
set -e
if [ "$SKIP_RC" -ne 3 ] || ! printf '%s\n' "$SKIP_OUTPUT" | grep -Fq 'SKIP:'; then
    echo "FAIL: ausência de processo suffix não é SKIP"
    exit 1
fi
echo "PASS: ausência de :sufixo reportada como SKIP"

FAKE_SUFFIX=1
FAKE_MAPS=0
export FAKE_SUFFIX FAKE_MAPS
set +e
"$ROOT/tools/device_round2_process_check.sh" com.example.game sa2ammo.so >/dev/null 2>&1
MAP_RC=$?
set -e
[ "$MAP_RC" -eq 1 ] || { echo "FAIL: mod ausente do processo não falhou"; exit 1; }
echo "PASS: mod não mapeado no processo :sufixo falha"

FAKE_CRASH=1
export FAKE_CRASH
set +e
"$ROOT/tools/device_round2_soak.sh" com.example.game >/dev/null 2>&1
CRASH_RC=$?
set -e
[ "$CRASH_RC" -eq 1 ] || { echo "FAIL: crash de soak simulado não falhou"; exit 1; }
echo "PASS: crash simulado durante o soak falha"

FAKE_CRASH=0
FAKE_AVC=unrelated
export FAKE_CRASH FAKE_AVC
"$ROOT/tools/device_round2_soak.sh" com.example.game >/dev/null
FAKE_AVC=project
export FAKE_AVC
set +e
"$ROOT/tools/device_round2_soak.sh" com.example.game >/dev/null 2>&1
AVC_RC=$?
set -e
[ "$AVC_RC" -eq 1 ] || { echo "FAIL: AVC em caminho de mod não falhou"; exit 1; }
echo "PASS: AVC alheio só é coletado; AVC em caminho do mod falha"
