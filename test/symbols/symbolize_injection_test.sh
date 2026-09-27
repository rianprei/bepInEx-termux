#!/usr/bin/env bash
# test/symbols/symbolize_injection_test.sh — o tombstone é ENTRADA DE OUTRA
# PESSOA e nunca pode virar comando nesta máquina.
#
# ACHADO (revisão do OpenCode em 668cc9f, severidade ALTA): o awk do
# symbolize.sh montava uma linha de comando com o token .so, que vem do
# tombstone, e a executava com `cmd | getline`:
#
#     cmd = "SYMDIR=\"$symdir\" \"$finder\" \"$so\""
#     while ((cmd | getline line) > 0) ...
#
# Um tombstone com
#     y.so";id>/tmp/pwned;"z.so
# virava  SYMDIR=... finder "y.so";id>/tmp/pwned;"z.so"  e executava na máquina
# de quem symboliza. Quemsymboliza recebe tombstone de usuário Android; o
# arquivo é a entrada mais não confiável do projeto inteiro.
#
# O que este teste prova:
#   1. um tombstone com tentativa de injeção NÃO cria arquivo nenhum;
#   2. o símbolo é resolvido (a injeção tem que ser inofensiva, não um crash);
#   3. na dúvida (sem candidato), a resposta é uma RECUSA CLARA, não ruído;
#   4. `--offset` sem valor dá mensagem de erro, e não morte em silêncio;
#   5. variantes do mesmo ataque: $(...), crase, |, &, ponto-e-vírgula, aspas.
set -uo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
WORK=$(mktemp -d)
# shellcheck disable=SC2329  # so via trap
cleanup() { rm -rf "$WORK"; }
trap cleanup EXIT

FAILED=0
ok()  { echo "  [OK] $1"; }
bad() { echo "  [FAIL] $1"; FAILED=1; }

# --- uma biblioteca de símbolos de verdade, para o caso "resolve" -----------
SYMDIR="$WORK/symbols"
mkdir -p "$SYMDIR"
cp "$ROOT/mods/u_patch/libs/arm64-v8a/libu_patch.so" "$WORK/libu_patch.so" 2>/dev/null || {
    # sem o .so de build, gera um símbolo sintético — o teste do ataque não
    # depende do binário, e assim ele roda em worktree limpa.
    printf '\x7fELF\x02\x01\x01\x00' > "$WORK/libu_patch.so"
    head -c 4096 /dev/urandom >> "$WORK/libu_patch.so"
}
# shellcheck source=tools/symbols.sh
# shellcheck disable=SC1091
. "$ROOT/tools/symbols.sh"
BID="$(symbols_add "$WORK/libu_patch.so" "$SYMDIR" "u_patch" 2>/dev/null || true)"
[ -n "$BID" ] || BID="0000000000000000000000000000000000000000"
printf '%s\t%s\t%s\n' "$BID" "u_patch" "$BID/u_patch.so" >> "$SYMDIR/INDEX"

MARKER="$WORK/pwned"
rm -f "$MARKER"

# --- 1-5: variantes de injeção no token .so --------------------------------
# Cada uma põe um comando que, se rodar, deixa um arquivo. O nome do marcador
# vem do próprio payload, entao nao precisa hardcodar caminho nenhum no teste.
i=0
while IFS= read -r payload; do
    i=$((i + 1))
    marker="$WORK/pwned_$i"
    rm -f "$WORK"/pwned_*
    t="$WORK/tomb_$i.txt"
    cat > "$t" <<EOF
*** *** *** *** *** ***
backtrace:
      #04 pc 000000000001bb34  /data/local/tmp/mods/com.x/$payload
EOF
    out="$(bash "$ROOT/tools/symbolize.sh" --symbols "$SYMDIR" "$t" 2>&1 < /dev/null)"
    rc=$?
    criou=0
    for f in "$WORK"/pwned_*; do
        [ -e "$f" ] && criou=1
    done
    if [ "$criou" -eq 0 ]; then
        ok "payload #$i nao criou arquivo (rc=$rc)"
    else
        bad "payload #$i EXECUTOU: criou $(ls "$WORK"/pwned_* 2>/dev/null)"
    fi
    # e a resposta tem que ser uma linha de symbolize (resolvida ou recusada),
    # nunca um trace de shell nem um "command not found"
    if printf '%s' "$out" | grep -qiE "command not found|syntax error|unexpected token|/bin/sh:"; then
        bad "payload #$i vazou erro de shell na saida"
    else
        ok "payload #$i sem ruido de shell na saida"
    fi
    if printf '%s' "$out" | grep -qE "SEM BUILD-ID|NAO VERIFICADO|u_patch|\.cpp:"; then
        ok "payload #$i respondeu (resolvido ou recusado de forma clara)"
    else
        bad "payload #$i nao deu nenhuma resposta aproveitavel"
    fi
