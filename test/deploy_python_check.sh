#!/usr/bin/env bash
# test/deploy_python_check.sh — o deploy falha ANTES de enviar quando o
# aparelho não tem python do Termux.
#
# O emissor do push_mod (tools/push_mod_emit.py) roda NO APARELHO, com o
# python do Termux, e o Termux de base não instala python. Sem o preflight a
# falha vinha no meio do fluxo: o .so e o emissor já tinham sido empurrados, o
# erro era o do shell do aparelho, e o usuário ficava com residuo para limpar.
#
# O teste sobe um adb FALSO que registra toda chamada e responde o que o caso
# pede. Três casos, sem device:
#   1. python ausente → exit != 0, mensagem diz `pkg install python` e
#      NENHUMA chamada de push aconteceu (nada foi enviado);
#   2. python presente → o preflight passa e o deploy chega ao push (o check
#      não pode estar barrando o caminho feliz);
#   3. o "Requer:" dos dois deploy.sh cita o python, porque o preflight sem a
#      documentação é meia solução.
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
NDK_BUILD=${NDK_BUILD:-$HOME/Android/Sdk/ndk/23.2.8568313/ndk-build}

die() { echo "deploy_python_check: $*" >&2; exit 1; }

# --- adb falso: registra tudo e decide o python ---------------------------
cat > "$WORK/adb" <<'EOS'
#!/usr/bin/env bash
# Chamadas de adb que o deploy faz, com o python presente ou ausente.
echo "adb $*" >> "$ADB_LOG"
if [ "${1:-}" = "shell" ]; then
    # O preflight do deploy é `adb shell "test -x '<caminho do python>'"`.
    # Casa por padrão, não por igualdade: o caminho tem variants por instalação.
    case "${2:-}" in
        "test -x "*python3*) [ "${FAKE_PYTHON_PRESENT:-0}" = "1" ] && exit 0; exit 1 ;;
    esac
fi
case "${1:-}" in
    push) echo "1 file pushed."; exit 0 ;;
    shell) echo "ok"; exit 0 ;;
    *) exit 0 ;;
esac
EOS
chmod +x "$WORK/adb"
export PATH="$WORK:$PATH"
export ADB_LOG="$WORK/adb.log"

# O .so real: o deploy passa por symbols_ship (strip de verdade) antes do
# preflight, então o teste precisa de um binário de verdade. O gate já
# buildou os mods; standalone, builda o que falta. Falha de build é erro
# explícito, nunca um pulo silencioso.
ensure_built() {
    local mod=$1 so="$ROOT/mods/$1/libs/arm64-v8a/lib$1.so"
    [ -f "$so" ] && return 0
    [ -x "$NDK_BUILD" ] || die "sem $so e sem ndk-build em $NDK_BUILD para buildar"
    ( cd "$ROOT/mods/$mod" && "$NDK_BUILD" -C . NDK_PROJECT_PATH=. \
        APP_BUILD_SCRIPT=jni/Android.mk NDK_APPLICATION_MK=jni/Application.mk \
        -B -j4 ) >"$WORK/build-$mod.log" 2>&1 ||
        die "ndk-build de $mod falhou (veja $WORK/build-$mod.log)"
    [ -f "$so" ] || die "ndk-build de $mod nao gerou $so"
}

# --- (1) python ausente: recusa, diz o comando, nao envia nada -------------
: > "$ADB_LOG"
ensure_built kungfux
out="$(cd "$ROOT/mods/kungfux" && FAKE_PYTHON_PRESENT=0 TERMUX_PY=/data/data/com.termux/files/usr/bin/python3 \
    ./deploy.sh </dev/null 2>&1)" && rc=0 || rc=$?
[ "$rc" -ne 0 ] || die "deploy aceitou rodar com o python ausente (exit 0)"
printf '%s' "$out" | grep -q 'pkg install python' ||
    die "a falha nao diz como instalar: $out"
printf '%s' "$out" | grep -qi 'python' ||
    die "a falha nao diz QUE dependencia falta: $out"
if grep -q '^adb push' "$ADB_LOG"; then
    die "empurrou algo com o python ausente: $(grep '^adb push' "$ADB_LOG")"
fi
echo "deploy_python_check: (1) python ausente -> exit $rc, mensagem acional, nada enviado"

# --- (2) python presente: o preflight deixa passar -------------------------
: > "$ADB_LOG"
out="$(cd "$ROOT/mods/kungfux" && printf 'sim\n' | FAKE_PYTHON_PRESENT=1 \
    TERMUX_PY=/data/data/com.termux/files/usr/bin/python3 ./deploy.sh 2>&1)" && rc=0 || rc=$?
grep -q '^adb push' "$ADB_LOG" ||
    die "com python presente o deploy nao chegou ao push (check barrando o caminho feliz): rc=$rc $out"
echo "deploy_python_check: (2) python presente -> o deploy segue e envia"

# --- (3) o requisito esta escrito, e nos dois deploy.sh --------------------
for mod in kungfux mechabun; do
    head -20 "$ROOT/mods/$mod/deploy.sh" | grep -q 'pkg install python' ||
        die "mods/$mod/deploy.sh: o bloco Requer nao cita 'pkg install python'"
done
echo "deploy_python_check: (3) os dois Requer: citam o python do Termux"

echo "deploy_python_check: OK"
