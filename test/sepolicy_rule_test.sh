#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
tmp="$ROOT/.sepolicy-rule-test.$$"
trap 'rm -f "$tmp"' EXIT

cat >"$tmp" <<'EOF'
type bepinex_mod_file
typeattribute x
permissive a b
EOF

if "$ROOT/tools/check_sepolicy_rule.sh" "${tmp#"$ROOT"/}" >/dev/null 2>&1; then
    echo "expected malformed sepolicy rules to fail" >&2
    exit 1
fi

"$ROOT/tools/check_sepolicy_rule.sh" module/sepolicy.rule >/dev/null
echo "sepolicy_rule_test: PASS"
