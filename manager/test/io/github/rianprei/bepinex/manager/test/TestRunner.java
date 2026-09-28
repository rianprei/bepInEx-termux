package io.github.rianprei.bepinex.manager.test;

import java.util.ArrayList;
import java.util.List;

public class TestRunner {
    @FunctionalInterface
    private interface TestBody {
        void run() throws Throwable;
    }

    private static final List<String> failures = new ArrayList<>();
    private static String currentTest;
    private static int checksRun;

    static synchronized void check(String name, boolean condition) {
        checksRun++;
        if (!condition) failures.add(currentTest + ": falhou: " + name);
    }

    private static void runTest(String name, TestBody body) {
        int failuresBefore = failures.size();
        currentTest = name;
        try {
            body.run();
        } catch (Throwable t) {
            String message = t.getMessage();
            failures.add(name + ": exceção inesperada " + t.getClass().getSimpleName()
                    + (message == null || message.isEmpty() ? "" : ": " + message));
            t.printStackTrace(System.err);
        } finally {
            currentTest = null;
        }

        if (failures.size() == failuresBefore) {
            System.out.println("[RUNNER PASS] " + name);
        } else {
            System.out.println("[RUNNER FAIL] " + name + " (" + (failures.size() - failuresBefore) + ")");
        }
    }

    public static void main(String[] args) {
        System.out.println("=== Executando Testes Unitarios JVM (Contratos C2-C7 + F1d + VERSION + .bmod hostil + C4 compartilhado + orcamento de su + matriz de tipos) ===");
        runTest("ManifestParserTest", ManifestParserTest::run);
        runTest("ConfTest", ConfTest::run);
        runTest("PatchGeneratorTest", PatchGeneratorTest::run);
        runTest("DumpParserTest", DumpParserTest::run);
        runTest("EngineDetectorTest", EngineDetectorTest::run);
        runTest("GameInfoSorterTest", GameInfoSorterTest::run);
        runTest("NativeAbiDetectorTest", NativeAbiDetectorTest::run);
        runTest("ModContentDetectorTest", ModContentDetectorTest::run);
        runTest("SelectedFileRouterTest", SelectedFileRouterTest::run);
        runTest("SelectedFileStagerTest", SelectedFileStagerTest::run);
        runTest("SelectedFileWorkTest", SelectedFileWorkTest::run);
        runTest("CrashGuardStateTest", CrashGuardStateTest::run);
        runTest("BuildVersionTest", BuildVersionTest::run);
        runTest("SuHelperTest", SuHelperTest::run);
        runTest("ScanFlowTest", ScanFlowTest::run);
        runTest("BmodInstallerTest", BmodInstallerTest::run);
        runTest("C4FixtureTest", C4FixtureTest::run);
        runTest("ModTypeMatrixTest", ModTypeMatrixTest::run);
        runTest("RootCallBudgetTest", RootCallBudgetTest::run);
        runTest("InFlightFlagTest", InFlightFlagTest::run);
        runTest("UiLivenessTest", UiLivenessTest::run);
        runTest("PendingStagedFileTest", PendingStagedFileTest::run);
        runTest("SelectedFileFlowTest", SelectedFileFlowTest::run);
        runTest("RootInjectionTableTest", RootInjectionTableTest::run);
        runTest("ShellExecTest", ShellExecTest::run);
        runTest("Dll2PatchTest", Dll2PatchTest::run);

        System.out.println("RUNNER: checks=" + checksRun + " falhas=" + failures.size());
        if (failures.isEmpty()) {
            System.out.println("=== TODOS OS TESTES PASSARAM (0 FALHAS) ===");
            System.exit(0);
        }

        System.err.println("=== FALHAS DOS TESTES (" + failures.size() + ") ===");
        for (String failure : failures) System.err.println("[FAIL] " + failure);
        System.exit(1);
    }
}
