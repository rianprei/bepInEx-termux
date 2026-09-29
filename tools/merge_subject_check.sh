#!/usr/bin/env bash
# tools/merge_subject_check.sh — a forma do merge conta, e e conferida.
#
# POR QUE ISTE CHECK EXISTE
#
# Um merge de primeiraparent feito com `git commit-tree` a partir da arvore de
# uma branch, em vez de `git merge --no-ff`, produz um commit com UM PAI SO e a
# arvore da branch. Ele parece um merge na arvore de trabalho e nao aparece como
# merge no historico. Aconteceu na pratica: 72e0dd1 carregava a arvore de
# 016640a sem trazer 6f2b689, e o CHANGELOG afirmava que 6f2b689 estava
# mesclado. TODO O GATE PASSOU — inclusive o check de release-notes, porque
# `git log --merges` so enxerga commit com 2+ pais.
#
# As TRES formas que este check cobra:
#
#  1. ASSUNTO. Todo merge de primeiraparent a partir de MERGE_FLOOR tem que
#     começar com "merge: uni/<branch> — <resumo>". O em-dash importa: e o que
#     separa o nome da branch do assunto, e e o que a serie ja usa.
#
#  2. DOIS PAIS. Todo merge de primeiraparent tem que ter 2 pais. Um commit de
#     primeiraparent com 1 pai cuja arvore seja igual a de uma ponta de branch
#     e merge disfrazado: e o caso 72e0dd1.
#
#  3. CITACAO HONESTA. Todo hash citado numa linha de merge da secao v0.5.0 do
#     CHANGELOG tem que ser um objeto commit E ancestral de HEAD. Um hash que
#     existe mas nao esta na base e a mesma mentira do item 2, so que escrita
#     em vez de cometida. Build id (338b0601 e amigo) NAO e commit e e
#     ignorado de proposito — por isso o `cat-file -t` vem antes de qualquer
#     juizo sobre o hash.
# shellcheck disable=SC2016  # crase literal em padrao de grep e em
# printf: e o que queremos, nao ha expressao para expandir.
set -uo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
CHANGELOG="$ROOT/CHANGELOG.md"

# Piso do historico: merges anteriores a este nao sao julgados. Cada um que
# estiver fora do padrao entra aqui, nominalmente, com o motivo.
MERGE_FLOOR="791966d"

# Commits de primeiraparent com 1 pai que carregam a arvore de uma branch e que
# ficam como excecao, com o motivo. 72e0dd1 e o caso real: commit-tree com a
# arvore de 016640a, ja CORRIGIDO por 5bc45a4 (que e o merge de verdade logo
# acima). Ele fica listado porque o historico publicado o contem, e apagar do
# meio do historico exigiria reescrita.
UNPAIRED_ALLOWED="72e0dd1"

# Autor. Todo commit de primeiraparent apos MERGE_FLOOR tem que ter o MESMO autor
# ESPERADO (o dono do repo, escrito na constante mais abaixo), salvo excecao
# declarada. Nao e o autor do PISO: o piso de um floor anterior era justamente o
# commit corrompido, e deduzir o esperado dele invertia a regra. 791966d (merge de uni/config-leak) saiu
# com autor "f <f@t>": e o config local do repo tinha user.name=f / user.email=f@t
# porque a fixture do hash-gate escreveu nele — worktrees compartilham .git/config,
# entao o estrago alcança o repo principal mesmo com a fixture num worktree. Nao da
# para reescrever o que ja foi publicado, entao ele fica listado.
AUTHOR_ALLOWED="791966d"

# O AUTOR ESPERADO E CONSTANTE, E NAO DEDUZIDO DO PISO. A primeira versao desta
# regra tirava o autor esperado do proprio MERGE_FLOOR — e o piso era o 791966d,
# cujo autor e "f <f@t>". Resultado medido: com o piso assim, um merge LEGITIMO
# do rianprei era RECUSADO (exit 1, "autor rianprei <...> diferente do autor do
# piso (f <f@t>)"), enquanto o commit corrompido passava, por estar na excecao.
# A regra ficava exatamente invertida: aceitava o errado e recusava o certo.
# A razao de fundo e a mesma que justifica a excecao: nao se pode usar como
# referencia de confianca um commit que existe justamente porque a confianca
# nele falhou. O autor esperado e a identidade do dono do repo, escrita aqui.
ESPERADO_AUTHOR="rianprei <lucaguerian@gmail.com>"

errors=()

cd "$ROOT" || exit 1

