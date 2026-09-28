#!/usr/bin/env bash
# test/merge_subject_check_test.sh — as três regras do merge-subject-lint, cada
# uma provando que PEGA num caso construído e PASSA no caso legítimo.
#
# Este arquivo existe porque um lint que ninguém viu falhar é opinião sobre o
# próprio lint. Cada cenário monta um repositório temporário, faz o merge errado
# de propósito, e exige exit 1 com a mensagem certa. O cenário final exige exit
# 0, para o lint não ser um alarme queRejecta tudo.
# shellcheck disable=SC2016  # crase literal em padrao de grep e em
# printf: e o que queremos, nao ha expressao para expandir.
set -uo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
CHECK="$ROOT/tools/merge_subject_check.sh"
fails=0
ok()   { echo "  [PASS] $1"; }
bad()  { echo "  [FAIL] $1"; fails=$((fails + 1)); }

# novo_repo <dir> — repo vazio com amerge e o CHANGELOG mínimos
novo_repo() {
    local d="$1"
    mkdir -p "$d/tools"
    cp "$CHECK" "$d/tools/"
    git -C "$d" init -q .
    git -C "$d" config user.email s@t
    git -C "$d" config user.name s
    git -C "$d" config commit.gpgsign false
    printf '## v0.5.0\n' > "$d/CHANGELOG.md"
    printf 'base\n' > "$d/f.txt"
    git -C "$d" add -A
    git -C "$d" commit -qm base
}

# um merge legítimo, que vira o piso do lint
merge_legit() {
    local d="$1" branch="$2"
    git -C "$d" checkout -q -b "$branch"
    printf 'algo\n' > "$d/x.txt"
    git -C "$d" add -A
    git -C "$d" commit -qm "$branch: trabalho"
    git -C "$d" checkout -q master
    local m; m="$d/.msg"
    printf 'merge: uni/%s — resumo legitimo\n\nCo-Authored-By: OpenCode <noreply@opencode.ai>\n' "$branch" > "$m"
    git -C "$d" merge --no-ff "$branch" -q -F "$m"
    rm -f "$m"
    git -C "$d" rev-parse HEAD
}

aponta_piso() { sed -i "s|^MERGE_FLOOR=\".*\"|MERGE_FLOOR=\"$1\"|" "$1/tools/merge_subject_check.sh" 2>/dev/null || true; }

# ---------------------------------------------------------------- cenário 1
# ASSUNTO fora do padrao. Foi o meu erro real: um merge com assunto "tmp".
echo "--- (1) assunto fora do padrao ---"
D=$(mktemp -d); novo_repo "$D"
FLOOR=$(merge_legit "$D" branch-boa)
sed -i "s|^MERGE_FLOOR=\".*\"|MERGE_FLOOR=\"$FLOOR\"|" "$D/tools/merge_subject_check.sh"
sed -i "s|^UNPAIRED_ALLOWED=.*|UNPAIRED_ALLOWED=\"\"|" "$D/tools/merge_subject_check.sh"
git -C "$D" checkout -q -b branch-ruim
printf 'y\n' > "$D/y.txt"; git -C "$D" add -A; git -C "$D" commit -qm "branch-ruim: trabalho"
git -C "$D" checkout -q master
git -C "$D" merge --no-ff branch-ruim -q -m "tmp"
out=$(cd "$D" && bash tools/merge_subject_check.sh 2>&1); rc=$?
if [ $rc -ne 0 ] && printf '%s' "$out" | grep -q "assunto de merge fora do padrao"; then
    ok "assunto 'tmp' é recusado, com a mensagem do padrão"
else
    bad "assunto 'tmp' NÃO foi recusado (exit=$rc)"; printf '%s\n' "$out" | head -3 | sed 's/^/         /'
fi
rm -rf "$D"

# ---------------------------------------------------------------- cenário 2
# DOIS PAIS: commit de first-parent com 1 pai cuja árvore = ponta de branch.
# É o 72e0dd1 exato: commit-tree com a árvore da branch, sem trazer a branch.
echo "--- (2) merge de 1 pai com árvore de branch ---"
D=$(mktemp -d); novo_repo "$D"
FLOOR=$(merge_legit "$D" branch-x)
sed -i "s|^MERGE_FLOOR=\".*\"|MERGE_FLOOR=\"$FLOOR\"|" "$D/tools/merge_subject_check.sh"
sed -i "s|^UNPAIRED_ALLOWED=.*|UNPAIRED_ALLOWED=\"\"|" "$D/tools/merge_subject_check.sh"
TREE=$(git -C "$D" rev-parse "branch-x^{tree}")
# o mesmo truque do 72e0dd1: commit com 1 pai, árvore da branch, assunto de merge
FAKE=$(git -C "$D" commit-tree "$TREE" -p "$FLOOR" -F - <<'EOM'
merge: uni/branch-x —_merge de 1 pai com arvore de branch

Co-Authored-By: OpenCode <noreply@opencode.ai>
EOM
)
git -C "$D" update-ref refs/heads/master "$FAKE"
out=$(cd "$D" && bash tools/merge_subject_check.sh 2>&1); rc=$?
if [ $rc -ne 0 ] && printf '%s' "$out" | grep -q "arvore identica a de uma ponta de branch"; then
    ok "commit de 1 pai com árvore de branch é recusado (o caso 72e0dd1)"
else
    bad "commit de 1 pai com árvore de branch NÃO foi recusado (exit=$rc)"; printf '%s\n' "$out" | head -3 | sed 's/^/         /'
