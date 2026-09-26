#!/bin/sh
# T1 kit de validação no device. QUEM EXECUTA É O USUÁRIO (agente não usa adb):
#   tools/device_test.sh <pkg> <dir-de-teste> [timeout_s=120]
# Faz: backup de mods/<pkg>/ → instala artefatos (*.patch/*.conf/*.js) →
# limpa logs → abre o jogo (monkey) → espera as linhas de expect.txt no
# log.txt → checa crash (pid vazio / Fatal signal do pacote no logcat) →
# imprime PASS/FAIL por expectativa → RESTAURA TUDO (trap em EXIT/INT/TERM).
# Saída 0 = tudo PASS sem crash; 1 = qualquer FAIL/crash/erro.
# Pré-requisitos já instalados no device: u_patch.so (se houver *.patch),
# u_frida.so + frida-gadget.bin (se houver *.js). Faltando = FAIL rápido.
set -eu

PKG=${1:?uso: tools/device_test.sh <pkg> <dir-de-teste> [timeout_s]}
TDIR=${2:?uso: tools/device_test.sh <pkg> <dir-de-teste> [timeout_s]}
TIMEOUT=${3:-120}
MODS="/data/local/tmp/mods/$PKG"
BAK="/data/local/tmp/t1-bak-$PKG"
OUT="/data/data/$PKG/files/bepinex"
EXPECT="$TDIR/expect.txt"
HOST_TMP="$(mktemp -d)"

[ -d "$TDIR" ] || { echo "FAIL: dir de teste ausente: $TDIR"; exit 1; }
[ -f "$EXPECT" ] || { echo "FAIL: sem expect.txt em $TDIR"; exit 1; }
command -v adb >/dev/null 2>&1 || { echo "FAIL: adb não encontrado"; exit 1; }
[ "$(adb get-state 2>/dev/null)" = "device" ] || { echo "FAIL: device não conectado"; exit 1; }
adb shell su -c true 2>/dev/null || { echo "FAIL: su sem resposta (root?)"; exit 1; }

dev() { adb shell su -c "$1"; }

restore() {
    echo "--- restaurando device ---"
    # Só mexe se o backup existe (saída antes do backup = nada pra restaurar).
    dev "test -d $BAK && rm -rf $MODS && mv $BAK $MODS" || true
    if [ "$(cat "$HOST_TMP/log_present" 2>/dev/null)" = "1" ]; then
        dev "test -f $OUT/log.txt.t1bak && mv $OUT/log.txt.t1bak $OUT/log.txt" || true
    else
        dev "rm -f $OUT/log.txt" || true
    fi
    dev "rm -f $OUT/frida_ok.txt $OUT/frida_count.txt" || true
    adb shell am force-stop "$PKG" >/dev/null 2>&1 || true
    if [ -n "${HOST_TMP:-}" ]; then rm -rf "$HOST_TMP"; fi
    echo "device restaurado"
}
trap restore EXIT INT TERM HUP

echo "--- backup mods/$PKG ---"
dev "rm -rf $BAK && cp -a $MODS $BAK" || { echo "FAIL: sem pasta mods/$PKG no device (instale os mods antes)"; exit 1; }
if dev "test -f $OUT/log.txt"; then
    echo 1 > "$HOST_TMP/log_present"
    dev "cp $OUT/log.txt $OUT/log.txt.t1bak"
else
    echo 0 > "$HOST_TMP/log_present"
fi