# --- (1) e (2): assunto e dois pais, commit a commit -------------------------
# O formato usa "%H|%P|%s" com pipe como separador e NAO `read a b c`: o %P de
# um merge tem DOIS hashes separados por espaco, e o `read` com tres variaveis
# comia o segundo pai no campo do meio — todo merge de 2 pais contava como 1.
# Foi o teste deste repositorio que pegou, nao eu.
while IFS='|' read -r sha parents subject; do
    [ -n "$sha" ] || continue
    git merge-base --is-ancestor "$MERGE_FLOOR" "$sha" 2>/dev/null || continue

    # shellcheck disable=SC2086  # o word splitting aqui E a contagem
    # --- autor: o mesmo do piso, salvo excecao declarada ---------------------
    autor=$(git log -1 --format='%an <%ae>' "$sha")
    if [ -n "$ESPERADO_AUTHOR" ] && [ "$autor" != "$ESPERADO_AUTHOR" ]; then
        case " $AUTHOR_ALLOWED " in
            *" $sha "*)
                echo "merge-subject-lint: excecao declarada: $sha autor=$autor (esperado $ESPERADO_AUTHOR)" ;;
            *)
                errors+=("$sha: autor \"$autor\" diferente do autor esperado (dono do repo) ($ESPERADO_AUTHOR). Merge com autor de fila e sempre config local sujo: rode 'git config --local --unset user.name user.email' antes de commitar, ou commite com --author explicito.")
                ;;
        esac
    fi

    # shellcheck disable=SC2086  # o word splitting aqui E a contagem
    set -- $parents
    n_pais=$#
    if [ "$n_pais" -ge 2 ]; then
        case "$subject" in
            "merge: uni/"*" — "*)
                ;;
            *)
                errors+=("$sha: assunto de merge fora do padrao: \"$subject\"
      esperado: \"merge: uni/<branch> — <resumo>\" (o em-dash separa branch de assunto)")
                ;;
        esac
    else
        # primeiraparent com 1 pai. So e merger-wrapper se a arvore for igual a
        # de alguma ponta de branch; um commit normal de primeiraparent (uma
        # correcao, um docs) tem a arvore dele e nao e problema.
        case " $UNPAIRED_ALLOWED " in
            *" $sha "*)
                echo "merge-subject-lint: excecao declarada: $sha ($subject) — commit de 1 pai com arvore de branch, ja corrigido por $MERGE_FLOOR"
                ;;
            *)
                is_branch_tree=0
                for ref in $(git for-each-ref --format='%(objectname)' refs/heads refs/remotes); do
                    if [ "$(git rev-parse "$sha^{tree}")" = "$(git rev-parse "$ref^{tree}")" ]; then
                        is_branch_tree=1
                        break
                    fi
                done
                if [ "$is_branch_tree" -eq 1 ]; then
                    errors+=("$sha: commit de primeiraparent com 1 pai e arvore identica a de uma ponta de branch — merge disfrazado (foi assim que 72e0dd1 enganou todo o gate). Use 'git merge --no-ff'")
                fi
                ;;
        esac
    fi
done < <(git log --first-parent --format='%H|%P|%s' "$MERGE_FLOOR..HEAD")

# --- (3): todo hash de commit citado na v0.5.0 e ancestral de HEAD -----------
# A secao v0.5.0 e a que o release_notes_check mantem; as linhas de merge dela
# comecam com "- `<hash>` —". Hash que nao e objeto commit (build id) fica de
# fora por construcao, e nao por lista.
if [ -f "$CHANGELOG" ]; then
    while IFS= read -r linha; do
        hash=$(printf '%s' "$linha" | grep -oE '^- `[0-9a-f]{7,40}`' | grep -oE '[0-9a-f]{7,40}' | head -1)
        [ -n "$hash" ] || continue
        tipo=$(git cat-file -t "$hash" 2>/dev/null || echo ausente)
        [ "$tipo" = "commit" ] || continue          # build id, ou hash que nem existe
        git merge-base --is-ancestor "$hash" HEAD 2>/dev/null || \
            errors+=("CHANGELOG.md: linha de merge da v0.5.0 cita \`$hash\`, que E um commit mas NAO e ancestral de HEAD. Ou a linha e de um merge que nao entrou, ou o hash e de antes de um rebase.")
done < <(sed -n '/^## v0\.5\.0/,/^## v/p' "$CHANGELOG" | grep -E '^- `[0-9a-f]{7,40}`')
fi

if [ "${#errors[@]}" -gt 0 ]; then
    printf '%s\n' "${errors[@]}" >&2
    printf 'merge-subject-lint: %d problema(s)\n' "${#errors[@]}" >&2
    exit 1
fi
echo "merge-subject-lint: assunto, dois pais e citacao de hash conferidos apos $MERGE_FLOOR"
