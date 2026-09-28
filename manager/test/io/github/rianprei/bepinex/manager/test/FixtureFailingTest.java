package io.github.rianprei.bepinex.manager.test;

/**
 * Fixture que falha DE PROPÓSITO, só para o gate do runner.
 *
 * Ela existe porque o contrato do TestRunner é "acumula as falhas por teste e
 * sai com status diferente de zero", e nada na suíte real falha. O gate
 * (test/manager_runner_real_fail_test.sh) compila e roda o TestRunner de
 * verdade com BEPINEX_TEST_FAILING_FIXTURE=1 e exige: status != 0, a linha
 * [RUNNER FAIL] com a contagem acumulada correta e o resumo com falhas=1.
 * Nenhuma cópia do TestRunner: o código exercitado é o de produção do runner.
 */
final class FixtureFailingTest {
    private FixtureFailingTest() {}

    static void run() {
        TestRunner.check("fixture que falha de proposito", false);
    }
}