NEED_PATCH=0
NEED_JS=0
for f in "$TDIR"/*.patch "$TDIR"/*.js; do
    [ -e "$f" ] || continue
    case "$f" in *.patch) NEED_PATCH=1;; *.js) NEED_JS=1;; esac
done
if [ "$NEED_PATCH" = 1 ]; then
    dev "test -f $MODS/u_patch.so" || { echo "FAIL: u_patch.so ausente em mods/$PKG (instale antes)"; exit 1; }
fi
if [ "$NEED_JS" = 1 ]; then
    dev "test -f $MODS/u_frida.so" || { echo "FAIL: u_frida.so ausente em mods/$PKG (instale antes)"; exit 1; }
    dev "test -f $MODS/frida-gadget.bin" || { echo "FAIL: frida-gadget.bin ausente em mods/$PKG (instale antes)"; exit 1; }
fi

echo "--- instalando artefatos de $TDIR ---"
for f in "$TDIR"/*.patch "$TDIR"/*.conf "$TDIR"/*.js; do
    [ -e "$f" ] || continue
    base=$(basename "$f")
    adb push "$f" "$MODS/$base" >/dev/null || { echo "FAIL: push $base"; exit 1; }
    dev "chmod 644 $MODS/$base" || { echo "FAIL: chmod $base"; exit 1; }
    dev "chcon u:object_r:bepinex_mod_file:s0 $MODS/$base" 2>/dev/null \
        || echo "AVISO: chcon falhou em $base (tipo inexistente? Enforcing pode negar)"
done

dev "rm -f $OUT/log.txt $OUT/frida_ok.txt $OUT/frida_count.txt"
adb shell am force-stop "$PKG" >/dev/null 2>&1 || true
adb shell monkey -p "$PKG" -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1 || true
PID=""
for _i in 1 2 3 4 5; do
    sleep 3
    PID=$(adb shell pidof "$PKG" 2>/dev/null | tr -d '\r' || true)
    [ -n "$PID" ] && break
done
[ -n "$PID" ] || { echo "FAIL: jogo não subiu em ~15s (pid vazio)"; exit 1; }
echo "jogo no ar, pid $PID — esperando até ${TIMEOUT}s"

ELAPSED=0
LOG=""
ALL_OK=0
while [ "$ELAPSED" -lt "$TIMEOUT" ]; do
    LOG=$(dev "cat $OUT/log.txt 2>/dev/null" | tr -d '\r' || true)
    ALL_OK=1
    while IFS= read -r re || [ -n "$re" ]; do
        case "$re" in ''|\#*) continue;; esac
        echo "$LOG" | grep -Eq "$re" || ALL_OK=0
    done < "$EXPECT"
    [ "$ALL_OK" = 1 ] && break
    sleep 3
    ELAPSED=$((ELAPSED + 3))
done

CRASH=0
NEWPID=$(adb shell pidof "$PKG" 2>/dev/null | tr -d '\r' || true)
if [ -z "$NEWPID" ]; then
    echo "CRASH: processo do jogo morreu durante o teste"
    CRASH=1
fi
FATAL=$(adb logcat -d 2>/dev/null | tr -d '\r' | grep -E "Fatal signal.*${PKG}|FATAL EXCEPTION.*${PKG}" || true)
if [ -n "$FATAL" ]; then
    echo "CRASH (logcat):"
    echo "$FATAL" | head -5
    CRASH=1
fi

echo "--- expectativas ($EXPECT) ---"
FAIL_N=0
while IFS= read -r re || [ -n "$re" ]; do
    case "$re" in ''|\#*) continue;; esac
    if echo "$LOG" | grep -Eq "$re"; then
        echo "PASS: $re"
    else
        echo "FAIL: $re"
        FAIL_N=$((FAIL_N + 1))
    fi
done < "$EXPECT"

if [ "$NEED_JS" = 1 ]; then
    if dev "test -f $OUT/frida_ok.txt"; then
        echo "PASS: frida_ok.txt existe"
        dev "cat $OUT/frida_ok.txt" | tr -d '\r'
    else
        echo "FAIL: frida_ok.txt ausente (script não executou)"
        FAIL_N=$((FAIL_N + 1))
    fi
    if dev "test -f $OUT/frida_count.txt"; then
        echo "PASS: frida_count.txt existe"
        dev "cat $OUT/frida_count.txt" | tr -d '\r'
    else
        echo "FAIL: frida_count.txt ausente (intercept não contou)"
        FAIL_N=$((FAIL_N + 1))
    fi
fi

if [ "$CRASH" = 1 ] || [ "$FAIL_N" -gt 0 ]; then
    echo "RESULTADO: FAIL ($FAIL_N falhas, crash=$CRASH)"
    exit 1
fi
echo "RESULTADO: PASS (tudo aplicado, sem crash)"
