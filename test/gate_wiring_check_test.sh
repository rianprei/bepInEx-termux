#!/usr/bin/env bash
# test/gate_wiring_check_test.sh — o check da ligação também pode errar, e o
# jeito mais caro de errar é pela MENSAGEM.
#
# O QUE ESTE TESTE PROVA. Um lint que reprova um arquivo e imprime um caminho
# que NÃO EXISTE é pior do que um lint calado: o autor lê o caminho, procura o
# arquivo, não acha, e conclui que o lint mentiu — ou corrige o lint errado. Foi
# exatamente o defeito que a revisão pegou: a mensagem dizia "test/test/x.sh"
# (prefixo dobrado) para um arquivo que vive em "test/x.sh".
#
# O lint e' copiado FORA da raiz de brincadeira de proposito: na raiz de verdade
# ele mora em test/ e esta no manifesto, e dentro da arvore de teste ele seria
# mais um orfao — e a medicao passaria a ser "quantos orfaos a mensagem cita",
# que nao e a coisa.
#
# POR QUE UMA RAIZ DE BRINCADEIRA, E NÃO O REPO. O teste copia só o lint e
# monta uma raiz mínima com um teste ligado, um órfão e um manifesto. Se
# dependesse do inventário do repositório, ele quebraria no dia em que o
# repositório mudasse de testes — e um teste do lint que quebra por mudança de
# inventário é um teste que ninguém vai atualizar.
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

die() { echo "gate_wiring_check_test: $*" >&2; exit 1; }
ok() { echo "gate_wiring_check_test: $*"; }

NEW="$WORK/root"
mkdir -p "$NEW/test/fuzz" "$NEW/tools"
cp "$ROOT/test/gate_wiring_check.py" "$WORK/lint_fora_da_arvore.py"
printf '#!/usr/bin/env bash\necho ligado\n' > "$NEW/test/wired_exemplo.sh"
printf '#!/usr/bin/env bash\necho orfao\n' > "$NEW/test/orfao_novo.sh"
printf '#!/usr/bin/env python3\nprint("fuzz")\n' > "$NEW/test/fuzz/check_de_exemplo.py"
# verify_all.sh sintético: o lint só exige que exista e cite o nome de cada
# `wired`. Os DOIS testes ligados são citados; sobra UM órfão, para a mensagem
# ter um alvo único (com dois órfãos, o teste pegaria o primeiro em ordem
# alfabética e mediria a coisa errada — foi o que aconteceu na 1ª versão).
# O verify_all de brincadeira tem o bloco do manifesto e NENHUM run_step antes
# dele: e o que o lint de ordem exige, e a fixture nao pode mentir sobre isso.
printf '#!/usr/bin/env bash\n# --- O manifesto de testes do gate ---\n# test/wired_exemplo.sh\n# check_de_exemplo.py\n' \
    > "$NEW/tools/verify_all.sh"
printf 'test/wired_exemplo.sh\twired\ntest/fuzz/check_de_exemplo.py\twired\n' \
    > "$NEW/tools/gate_tests.list"

run_lint() {
    python3 "$WORK/lint_fora_da_arvore.py" "$NEW" 2>&1
}

# --- (1) teste no disco sem linha: FAIL, e a mensagem cita um caminho REAL ---
out="$(run_lint)" && rc=0 || rc=$?
[ "$rc" -ne 0 ] || die "o lint aceitou um teste no disco sem linha no manifesto"
cited="$(printf '%s\n' "$out" | sed -n 's/^\([^:]*\): arquivo de teste no disco.*/\1/p' | head -1)"
[ -n "$cited" ] || die "a reprovação não nomeou o arquivo: $out"
# A PROVA: o caminho da mensagem existe na raiz que o lint inspecionou.
[ -e "$NEW/$cited" ] || die "a mensagem cita '$cited', que não existe em $NEW (prefixo dobrado?)"
[ "$cited" = "test/orfao_novo.sh" ] ||
    die "a mensagem cita '$cited' em vez do caminho real do órfão"
ok "(1) órfão reprovado, e a mensagem cita '$cited', que existe"

