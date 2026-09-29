#!/usr/bin/env bash
# test/deploy_python_check.sh — o deploy falha ANTES de enviar quando o
# aparelho não tem python do Termux.
#
# O emissor do push_mod (tools/push_mod_emit.py) roda NO APARELHO, com o
# python do Termux, e o Termux de base não instala python. Sem o preflight a
# falha vinha no meio do fluxo: o .so e o emissor já tinham sido empurrados, o
# erro era o do shell do aparelho, e o usuário ficava com residuo para limpar.
#
# O teste sobe um adb FALSO que registra toda chamada e MODELA o Android de
# produção, em vez de responder o que o caso quer. Quatro casos, sem device:
#   1. python ausente → exit != 0, mensagem diz `pkg install python` e
#      NENHUMA chamada de push aconteceu (nada foi enviado);
#   2. python presente → o preflight passa e o deploy chega ao push (o check
#      não pode estar barrando o caminho feliz);
#   3. o "Requer:" dos dois deploy.sh cita o python, porque o preflight sem a
#      documentação é meia solução, e o código não consulta `adb root`;
#   4. aparelho SEM su → exit != 0 com a mensagem de root, e nada enviado.
#
# POR QUE ESTE ADB FALSO TEM `adb root` SEMPRE QUEBRADO. Num aparelho de
# produção o `adb root` falha com "adbd cannot run as root in production builds"
# — e o aparelho tem root do mesmo jeito, via su (Magisk/KernelSU), que é o
# caminho que o deploy usa. Um adb falso com root disponível deixa o bug
# passar em silêncio: o deploy "funciona" num aparelho que não existe. Com o
# adb fiel, reintroduzir o `adb root` no deploy.sh quebra os casos (1) e (2)
# sozinhos, e o teste (3) barra a linha de comando antes disso.
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
NDK_BUILD=${NDK_BUILD:-$HOME/Android/Sdk/ndk/23.2.8568313/ndk-build}

die() { echo "deploy_python_check: $*" >&2; exit 1; }

# --- adb falso: registra tudo e decide o python ---------------------------
cat > "$WORK/adb" <<'EOS'
#!/usr/bin/env bash
# adb falso que MODELA o Android, em vez de responder o que o teste quer.
#
# O que o Android faz e o que este adb faz:
#   - `adb shell` sem su roda como uid 2000 e NÃO atravessa /data/data/,
#     que é 0700 do app do Termux → o teste de existência ali dá "ausente"
#     com o python instalado. Foi exatamente esse o defeito: o preflight sem su
#     dava falso ausente em todo aparelho real.
#   - `adb shell su -c` roda como root e enxerga o mesmo caminho.
#   - `adb root` SEMPRE falha: é assim que o Android de produção se comporta, e
#     é a razão de o deploy perguntar "tem root?" por `su -c id` e não por ele.
#   - sem root disponível, o su falha e o script tem que dizer "root
#     necessário", que é mensagem diferente de "python ausente".
echo "adb $*" >> "$ADB_LOG"
[ "${1:-}" = "root" ] && {
    echo "adbd cannot run as root in production builds" >&2
    exit 1
}
if [ "${1:-}" = "shell" ]; then
    shift
    if [ "${1:-}" = "su" ]; then
        [ "${FAKE_NO_ROOT:-0}" = "1" ] && { echo "su: not found" >&2; exit 1; }
        # "tem root?" e `su -c id` respondendo uid=0 — o mesmo aparelho que
        # recusa `adb root` tem root aqui, e é esse o que o deploy precisa.
        [ "$*" = "su -c id" ] && {
            echo "uid=0(root) gid=0(root) groups=0(root)"
            exit 0
        }
        # como root: enxerga /data/data/ e responde sobre o python
        case "$*" in
            *"test -x "*python3*) [ "${FAKE_PYTHON_PRESENT:-0}" = "1" ] && exit 0; exit 1 ;;
        esac
        # caminho de ENVIO: exercitado de verdade, com o su -c real
        if grep -q 'push_mod_emit\.py' <<<"$*"; then
            echo "ok: 1234 bytes written"; exit 0
        fi
        echo "ok"; exit 0
    fi
    # sem su: uid 2000 não atravessa /data/data/
    case "$2" in
        "test -x "*python3*)
            if grep -q '/data/data/' <<<"$2"; then
                echo "test: /data/data/com.termux/files/usr/bin/python3: Permission denied" >&2
                exit 1
            fi
            exit 1 ;;
    esac
    echo "ok"; exit 0
fi
case "${1:-}" in
    push) echo "1 file pushed."; exit 0 ;;
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
grep -q 'pkg install python' <<<"$out" ||
    die "a falha nao diz como instalar: $out"
grep -qi 'python' <<<"$out" ||
    die "a falha nao diz QUE dependencia falta: $out"
if grep -q '^adb push' "$ADB_LOG"; then
    die "empurrou algo com o python ausente: $(grep '^adb push' "$ADB_LOG")"
fi
grep -q "su -c test -x" "$ADB_LOG" ||
    die "o preflight nao rodou como root; sem su o uid 2000 nao atravessa /data/data e o check mente"
