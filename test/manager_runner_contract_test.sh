#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

mkdir -p "$TMP/project/manager" "$TMP/bin"
cp "$ROOT/manager/run_tests.sh" "$TMP/project/manager/run_tests.sh"
cp "$ROOT/manager/version.sh" "$TMP/project/manager/version.sh"
cp "$ROOT/VERSION" "$TMP/project/VERSION"

cat > "$TMP/bin/javac" <<'EOF'
#!/usr/bin/env sh
exit 0
EOF
cat > "$TMP/bin/java" <<'EOF'
#!/usr/bin/env sh
case "$RUNNER_SCENARIO" in
    pass)
        echo "RUNNER: checks=1813 falhas=0"
        exit 0
        ;;
    exit0_failure)
        echo "[RUNNER FAIL] ConfTest (1)"
        echo "RUNNER: checks=1813 falhas=1"
        exit 0
        ;;
    failures_only)
        echo "RUNNER: checks=1813 falhas=1"
        exit 0
        ;;
    marker_only)
        echo "[RUNNER FAIL] ConfTest (1)"
        echo "RUNNER: checks=1813 falhas=0"
        exit 0
        ;;
    missing_summary)
        echo "[RUNNER PASS] ConfTest"
        exit 0
        ;;
    zero_checks)
        echo "RUNNER: checks=0 falhas=0"
        exit 0
        ;;
esac
EOF
chmod +x "$TMP/bin/javac" "$TMP/bin/java"

run_scenario() {
    local scenario=$1
    RUNNER_SCENARIO="$scenario" PATH="$TMP/bin:$PATH" \
        "$TMP/project/manager/run_tests.sh" >"$TMP/$scenario.log" 2>&1
}

if run_scenario pass; then
    echo "PASS: resumo válido e checks no mínimo aceitos"
else
    cat "$TMP/pass.log" >&2
    echo "FAIL: resumo válido foi recusado" >&2
    exit 1
fi

for scenario in exit0_failure failures_only marker_only missing_summary zero_checks; do
    if run_scenario "$scenario"; then
        cat "$TMP/$scenario.log" >&2
        echo "FAIL: sabotagem $scenario foi aceita" >&2
        exit 1
    fi
done
grep -Fq 'ERRO: TestRunner registrou falha por teste' "$TMP/exit0_failure.log"
grep -Fq 'ERRO: TestRunner reportou 1 falha(s)' "$TMP/failures_only.log"
grep -Fq 'ERRO: TestRunner registrou falha por teste' "$TMP/marker_only.log"
grep -Fq 'ERRO: resumo RUNNER ausente ou duplicado' "$TMP/missing_summary.log"
grep -Fq 'ERRO: TestRunner executou 0 checks; mínimo 1813' "$TMP/zero_checks.log"
echo "PASS: falha registrada, marker, resumo ausente e checks=0 são recusados"
