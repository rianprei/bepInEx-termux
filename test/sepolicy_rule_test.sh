#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
tmp="$ROOT/.sepolicy-rule-test.$$"
trap 'rm -f "$tmp"' EXIT

bad_rules=(
    "type x"
    "typeattribute x"
    "permissive a b"
    "enforce"
    "type_transition a b c"
    "type_member a b c"
    "genfscon a b"
    "allow a b c"
)
good_rules=(
    "type bepinex_mod_file file_type"
    "typeattribute bepinex_mod_file file_type"
    "permissive bepinex_mod_file"
    "enforce bepinex_mod_file"
    "type_transition a b c d"
    "type_member a b c d"
    "genfscon a b c"
    "allow a b c d"
)

for rule in "${bad_rules[@]}"; do
    printf '%s\n' "$rule" >"$tmp"
    if "$ROOT/tools/check_sepolicy_rule.sh" "${tmp#"$ROOT"/}" >/dev/null 2>&1; then
        echo "expected malformed rule to fail: $rule" >&2
        exit 1
    fi
done

for rule in "${good_rules[@]}"; do
    printf '%s\n' "$rule" >"$tmp"
    if ! "$ROOT/tools/check_sepolicy_rule.sh" "${tmp#"$ROOT"/}" >/dev/null; then
        echo "expected valid rule to pass: $rule" >&2
        exit 1
    fi
done

"$ROOT/tools/check_sepolicy_rule.sh" module/sepolicy.rule >/dev/null

# ---------------------------------------------------------------------------
# As 4 regras, e so elas (revisao de seguranca do freebuff).
#
# Ate a revisao este arquivo tinha 9 regras. Seisexistiam SO para o jogo abrir
# caminho em /data/local/tmp — caminho que nao existe mais. O teste de gramatica
# acima nao pegaria a volta de nenhuma delas, entao aqui as 4 sao fixadas pelo
# CONTEUDO, nao so pela forma.
# ---------------------------------------------------------------------------
RULE=module/sepolicy.rule

# 1) o tipo proprio (rotula a arvore root em /data/adb)
grep -qE '^type[[:space:]]+bepinex_mod_file[[:space:]]+file_type$' "$RULE" || {
    echo "sepolicy: falta 'type bepinex_mod_file file_type'" >&2; exit 1; }

# 2) o jogo mapeia o .so — e SO isso: sem getattr/open/dir, porque nao ha mais
#    acesso por caminho (o FD vem aberto pelo companion).
grep -qE '^allow appdomain bepinex_mod_file file \{ map read execute \}$' "$RULE" || {
    echo "sepolicy: falta 'allow appdomain bepinex_mod_file file { map read execute }'" >&2
    exit 1; }
if grep -qE '^allow appdomain bepinex_mod_file (file|dir) .*(getattr|open|search)' "$RULE"; then
    echo "sepolicy: sobrou acesso de CAMINHO para o jogo (getattr/open/search) — nao deve existir" >&2
    exit 1
fi

# 3) e 4) as duas permcoes de escrita de codigo
grep -qE '^allow appdomain appdomain process execmem$' "$RULE" || {
    echo "sepolicy: falta 'allow appdomain appdomain process execmem'" >&2; exit 1; }
grep -qE '^allow appdomain apk_data_file file execmod$' "$RULE" || {
    echo "sepolicy: falta 'allow appdomain apk_data_file file execmod'" >&2; exit 1; }

# 5) NADA de shell_data_file para o jogo: essa arvore era o caminho velho, e o
#    jogo nao tem mais por que tocar nela.
if grep -qE '^allow .*appdomain.*shell_data_file' "$RULE"; then
    echo "sepolicy: sobrou regra de shell_data_file para appdomain" >&2; exit 1
fi

# 6) a contagem fecha em 4
n=$(grep -cE '^(allow|type) ' "$RULE")
[ "$n" -eq 4 ] || {
    echo "sepolicy: $n regras; a revisao fixou 4 (1 type + 3 allow)" >&2; exit 1; }

echo "sepolicy_rule_test: PASS (gramatica + as 4 regras da revisao)"