echo "deploy_python_check: (1) python ausente, com su real -> exit $rc, mensagem acional, nada enviado"

# --- (2) python presente: o preflight deixa passar -------------------------
: > "$ADB_LOG"
out="$(cd "$ROOT/mods/kungfux" && printf 'sim\n' | FAKE_PYTHON_PRESENT=1 \
    TERMUX_PY=/data/data/com.termux/files/usr/bin/python3 ./deploy.sh 2>&1)" && rc=0 || rc=$?
grep -q '^adb push' "$ADB_LOG" ||
    die "com python presente o deploy nao chegou ao push (check barrando o caminho feliz): rc=$rc $out"
# O aparelho deste caso e de producao: adb root falha, su funciona. Se o deploy
# ainda consultasse adb root, ele abortaria aqui — antes do su, que e o caminho.
grep -q '^adb root' "$ADB_LOG" &&
    die "o deploy consultou 'adb root' no aparelho de producao: $(grep '^adb root' "$ADB_LOG")"
echo "deploy_python_check: (2) python presente, adb root indisponivel -> o deploy segue e envia"

# --- (3) o preflight e `su -c`, no codigo — e o comentario nao conta -------
# Conferir o comentario daria azo para apagar a checagem e deixar o texto
# prometendo o que nao existe. O que e lido aqui e a linha que roda.
for mod in kungfux mechabun; do
    f="$ROOT/mods/$mod/deploy.sh"
    # Comportamento primeiro: o caso (1) ja exige no log do adb uma invocacao
    # `shell su -c test -x`, que e o preflight rodando como root de verdade.
    # Aqui e so o contrario que se prova no codigo: nao pode existir preflight
    # sem su, que e o defeito original.
    grep -q 'adb shell "test -x' "$f" &&
        die "mods/$mod/deploy.sh: ha preflight SEM su; o uid 2000 nao atravessa /data/data/com.termux (0700) e o check da falso ausente em todo aparelho real"
    grep -q 'echo "erro: root necessario' "$f" ||
        die "mods/$mod/deploy.sh: sem su nao ha mensagem propria de root, e ela se confunde com 'python ausente'"
    # `adb root` e proibido em CODIGO; no comentario ele aparece o tempo todo,
    # porque e a raza de nao usa-lo. Daí o filtro de comentario: sem ele a
    # prova casaria com o proprio texto que explica a proibicao.
    _sem_comentario=$(sed 's/^[[:space:]]*#.*//' "$f")
    grep -q 'adb root' <<<"$_sem_comentario" &&
        die "mods/$mod/deploy.sh: chama 'adb root' no codigo; em build de producao o adbd recusa ('adbd cannot run as root in production builds') e o deploy aborta sem nunca tentar o su, que e o caminho que funciona"
    grep -q 'adb shell su -c id' "$f" ||
        die "mods/$mod/deploy.sh: a pergunta de root nao e 'su -c id' respondendo uid=0"
    head -20 "$f" | grep 'pkg install python' >/dev/null ||
        die "mods/$mod/deploy.sh: o bloco Requer nao cita 'pkg install python'"
done
echo "deploy_python_check: (3) preflight em su -c, mensagem de root propria, Requer cita o python"

# --- (4) TERMUX_PY com metacaractere e recusado antes de qualquer adb -----
: > "$ADB_LOG"
out="$(cd "$ROOT/mods/kungfux" && TERMUX_PY="/x' ; echo INJETADO ; '" ./deploy.sh </dev/null 2>&1)" && rc=0 || rc=$?
[ "$rc" -ne 0 ] || die "aceitou TERMUX_PY com metacaractere (exit 0): o caminho entra num su -c como root"
grep -q 'su -c' <<<"$out" ||
    die "a recusa do TERMUX_PY nao explica o risco: $out"
[ ! -s "$ADB_LOG" ] || die "validou o caminho DEPOIS de chamar adb: $(cat "$ADB_LOG")"
echo "deploy_python_check: (4) TERMUX_PY com aspa/ponto e virgula recusado antes de qualquer adb"

# --- (5) aparelho sem su: root necessario, e nada enviado ------------------
# `adb root` aqui ja falha (producao), entao o unico caminho de root e o su. Sem
# su, o deploy tem que dizer isso pelo nome, e nao empurrar o .so antes.
: > "$ADB_LOG"
out="$(cd "$ROOT/mods/kungfux" && printf 'sim\n' | FAKE_NO_ROOT=1 FAKE_PYTHON_PRESENT=1 \
    TERMUX_PY=/data/data/com.termux/files/usr/bin/python3 ./deploy.sh 2>&1)" && rc=0 || rc=$?
[ "$rc" -ne 0 ] || die "deploy aceitou rodar sem su nenhum (exit 0)"
grep -q 'root necessario: o aparelho precisa de su (Magisk/KernelSU)' <<<"$out" ||
    die "a falha sem su nao diz o que resolver: $out"
if grep -q '^adb push' "$ADB_LOG"; then
    die "empurrou algo sem root: $(grep '^adb push' "$ADB_LOG")"
fi
echo "deploy_python_check: (5) sem su -> exit $rc, mensagem nomeia Magisk/KernelSU, nada enviado"

echo "deploy_python_check: OK"