# --- (2) o mesmo lint, com a árvore limpa: PASS ------------------------------
printf 'test/wired_exemplo.sh\twired\ntest/fuzz/check_de_exemplo.py\twired\ntest/orfao_novo.sh\trun\n' \
    > "$NEW/tools/gate_tests.list"
out="$(run_lint)" && rc=0 || rc=$?
[ "$rc" -eq 0 ] || die "árvore limpa reprovada: $out"
printf '%s\n' "$out" | grep -q 'gate-wiring:' ||
    die "o lint não impressions o resumo: $out"
ok "(2) árvore limpa: PASS, com resumo"

# --- (3) `wired` sem prova é FAIL (é skip com nome mais bonito) --------------
printf '#!/usr/bin/env bash\n# --- O manifesto de testes do gate ---\n' > "$NEW/tools/verify_all.sh"
printf 'test/wired_exemplo.sh\twired\ntest/fuzz/check_de_exemplo.py\twired\ntest/orfao_novo.sh\trun\n' \
    > "$NEW/tools/gate_tests.list"
out="$(run_lint)" && rc=0 || rc=$?
[ "$rc" -ne 0 ] || die "wired sem prova foi aceito"
printf '%s\n' "$out" | grep -q 'declarado wired' ||
    die "a reprovação não diz que é falta de prova: $out"
ok "(3) wired sem prova: FAIL com a mensagem certa"

# --- (4) exceção sem motivo é FAIL ------------------------------------------
printf '#!/usr/bin/env bash\n# --- O manifesto de testes do gate ---\n# test/wired_exemplo.sh\n# check_de_exemplo.py\n' \
    > "$NEW/tools/verify_all.sh"
printf 'test/wired_exemplo.sh\twired\ntest/orfao_novo.sh\trun\ntest/fuzz/check_de_exemplo.py\tskip\tnao roda\n' \
    > "$NEW/tools/gate_tests.list"
out="$(run_lint)" && rc=0 || rc=$?
[ "$rc" -ne 0 ] || die "exceção sem motivo foi aceita"
printf '%s\n' "$out" | grep -q 'sem motivo' ||
    die "a reprovação não diz que o motivo falta: $out"
ok "(4) exceção sem motivo: FAIL"

# --- (5) caminho do manifesto que sumiu é FAIL (destino de arquivo apagado) --
printf '#!/usr/bin/env bash\n# --- O manifesto de testes do gate ---\n# test/wired_exemplo.sh\n# check_de_exemplo.py\n' \
    > "$NEW/tools/verify_all.sh"
printf 'test/wired_exemplo.sh\twired\ntest/orfao_novo.sh\trun\ntest/fuzz/check_de_exemplo.py\twired\ntest/nao_existe.sh\trun\n' \
    > "$NEW/tools/gate_tests.list"
out="$(run_lint)" && rc=0 || rc=$?
[ "$rc" -ne 0 ] || die "destino de arquivo apagado foi aceito"
printf '%s\n' "$out" | grep -q 'nao existe no disco' ||
    die "a reprovação não diz que o arquivo sumiu: $out"
ok "(5) caminho do manifesto que sumiu: FAIL"

# --- (6) o manifesto lido DEPOIS da primeira etapa: FAIL ---------------------
# A promessa "o check da ligação roda antes de tudo" é do texto do manifesto, do
# docstring do check e do bloco no verify_all. Sem verificação, ela já foi
# mentira uma vez (o bloco vivia na linha ~455, depois de dezenas de etapas).
printf '#!/usr/bin/env bash\nrun_step "primeira etapa" x y\n# --- O manifesto de testes do gate ---\n# test/wired_exemplo.sh\n# check_de_exemplo.py\n' \
    > "$NEW/tools/verify_all.sh"
out="$(run_lint)" && rc=0 || rc=$?
[ "$rc" -ne 0 ] || die "manifesto depois da primeira etapa foi aceito"
printf '%s\n' "$out" | grep -q 'DEPOIS da primeira etapa' ||
    die "a reprovação não diz que o bloco veio tarde: $out"
ok "(6) manifesto depois da primeira etapa: FAIL"

echo "gate_wiring_check_test: OK"
