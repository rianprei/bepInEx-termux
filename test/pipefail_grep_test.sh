#!/usr/bin/env bash
# test/pipefail_grep_test.sh — os dois lados da regra, na forma de comportamento.
#
# Não testa o lint (isso é o próprio lint). Testa as DUAS coisas que o lint
# existe para proteger, e cada uma falha no comportamento antigo:
#
#  1. IDENTIDADE DE SUBSTRING: o nome do pacote do Manager é usado como REGEX
#     quando o -F falta, e o ponto casa com qualquer caractere. Um pacote
#     decoy passa por Manager. Aqui o pm é falso e devolve o decoy; o script
#     tem que recusar.
#
#  2. A CORRIDA DO pipefail: o grep -q sai cedo, o produtor leva SIGPIPE (141),
#     e o pipeline devolve 141 mesmo com o elemento PRESENTE. Aqui medimos
#     as duas formas com uma saída grande e o padrão no começo (que é o que
#     dispara). A forma antiga tem que dar falso-negativo; a nova, nunca.
set -uo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
fails=0
check() { printf '  [%s] %s\n' "$([ "$1" = 0 ] && echo PASS || echo FAIL)" "$2"; fails=$((fails + ($1 != 0))); }

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

# ---------------------------------------------------------------- 1. o decoy
# pm falso: devolve um pacote que CASA com o padrao ^package:com.getermux.x$
# (o ponto casa com qualquer caractere) mas que NAO e o Manager.
mkdir -p "$WORK/bin"
cat > "$WORK/bin/pm" <<'EOS'
#!/usr/bin/env bash
echo "package:comXgetermuxYx"
echo "package:android"
EOS
chmod +x "$WORK/bin/pm"
cat > "$WORK/bin/am" <<'EOS'
#!/usr/bin/env bash
echo "AM-CHAMADO: $*"          # marca que entrou no ramo do Manager
EOS
chmod +x "$WORK/bin/am"

gerar_action_com_forma() {
    # $1: a forma do teste (antiga|correta)
    local forma="$1"
    {
        echo '#!/bin/sh'
        echo 'MODDIR=/tmp; MODS=/tmp; MANAGER=com.getermux.x'
        # SC2016 desligado de proposito: o $MANAGER tem que chegar INTEIRO no
        # arquivo gerado, e nao expandir aqui. E o que torna o teste valido.
        # shellcheck disable=SC2016
        if [ "$forma" = antiga ]; then
            echo 'if pm list packages 2>/dev/null | grep -q "^package:$MANAGER$"; then echo RAMO-MANAGER; fi'
        else
            echo 'if pm list packages 2>/dev/null | grep -Fxq "package:$MANAGER"; then echo RAMO-MANAGER; fi'
        fi
        echo 'echo FIM'
    } > "$WORK/action_$forma.sh"
    chmod +x "$WORK/action_$forma.sh"
}
gerar_action_com_forma antiga
gerar_action_com_forma correta

saida_antiga=$(PATH="$WORK/bin:$PATH" sh "$WORK/action_antiga.sh" 2>/dev/null)
saida_correta=$(PATH="$WORK/bin:$PATH" sh "$WORK/action_correta.sh" 2>/dev/null)

# o comportamento ANTIGO entra no ramo do Manager por causa do decoy
case "$saida_antiga" in
    *RAMO-MANAGER*) check 0 "forma ANTIGA aceita o decoy (o bug existe, o teste tem condicao)" ;;
    *)              check 1 "forma ANTIGA aceita o decoy (se nao aceitou, o bug nao se reproduz e o teste nao prova nada)" ;;
esac
# a forma CORRETA nao pode entrar
case "$saida_correta" in
    *RAMO-MANAGER*) check 1 "forma CORRETA rejeita o decoy" ;;
    *)              check 0 "forma CORRETA rejeita o decoy" ;;
esac
# e o Manager de verdade ainda e reconhecido (a correcao nao pode cegar o caso bom)
cat > "$WORK/bin/pm" <<'EOS'
#!/usr/bin/env bash
echo "package:com.getermux.x"
EOS
saida_certo_pkg=$(PATH="$WORK/bin:$PATH" sh "$WORK/action_correta.sh" 2>/dev/null)
case "$saida_certo_pkg" in
    *RAMO-MANAGER*) check 0 "forma CORRETA ainda reconhece o pacote de verdade" ;;
    *)              check 1 "forma CORRETA ainda reconhece o pacote de verdade" ;;
esac

# ------------------------------------------------------- 2. a corrida do pipe
# O produtor EXTERNO e o que dispara a corrida, e mede-se aqui: com o elemento
# presente, `produtor | grep -q` devolve 141 (grep achou, fechou o pipe, o
# produtor levou SIGPIPE) enquanto a forma materializada devolve 0 sempre.
#   set -o pipefail; seq 1 300000 | grep -q '^1$'; echo $?   ->  141
#
# Por que "seq" e um bom produtor: a primeira linha ja casa com o padrao, e ele
# ainda tem 300 mil linhas para escrever. Com um builtin (printf de uma string
# grande) a corrida NAO se reproduz nesta maquina — e por isso que tanto
# `echo "$x" | grep -q` sobreviveu. O que se exige abaixo e so a garantia da
# forma nova; a da antiga e medida e impressa.
velha_com_race=0
nova_com_race=0
ITER=${ITER:-100}
for _ in $(seq 1 "$ITER"); do
    seq 1 300000 | grep -q '^1$'  || velha_com_race=$((velha_com_race + 1))
    _saida="$(seq 1 300000)"
    grep -q '^1$' <<<"$_saida"    || nova_com_race=$((nova_com_race + 1))
