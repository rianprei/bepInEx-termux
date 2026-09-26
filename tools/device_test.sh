#!/bin/sh
# T1 kit de validação no device. QUEM EXECUTA É O USUÁRIO (agente não usa adb):
#   tools/device_test.sh <pkg> <dir-de-teste> [timeout_s] [--no-frida] [--dry-run]
# Faz: snapshot inicial → backup de mods/<pkg>/ → instala artefatos via
# staging + su (cp/chmod/chcon) → limpa logs → abre o jogo (monkey) → espera
# as linhas de expect.txt no log.txt → checa crash (pid vazio / Fatal signal
# do pacote no logcat) → imprime PASS/FAIL por expectativa → RESTAURA TUDO e
# CONFERE (listagem + sha256 antes/depois).
# Saída 0 = tudo PASS sem crash; 1 = qualquer FAIL/crash/erro de teste;
# 2 = RESTAURAÇÃO FALHOU (device diferente do inicial).
# --no-frida: pula *.js (SKIP com aviso, não FAIL). --dry-run: imprime os
# comandos adb sem executar nada (pra revisar o script sem device).
set -eu

PKG=${1:?uso: tools/device_test.sh <pkg> <dir-de-teste> [timeout_s] [--no-frida] [--dry-run]}
TDIR=${2:?uso: tools/device_test.sh <pkg> <dir-de-teste> [timeout_s] [--no-frida] [--dry-run]}
shift 2
TIMEOUT=120
NO_FRIDA=0
DRY=0
for a in "$@"; do
    case "$a" in
        --no-frida) NO_FRIDA=1;;
        --dry-run) DRY=1;;
        ''|*[!0-9]*) echo "FAIL: arg inválido: $a"; exit 1;;
        *) TIMEOUT=$a;;
    esac
done

MODS="/data/local/tmp/mods/$PKG"
STAGE="/data/local/tmp/t1-stage-$PKG"
BAK="/data/local/tmp/t1-bak-$PKG"
OUT="/data/data/$PKG/files/bepinex"
EXPECT="$TDIR/expect.txt"
HOST_TMP="$(mktemp -d)"

# Todos os toques no device passam daqui (dry-run imprime no stderr pra não
# poluir capturas $(...)).
dev() {
    if [ "$DRY" = 1 ]; then echo "DRY> adb shell su -c '$1'" >&2; else adb shell su -c "$1"; fi
}
do_adb() {
    if [ "$DRY" = 1 ]; then echo "DRY> adb $*" >&2; else adb "$@"; fi
}

# Foto do estado: modos+tamanhos (sem mtime) e sha256 dos conteúdos.
# Igual antes/depois = restaurado. (Binários do sha: toybox no device.)
snapshot() {
    dev "stat -c '%n %s %a' $MODS $MODS/* $OUT/log.txt $OUT/frida_ok.txt $OUT/frida_count.txt 2>/dev/null; sha256sum $MODS/* $OUT/log.txt 2>/dev/null; true" > "$1" 2>/dev/null
}

restore() {
    [ -f "$HOST_TMP/done" ] && return 0
    touch "$HOST_TMP/done"
    echo "--- restaurando device ---"
    dev "test -d $BAK && rm -rf $MODS && mv $BAK $MODS" || true
    if [ "$(cat "$HOST_TMP/log_present" 2>/dev/null)" = "1" ]; then
        dev "test -f $OUT/log.txt.t1bak && mv $OUT/log.txt.t1bak $OUT/log.txt" || true
    else
        dev "rm -f $OUT/log.txt" || true
    fi
    dev "rm -f $OUT/frida_ok.txt $OUT/frida_count.txt" || true
    dev "rm -rf $STAGE $BAK" || true
    do_adb shell am force-stop "$PKG" >/dev/null 2>&1 || true
    snapshot "$HOST_TMP/after.txt"
    if [ -f "$HOST_TMP/before.txt" ] && diff -q "$HOST_TMP/before.txt" "$HOST_TMP/after.txt" >/dev/null 2>&1; then
        echo "device restaurado"
    else
        echo "RESTAURACAO FALHOU (estado difere do inicial; ver diff)"
        diff "$HOST_TMP/before.txt" "$HOST_TMP/after.txt" 2>/dev/null | head -20 || true
        rm -rf "$HOST_TMP"
        exit 2
    fi
    rm -rf "$HOST_TMP"
}
trap restore EXIT INT TERM HUP