fi
rm -rf "$D"

# ---------------------------------------------------------------- cenário 3
# CITAÇÃO: a linha da v0.5.0 cita um commit que existe mas NÃO é ancestral.
echo "--- (3) linha da v0.5.0 citando commit fora da base ---"
D=$(mktemp -d); novo_repo "$D"
FLOOR=$(merge_legit "$D" branch-ok)
sed -i "s|^MERGE_FLOOR=\".*\"|MERGE_FLOOR=\"$FLOOR\"|" "$D/tools/merge_subject_check.sh"
sed -i "s|^UNPAIRED_ALLOWED=.*|UNPAIRED_ALLOWED=\"\"|" "$D/tools/merge_subject_check.sh"
git -C "$D" checkout -q -b branch-perdida
printf 'z\n' > "$D/z.txt"; git -C "$D" add -A; git -C "$D" commit -qm "branch-perdida: trabalho"
PERDIDA=$(git -C "$D" rev-parse branch-perdida)
git -C "$D" checkout -q master
# a branch some da base, mas a linha do CHANGELOG continua citando o hash dela
printf '\n- `%s` — uni/branch-perdida — linha que sobrou de um rebase\n' "$PERDIDA" >> "$D/CHANGELOG.md"
out=$(cd "$D" && bash tools/merge_subject_check.sh 2>&1); rc=$?
if [ $rc -ne 0 ] && printf '%s' "$out" | grep -q "NAO e ancestral de HEAD"; then
    ok "linha da v0.5.0 citando commit fora da base é recusada"
else
    bad "citação fora da base NÃO foi recusada (exit=$rc)"; printf '%s\n' "$out" | head -3 | sed 's/^/         /'
fi
rm -rf "$D"

# ---------------------------------------------------------------- cenário 4
# BUILD ID não pode ser acusado: 338b0601 e identicador de build do jogo, não
# é objeto commit, e tem de passar ignorado.
echo "--- (4) build id na linha não é falso positivo ---"
D=$(mktemp -d); novo_repo "$D"
FLOOR=$(merge_legit "$D" branch-ok)
sed -i "s|^MERGE_FLOOR=\".*\"|MERGE_FLOOR=\"$FLOOR\"|" "$D/tools/merge_subject_check.sh"
sed -i "s|^UNPAIRED_ALLOWED=.*|UNPAIRED_ALLOWED=\"\"|" "$D/tools/merge_subject_check.sh"
printf '\n- `338b0601` — build do jogo usado nos offsets, nao e commit\n' >> "$D/CHANGELOG.md"
out=$(cd "$D" && bash tools/merge_subject_check.sh 2>&1); rc=$?
if [ $rc -eq 0 ]; then
    ok "build id (não é objeto commit) é ignorado, sem falso positivo"
else
    bad "build id foi acusado"; printf '%s\n' "$out" | head -3 | sed 's/^/         /'
fi
rm -rf "$D"

# ---------------------------------------------------------------- cenário 5
# O happy path: histórico legítimo passa. Sem isto o lint é um alarme.
echo "--- (5) histórico legítimo passa ---"
D=$(mktemp -d); novo_repo "$D"
FLOOR=$(merge_legit "$D" branch-limpa)
sed -i "s|^MERGE_FLOOR=\".*\"|MERGE_FLOOR=\"$FLOOR\"|" "$D/tools/merge_subject_check.sh"
sed -i "s|^UNPAIRED_ALLOWED=.*|UNPAIRED_ALLOWED=\"\"|" "$D/tools/merge_subject_check.sh"
out=$(cd "$D" && bash tools/merge_subject_check.sh 2>&1); rc=$?
if [ $rc -eq 0 ]; then
    ok "histórico legítimo passa (o lint não rejeita tudo)"
else
    bad "histórico legítimo foi acusado"; printf '%s\n' "$out" | head -3 | sed 's/^/         /'
fi
rm -rf "$D"

# ---------------------------------------------------------------- cenário 6
# A exceção declarada funciona: o 72e0dd1 é listado e não reprova.
echo "--- (6) exceção declarada de 1 pai ---"
D=$(mktemp -d); novo_repo "$D"
FLOOR=$(merge_legit "$D" branch-exc)
TREE=$(git -C "$D" rev-parse "branch-exc^{tree}")
FAKE=$(git -C "$D" commit-tree "$TREE" -p "$FLOOR" -F - <<'EOM'
merge: uni/branch-exc — merge de 1 pai, declarado excecao

Co-Authored-By: OpenCode <noreply@opencode.ai>
EOM
)
git -C "$D" update-ref refs/heads/master "$FAKE"
sed -i "s|^MERGE_FLOOR=\".*\"|MERGE_FLOOR=\"$FLOOR\"|" "$D/tools/merge_subject_check.sh"
sed -i "s|^UNPAIRED_ALLOWED=.*|UNPAIRED_ALLOWED=\"$FAKE\"|" "$D/tools/merge_subject_check.sh"
out=$(cd "$D" && bash tools/merge_subject_check.sh 2>&1); rc=$?
if [ $rc -eq 0 ] && printf '%s' "$out" | grep -q "excecao declarada"; then
    ok "exceção declarada: o commit de 1 pai passa e sai listado"
else
    bad "exceção declarada não funcionou (exit=$rc)"; printf '%s\n' "$out" | head -3 | sed 's/^/         /'
fi
rm -rf "$D"

echo "=== merge_subject_check_test: $fails falha(s) ==="
[ "$fails" -eq 0 ]
