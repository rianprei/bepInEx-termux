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
echo "sepolicy_rule_test: PASS"
