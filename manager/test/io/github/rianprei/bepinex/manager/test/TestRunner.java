package io.github.rianprei.bepinex.manager.test;

public class TestRunner {
    public static void main(String[] args) {
        System.out.println("=== Executando Testes Unitarios JVM (Contratos C2-C7 + F1d + VERSION + .bmod hostil + C4 compartilhado + orcamento de su + matriz de tipos) ===");
        try {
            ManifestParserTest.run();
            ConfTest.run();
            PatchGeneratorTest.run();
            DumpParserTest.run();
            EngineDetectorTest.run();
            ModContentDetectorTest.run();
            SelectedFileRouterTest.run();
            SelectedFileStagerTest.run();
            SelectedFileWorkTest.run();
            CrashGuardStateTest.run();
            BuildVersionTest.run();
            SuHelperTest.run();
            ScanFlowTest.run();
            BmodInstallerTest.run();
            C4FixtureTest.run();
            ModTypeMatrixTest.run();
            RootCallBudgetTest.run();
        InFlightFlagTest.run();
        UiLivenessTest.run();
        PendingStagedFileTest.run();
        SelectedFileFlowTest.run();
        RootInjectionTableTest.run();
            ShellExecTest.run();
            System.out.println("=== TODOS OS TESTES PASSARAM COM SUCESSO (0 FALHAS) ===");
            System.exit(0);
        } catch (Throwable t) {
            System.err.println("FALHA NOS TESTES:");
            t.printStackTrace();
            System.exit(1);
        }
    }
}