done
# A forma nova e a que PRECISA nunca falhar. A antiga tem que falhar pelo menos
# uma vez, senao este teste nao esta provando a existencia da corrida e so
# medindo a sua ausencia — que e o erro do meu primeiro teste.
if [ "$nova_com_race" -eq 0 ]; then
    check 0 "forma materializada: $ITER execucoes com o elemento presente, $nova_com_race falso-negativo(s)"
else
    check 1 "forma materializada: $nova_com_race falso-negativo(s) em $ITER"
fi
if [ "$velha_com_race" -gt 0 ]; then
    check 0 "forma com pipe: a corrida REPRODUZ ($velha_com_race/$ITER falso-negativos com o elemento presente)"
else
    check 1 "forma com pipe: a corrida nao reproduziu — o teste nao prova nada, reveja o produtor"
fi

# o par exato do lint: o proprio arquivo do lint nao pode se acusar
"$ROOT/tools/pipefail_grep_check.sh" >/dev/null 2>&1
check $? "o lint passa na arvore limpa (nao se acusa)"


# ------------------------------------------------------- 3. DETECCAO DE pipefail
# ACHADO DO KIMI: a primeira versao do detector so casava `set -euo pipefail` e
# `set -o pipefail`, e lia QUALQUER OUTRA FORMA como "sem pipefail" — 32 scripts
# com `set -euo pipefail` e 17 com `set -uo pipefail` nunca eram varridos, e a
# linha de sucesso era FALSA: ela anunciava uma limpeza que nao tinha verificado
# nada. O detalhe que faz `-euo` casar e que essa e UMA flag so, e o `o` e a
# ULTIMA letra dela.
echo "--- (6) deteccao de pipefail: as formas de 'set' ---"
for forma in "set -euo pipefail" "set -uo pipefail" "set -o pipefail" "set -x -o pipefail" "shopt -so pipefail" "set -euo pipefail -x"; do
    D=$(mktemp -d); mkdir -p "$D/tools" "$D/test"
    cp "$ROOT/tools/pipefail_grep_check.sh" "$D/tools/"
    printf '#!/usr/bin/env bash\n%s\nif printf %%s "" "$X" | grep -q Z; then :; fi\n' "$forma" > "$D/test/alvo.sh"
    out=$(cd "$D" && bash tools/pipefail_grep_check.sh 2>&1); rc=$?
    if [ $rc -ne 0 ]; then check 0 "'$forma' e reconhecido como pipefail"
    else check 1 "'$forma' NAO foi reconhecido — o site escapou do lint"; fi
    rm -rf "$D"
done

echo "--- (7) sem pipefail: o site e IGNORADO de proposito ---"
D=$(mktemp -d); mkdir -p "$D/tools" "$D/test"
cp "$ROOT/tools/pipefail_grep_check.sh" "$D/tools/"
printf '#!/usr/bin/env bash\nset -eu\necho x\nif printf %%s "" "$X" | grep -q Z; then :; fi\n' > "$D/test/alvo.sh"
out=$(cd "$D" && bash tools/pipefail_grep_check.sh 2>&1); rc=$?
if [ $rc -eq 0 ]; then check 0 "sem pipefail: o site nao e erro (o 141 nao existe ali)"
else check 1 "sem pipefail foi acusado"; fi
rm -rf "$D"

echo "--- (8) UM sitio conta UM, e nao seis ---"
D=$(mktemp -d); mkdir -p "$D/tools" "$D/test"
cp "$ROOT/tools/pipefail_grep_check.sh" "$D/tools/"
printf '#!/usr/bin/env bash\nset -uo pipefail\nif printf %%s "" "$X" | grep -q Z; then :; fi\n' > "$D/test/alvo.sh"
out=$(cd "$D" && bash tools/pipefail_grep_check.sh 2>&1)
n_linhas=$(printf '%s\n' "$out" | grep -cE '^[^ ]+:[0-9]+: ')
n_dito=$(printf '%s\n' "$out" | grep -oE '[0-9]+ sitio\(s\)' | grep -oE '^[0-9]+')
if [ "$n_linhas" = "1" ] && [ "$n_dito" = "1" ]; then
    check 0 "1 sitio -> 1 linha de erro e '1 sitio(s)' (antes saia 1/6)"
else
    check 1 "contagem errada: $n_linhas linha(s), dicendo '$n_dito'"; fi
rm -rf "$D"

echo "--- (9) a linha de sucesso e VERIFICAVEL de fora ---"
  out=$(bash "$ROOT/tools/pipefail_grep_check.sh" 2>&1 | tail -1)
  varridos=$(printf '%s\n' "$out" | sed -nE 's/.*varridos ([0-9]+)\/([0-9]+) scripts com pipefail.*/\1/p')
  com_pipefail=$(printf '%s\n' "$out" | sed -nE 's/.*varridos ([0-9]+)\/([0-9]+) scripts com pipefail.*/\2/p')
  if [ -n "$varridos" ] && [ -n "$com_pipefail" ] && [ "$varridos" = "$com_pipefail" ] && [ "$varridos" -gt 0 ]; then
      check 0 "o sucesso informa a varredura e ela confere: $varridos/$com_pipefail scripts com pipefail"
  else
      check 1 "o sucesso nao informa uma varredura confiavel: $out"
  fi

echo "=== pipefail_grep_test: $fails falha(s) ==="
[ "$fails" -eq 0 ]
