#!/usr/bin/env bash
# test/manager_runner_real_fail_test.sh — o TestRunner REAL accumula e sai != 0.
#
# O contrato do runner (manager/test/.../TestRunner.java) é: cada teste conta
# os próprios checks, as falhas ficam registradas por teste e o processo sai
# com status diferente de zero. Nada na suíte real falha, então este teste
# compila e roda o MESMO TestRunner de produção com
# BEPINEX_TEST_FAILING_FIXTURE=1, que executa um teste que falha de propósito
# (FixtureFailingTest), e exige as três provas:
#   1. status de saída diferente de zero;
#   2. a linha "[RUNNER FAIL] FixtureFailingTest (1)" — o (1) é a contagem
#      ACUMULADA do runTest; se alguém trocar o acúmulo por 0, a linha passa a
#      dizer (0) e este teste falha;
#   3. o resumo "RUNNER: ... falhas=1" e o erro do run_tests.sh
#      "registrou falha por teste".
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOG="$(mktemp)"
trap 'rm -f "$LOG"' EXIT

fails=0

if [ -z "${JAVA_HOME:-}" ] && ! command -v javac >/dev/null 2>&1; then
    echo "manager_runner_real_fail: sem javac no host; o runner REAL nao roda aqui" >&2
    exit 1
fi

# Copia a arvore do manager para um temp: compilar na worktree deixaria
# manager/build/test_classes sujo e o gate nao pode sujar a arvore.
TMP="$(mktemp -d)"
trap 'rm -f "$LOG"; rm -rf "$TMP"' EXIT
mkdir -p "$TMP/manager"
cp -r "$ROOT/manager/src" "$TMP/manager/src"
cp -r "$ROOT/manager/test" "$TMP/manager/test"
cp "$ROOT/manager/run_tests.sh" "$ROOT/manager/version.sh" "$TMP/manager/"
cp "$ROOT/manager/test_checks_baseline" "$TMP/manager/"
cp "$ROOT/VERSION" "$TMP/VERSION"

BEPINEX_TEST_FAILING_FIXTURE=1 bash "$TMP/manager/run_tests.sh" >"$LOG" 2>&1
status=$?

if [ "$status" -eq 0 ]; then
    echo "manager_runner_real_fail: runner saiu com 0 tendo 1 teste que falha" >&2
    fails=1
fi
if ! grep -Fq '[RUNNER FAIL] FixtureFailingTest (1)' "$LOG"; then
    echo "manager_runner_real_fail: linha [RUNNER FAIL] com o acúmulo (1) ausente:" >&2
    grep -F '[RUNNER FAIL]' "$LOG" | sed 's/^/    /' >&2
    echo "    (acumulado quebrado: o runTest parou de contar as falhas do teste)" >&2
    fails=1
fi
if ! grep -Eq '^RUNNER: checks=[0-9]+ falhas=1$' "$LOG"; then
    echo "manager_runner_real_fail: resumo com falhas=1 ausente:" >&2
    grep -E '^RUNNER: ' "$LOG" | sed 's/^/    /' >&2
    fails=1
fi
if ! grep -Fq '[FAIL] FixtureFailingTest: falhou: fixture que falha de proposito' "$LOG"; then
    echo "manager_runner_real_fail: a falha do fixture nao foi registrada por teste:" >&2
    grep -F '[FAIL]' "$LOG" | sed 's/^/    /' >&2
    fails=1
fi
if ! grep -Fq 'ERRO: TestRunner registrou falha por teste' "$LOG"; then
    echo "manager_runner_real_fail: run_tests.sh nao recusou a suite com falha" >&2
    fails=1
fi

# Controle: a MESMA suite sem a fixture tem de passar. Se isto falhasse, o
# teste acima so estaria provando que o gate esta quebrado.
if BEPINEX_TEST_FAILING_FIXTURE=0 bash "$TMP/manager/run_tests.sh" >"$LOG.clean" 2>&1; then
    :
else
    echo "manager_runner_real_fail: controle (suite real sem fixture) falhou:" >&2
    tail -5 "$LOG.clean" | sed 's/^/    /' >&2
    fails=1
fi

if [ "$fails" -ne 0 ]; then
    exit 1
fi
echo "manager_runner_real_fail: runner real saiu != 0 com a falha registrada e acumulada; controle sem fixture passa"