[ -d "$TDIR" ] || { echo "FAIL: dir de teste ausente: $TDIR"; exit 1; }
[ -f "$EXPECT" ] || { echo "FAIL: sem expect.txt em $TDIR"; exit 1; }
if [ "$DRY" = 0 ]; then
    command -v adb >/dev/null 2>&1 || { echo "FAIL: adb não encontrado"; exit 1; }
    [ "$(adb get-state 2>/dev/null)" = "device" ] || { echo "FAIL: device não conectado"; exit 1; }
    adb shell su -c true 2>/dev/null || { echo "FAIL: su sem resposta (root?)"; exit 1; }
    dev "command -v sha256sum" >/dev/null || { echo "FAIL: sem sha256sum no device (restauração não verificável)"; exit 1; }
else
    echo "DRY-RUN: nenhum comando executa de verdade"
fi

echo "--- snapshot inicial ---"
snapshot "$HOST_TMP/before.txt"

echo "--- backup mods/$PKG ---"
dev "rm -rf $BAK $STAGE && cp -a $MODS $BAK && mkdir -p $STAGE" || { echo "FAIL: sem pasta mods/$PKG no device (instale os mods antes)"; exit 1; }
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
SKIP_JS=0
if [ "$NEED_JS" = 1 ]; then
    if [ "$NO_FRIDA" = 1 ]; then
        SKIP_JS=1
        echo "SKIP: *.js ignorado (--no-frida)"
    elif ! dev "test -f $MODS/u_frida.so" || ! dev "test -f $MODS/frida-gadget.bin"; then
        SKIP_JS=1
        echo "SKIP: sem u_frida.so/frida-gadget.bin no device — *.js não testado (instale pra cobrir)"
    fi
fi
if [ "$NEED_PATCH" = 1 ]; then
    dev "test -f $MODS/u_patch.so" || { echo "FAIL: u_patch.so ausente em mods/$PKG (instale antes)"; exit 1; }
fi

echo "--- instalando artefatos de $TDIR ---"
for f in "$TDIR"/*.patch "$TDIR"/*.conf "$TDIR"/*.js; do
    [ -e "$f" ] || continue
    case "$f" in
        *.js) [ "$SKIP_JS" = 1 ] && continue;;
    esac
    base=$(basename "$f")
    # mods/ é root 755: push vai pro staging (shell escreve), su instala.
    do_adb push "$f" "$STAGE/$base" >/dev/null || { echo "FAIL: push $base"; exit 1; }
    dev "cp $STAGE/$base $MODS/$base && chmod 644 $MODS/$base" || { echo "FAIL: instala $base"; exit 1; }
    # Sem 2>/dev/null aqui de propósito: no dry-run o eco DRY iria pro
    # /dev/null junto (dev() imprime no stderr); no real o barulho do adb
    # só aparece no caminho de aviso mesmo.
    dev "chcon u:object_r:bepinex_mod_file:s0 $MODS/$base" \
        || echo "AVISO: chcon falhou em $base (tipo inexistente? Enforcing pode negar)"
done

if [ "$DRY" = 1 ]; then
    echo "DRY: pularia limpar logs, force-stop, launch, espera de até ${TIMEOUT}s,"
    echo "DRY: checagem de crash, e estas expectativas (WOULD-CHECK):"
    while IFS= read -r re || [ -n "$re" ]; do
        case "$re" in ''|\#*) continue;; esac
        echo "DRY-WOULD-CHECK: $re"
    done < "$EXPECT"
    [ "$SKIP_JS" = 0 ] && [ "$NEED_JS" = 1 ] && echo "DRY-WOULD-CHECK: frida_ok.txt + frida_count.txt"
    echo "DRY-RUN OK (nada executado, nada alterado)"
    exit 0
fi

dev "rm -f $OUT/log.txt $OUT/frida_ok.txt $OUT/frida_count.txt"
do_adb shell am force-stop "$PKG" >/dev/null 2>&1 || true
do_adb shell monkey -p "$PKG" -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1 || true
PID=""
for _i in 1 2 3 4 5; do
    sleep 3
    PID=$(do_adb shell pidof "$PKG" 2>/dev/null | tr -d '\r' || true)
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
NEWPID=$(do_adb shell pidof "$PKG" 2>/dev/null | tr -d '\r' || true)
if [ -z "$NEWPID" ]; then
    echo "CRASH: processo do jogo morreu durante o teste"
    CRASH=1
fi
FATAL=$(do_adb logcat -d 2>/dev/null | tr -d '\r' | grep -E "Fatal signal.*${PKG}|FATAL EXCEPTION.*${PKG}" || true)
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

if [ "$NEED_JS" = 1 ] && [ "$SKIP_JS" = 0 ]; then
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
