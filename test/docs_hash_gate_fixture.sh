#!/usr/bin/env bash
# test/docs_hash_gate_fixture.sh — exercita test/docs_hash_gate.sh de verdade.
#
# POR QUE ISTO EXISTE. O gate cobre CHANGELOG.md da raiz desde docs-hash-gate-3,
# mas o CHANGELOG do repo não tem nenhuma linha que se declare mesclada com
# hash: sem fixture, a cobertura do CHANGELOG seria só estrutural — o nome
# estaria na lista e nada provaria que uma linha ali é conferida. Aqui cada
# caso roda o gate de verdade, num repositório temporário, e exige PASS/FAIL.
#
# O gate resolve a raiz a partir do próprio caminho (dirname do script /..),
# então o script é copiado para dentro da árvore temporária antes de rodar.
set -uo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
GATE_SRC="$ROOT/test/docs_hash_gate.sh"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

fails=0
cases=0

# repo temporário com dois commits: H0 é ancestral de HEAD, HX está num
# branch lateral e não é — é o "fora da base" que o gate precisa pegar.
TREE="$TMP/tree"
mkdir -p "$TREE/test"
cp "$GATE_SRC" "$TREE/test/docs_hash_gate.sh"
(
    cd "$TREE" || exit 1
    git init -q .
    # NUNCA 'git config' puro aqui: sem -C, um refactor que remova o cd
    # acima escreve no config LOCAL do repo real (foi assim que user.name=t
    # parou no ~/repos/bepInEx-termux). -C prende tudo ao repo temporário.
    git -C "$TREE" config user.email f@t; git -C "$TREE" config user.name f
    git -C "$TREE" config commit.gpgsign false
    echo base > base.txt
    git add -A && git commit -qm base
    git rev-parse HEAD > "$TMP/h0"
    git checkout -q -b lateral
    echo lado > lado.txt
    git add -A && git commit -qm lado
    git rev-parse HEAD > "$TMP/hx"
    git checkout -q -   # volta para o branch principal; HX fica fora da base
)
H0=$(cat "$TMP/h0")
HX=$(cat "$TMP/hx")

# expect <PASS|FAIL> <nome> <arquivo-ou-vazio> <conteudo-da-linha>
expect() {
    local want="$1" nome="$2" arquivo="$3" linha="${4:-}"
    rm -f "$TREE/docs/ROADMAP-UNIVERSAL.md" "$TREE/CHANGELOG.md"
    mkdir -p "$TREE/docs"
    if [ -n "$arquivo" ]; then
        : > "$TREE/$arquivo"
        [ -n "$linha" ] && printf -- '%s\n' "$linha" >> "$TREE/$arquivo"
    fi
    local out rc
    out=$(cd "$TREE" && bash test/docs_hash_gate.sh 2>&1); rc=$?
    cases=$((cases + 1))
    local got=PASS
    [ $rc -ne 0 ] && got=FAIL
    if [ "$got" = "$want" ]; then
        printf '  ok    %-46s %s\n' "$nome" "$got"
    else
        fails=$((fails + 1))
        printf '  FALHA %-46s esperado %s, veio %s\n' "$nome" "$want" "$got"
        printf '%s\n' "$out" | sed 's/^/         /' | head -3
    fi
}

BAD=deadbeefdeadbeefdeadbeefdeadbeefdeadbeef

echo "fixture: docs_hash_gate.sh num repo temporario (H0=$H0, fora da base HX=$HX)"

# --- (D) o CHANGELOG.md da raiz, exercitado de verdade -----------------------
expect PASS "CHANGELOG raiz: linha merged com hash da base" \
    CHANGELOG.md "- [x] 2026-09-28: algo merged em $H0"
expect FAIL "CHANGELOG raiz: linha merged com hash inexistente" \
    CHANGELOG.md "- [x] 2026-09-28: algo merged em $BAD"
expect FAIL "CHANGELOG raiz: linha merged com hash fora da base" \
    CHANGELOG.md "- [x] 2026-09-28: algo merged em $HX"
expect FAIL "ROADMAP: linha merged com hash inexistente" \
    docs/ROADMAP-UNIVERSAL.md "- [x] merged: $BAD"

# --- (A) negacao conta como nao-mergeado ------------------------------------
expect PASS "negacao: unmerged com hash fora da base" \
    docs/ROADMAP-UNIVERSAL.md "- [ ] unmerged: $HX"
expect PASS "negacao: nao merged com hash fora da base" \
    docs/ROADMAP-UNIVERSAL.md "- [ ] nao merged: $HX"
expect PASS "negacao: nao mesclado com hash fora da base" \
    docs/ROADMAP-UNIVERSAL.md "- [ ] nao mesclado: $HX"
expect PASS "negacao: not merged com hash fora da base" \
    docs/ROADMAP-UNIVERSAL.md "- [ ] not merged: $HX"
expect PASS "negacao com acento: nao merged escrito com acento" \
    docs/ROADMAP-UNIVERSAL.md "- [ ] não merged: $HX"
expect FAIL "negacao nao isenta merge de verdade na mesma frase" \
    docs/ROADMAP-UNIVERSAL.md "- [x] nao merged: $BAD; depois merged: $BAD"

# --- (B) caixa [X] maiuscula ------------------------------------------------
expect FAIL "caixa [X] maiuscula com hash ruim" \
    docs/ROADMAP-UNIVERSAL.md "- [X] $BAD"
expect PASS "caixa [X] maiuscula com hash da base" \
    docs/ROADMAP-UNIVERSAL.md "- [X] $H0"
expect FAIL "caixa [x] minuscula com hash ruim (controle)" \
    docs/ROADMAP-UNIVERSAL.md "- [x] $BAD"

# --- buraco (1): as 4 variacoes de 'fora da base' ---------------------------
expect FAIL "'fora da base' exato" \
    docs/ROADMAP-UNIVERSAL.md "- [x] mesclado $BAD — fora da base"
expect FAIL "'FORA DA BASE' caixa alta" \
    docs/ROADMAP-UNIVERSAL.md "- [x] mesclado $BAD — FORA DA BASE"
expect FAIL "'fora-da-base' com hifen" \
    docs/ROADMAP-UNIVERSAL.md "- [x] mesclado $BAD — fora-da-base"
expect FAIL "'fora  da  base' espacos duplos" \
    docs/ROADMAP-UNIVERSAL.md "- [x] mesclado $BAD — fora  da  base"

# --- propriedade central -----------------------------------------------------
expect FAIL "hash que existe mas nao e ancestral" \
    docs/ROADMAP-UNIVERSAL.md "- [x] merged: $HX"
expect PASS "hash da base em linha sem marca de merge" \
    docs/ROADMAP-UNIVERSAL.md "- [ ] trabalho em andamento, ref $HX"
expect FAIL "nenhum documento coberto" ""

echo "fixture: $cases casos, $fails falha(s)"
[ "$fails" -eq 0 ]