done <<'PAYLOADS'
y.so";touch %MARKER%;"z.so
y.so";touch %MARKER%#"
y.so$(touch %MARKER%)x.so
y.so`touch %MARKER%`.so
y.so";touch %MARKER% ;"z.so
y.so"|touch %MARKER% #".so
y.so"&touch %MARKER% #".so
y.so"; : ; touch %MARKER% ;"z.so
PAYLOADS

# O marcador acima e literal: substitui pelo caminho real, para o payload rodar
# de verdade se o bug voltar.
for t in "$WORK"/tomb_*.txt; do
    sed -i "s|%MARKER%|$WORK/pwned_x|g" "$t"
done
rm -f "$WORK"/pwned_*
# Roda DE NOVO com os marcadores substituidos: e aqui que a injecão acontece de
# fato, se ela ainda existir.
i=0
for t in "$WORK"/tomb_*.txt; do
    i=$((i + 1))
    rm -f "$WORK"/pwned_*
    out="$(bash "$ROOT/tools/symbolize.sh" --symbols "$SYMDIR" "$t" 2>&1 < /dev/null)"
    if ls "$WORK"/pwned_* >/dev/null 2>&1; then
        bad "execucao real: payload #$i rodou e criou arquivo"
    else
        ok "execucao real: payload #$i nao rodou nada"
    fi
    if printf '%s' "$out" | grep -qiE "command not found|/bin/sh:"; then
        bad "payload #$i vazou erro de shell"
    else
        ok "payload #$i sem erro de shell"
    fi
done

# --- 6: tombstone INTEIRO malicioso (o payload no campo do nome) -----------
{
    printf 'backtrace:\n'
    printf '      #04 pc 000000000001bb34  /data/local/tmp/mods/%s/u_patch.so (func+24)\n' \
        "$(printf 'x" ; touch %s/pwned_y ; echo "' "$WORK")"
} > "$WORK/tomb_nome.txt"
rm -f "$WORK"/pwned_*
out="$(bash "$ROOT/tools/symbolize.sh" --symbols "$SYMDIR" "$WORK/tomb_nome.txt" 2>&1 < /dev/null)"
if ls "$WORK"/pwned_* >/dev/null 2>&1; then
    bad "payload no NOME do .so executou"
else
    ok "payload no NOME do .so nao executou"
fi

# --- 7: --offset sem valor: mensagem, e nao morte em silencio --------------
for flag in --offset --symbols --build-id; do
    rc=0
    out="$(bash "$ROOT/tools/symbolize.sh" "$flag" 2>&1 < /dev/null)" || rc=$?
    if [ "$rc" -ne 0 ] && printf '%s' "$out" | grep -qi -- "$flag"; then
        ok "$flag sem valor: erro com mensagem (rc=$rc)"
    else
        bad "$flag sem valor: rc=$rc, saida=[$out]"
    fi
done

# --- 8: o awk nao pode mais executar nada ---------------------------------
# Guarda de regressão no FONTE: se alguém reintroduzir `cmd | getline` ou
# system()/eval no awk, isto falha mesmo que o payload acima deixe de casar.
if sed 's/[[:space:]]*#.*$//' "$ROOT/tools/symbolize.sh" \
     | grep -nE '(^|[^a-zA-Z_])(getline|system|eval)[[:space:]]*[( ]' \
     | grep -q .; then
    bad "o awk voltou a executar algo (getline/system/eval fora de comentario)"
else
    ok "o awk nao executa nada (sem getline/system/eval em codigo)"
fi

echo
if [ "$FAILED" -ne 0 ]; then
    echo "symbolize_injection_test: HOUVE FALHAS" >&2
    exit 1
fi
echo "symbolize_injection_test: OK (tombstone tratado como dado, nunca como comando)"
exit 0
