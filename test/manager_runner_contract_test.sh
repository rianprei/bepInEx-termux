#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

mkdir -p "$TMP/project/manager" "$TMP/bin"
cp "$ROOT/manager/run_tests.sh" "$TMP/project/manager/run_tests.sh"
cp "$ROOT/manager/version.sh" "$TMP/project/manager/version.sh"
cp "$ROOT/VERSION" "$TMP/project/VERSION"
cp "$ROOT/manager/test_checks_baseline" "$TMP/project/manager/test_checks_baseline"

# O java falso le o baseline real do repo: o contrato compara por
# IGUALDADE, entao a fixture nao pode furar com um numero inventado.
BASE="$(grep -Eo '[0-9]+' "$ROOT/manager/test_checks_baseline" | head -1)"
export BASE  # o java falso roda em outro processo e le o baseline daqui

cat > "$TMP/bin/javac" <<'EOF'
#!/usr/bin/env sh
exit 0
EOF
cat > "$TMP/bin/java" <<'EOF'
#!/usr/bin/env sh
case "$RUNNER_SCENARIO" in
    pass)
        echo "RUNNER: checks=$BASE falhas=0"
        exit 0
        ;;
    exit0_failure)
        echo "[RUNNER FAIL] ConfTest (1)"
        echo "RUNNER: checks=$BASE falhas=1"
        exit 0
        ;;
    failures_only)
        echo "RUNNER: checks=$BASE falhas=1"
        exit 0
        ;;
    marker_only)
        echo "[RUNNER FAIL] ConfTest (1)"
        echo "RUNNER: checks=$BASE falhas=0"
        exit 0
        ;;
    missing_summary)
        echo "[RUNNER PASS] ConfTest"
        exit 0
        ;;
    zero_checks)
        echo "RUNNER: checks=0 falhas=0"
        ;;
    drift_less)
        echo "RUNNER: checks=$((BASE - 1)) falhas=0"
        ;;
    drift_more)
        echo "RUNNER: checks=$((BASE + 1)) falhas=0"
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

for scenario in exit0_failure failures_only marker_only missing_summary zero_checks drift_less drift_more; do
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
grep -Fq "ERRO: TestRunner executou 0 checks; baseline ${BASE}" "$TMP/zero_checks.log"
grep -Fq "ERRO: TestRunner executou $((BASE - 1)) checks; baseline ${BASE}" "$TMP/drift_less.log"
grep -Fq "ERRO: TestRunner executou $((BASE + 1)) checks; baseline ${BASE}" "$TMP/drift_more.log"
# O baseline nao pode apodrecer em silencio: tem de ser um numero lido do
# arquivo, nao escrito a mao aqui nem em manager/run_tests.sh.
grep -Eq '^[0-9]+$' <(grep -Eo '[0-9]+' "$ROOT/manager/test_checks_baseline" | head -1)
if grep -qE 'MIN_CHECKS=[0-9]+' "$ROOT/manager/run_tests.sh"; then
    echo "FALHA: run_tests.sh ainda tem MIN_CHECKS escrito a mao; o baseline vem de manager/test_checks_baseline" >&2
    exit 1
fi
echo "PASS: falha registrada, marker, resumo ausente, checks=0 e drift do baseline sao recusados"
