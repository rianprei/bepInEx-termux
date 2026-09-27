package io.github.rianprei.bepinex.manager.test;

import io.github.rianprei.bepinex.manager.core.LooseModInstaller;
import io.github.rianprei.bepinex.manager.core.SelectedFileFlow;
import io.github.rianprei.bepinex.manager.core.SelectedFileStager;

import java.io.File;
import java.nio.file.Files;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.List;
import java.util.Queue;
import java.util.concurrent.Executor;

/**
 * O fluxo dirigido passo a passo, com executor falso: cada chamada fica
 * enfileirada até o teste executar. É a MESMA API que a Activity chama —
 * o duplo toque e o sweep concorrente são provados no nível certo.
 */
public final class SelectedFileFlowTest {
    private SelectedFileFlowTest() {}

    private static class FakeExecutor implements Executor {
        final Queue<Runnable> tasks = new ArrayDeque<>();
        @Override public void execute(Runnable command) { tasks.add(command); }
        void runOne() {
            if (!tasks.isEmpty()) tasks.poll().run();
        }
        void runAll() {
            while (!tasks.isEmpty()) tasks.poll().run();
        }
    }

    private static final class FakeUiExecutor extends FakeExecutor { }

    public static void run() throws Exception {
        testSweepDuranteStage();
        testSweepDuranteInstall();
        testSweepDepoisDeTudo();
        testDuploToque();
        testHerancaDeRecriacao();
        System.out.println("  [OK] SelectedFileFlowTest (stage/install/sweep/duplo toque/herança)");
    }

    private static void check(String what, boolean cond) {
        if (!cond) throw new AssertionError("falhou: " + what);
    }

    private static File makeStaged(File cache, String name) throws Exception {
        return SelectedFileStager.copyIntoStaging(cache, name,
                () -> new java.io.ByteArrayInputStream("conteudo".getBytes("UTF-8")));
    }

    /** (1) sweep durante stage em voo NÃO apaga. */
    private static void testSweepDuranteStage() throws Exception {
        File cache = Files.createTempDirectory("flow-stage-").toFile();
        try {
            SelectedFileFlow.Flow flow = SelectedFileFlow.of(SelectedFileFlow.MAIN);
            FakeExecutor worker = new FakeExecutor();
            FakeUiExecutor ui = new FakeUiExecutor();
            List<File> stagedRef = new ArrayList<>();

            flow.stage(worker, ui, () -> {
                File staged = makeStaged(cache, "em-copia.so");
                stagedRef.add(staged);
                return staged;
            }, (file, error) -> { });
            check("stage em voo antes de rodar", SelectedFileFlow.anyStageInFlight());
            check("sweep pula a rodada com stage em voo",
                    SelectedFileFlow.sweepOrphans(cache) == -1);

            worker.runOne();   // a cópia acontece e registra o staged como em uso
            check("sweep NÃO apaga o arquivo em cópia",
                    SelectedFileFlow.sweepOrphans(cache) == 0
                            && stagedRef.get(0).exists());

            ui.runOne();       // callback chega à tela
            flow.consume(stagedRef.get(0));
            check("depois de consumir, nada sobra",
                    !stagedRef.get(0).exists() && SelectedFileFlow.sweepOrphans(cache) == 0);
        } finally {
            rmrf(cache);
        }
    }

    /** (2) sweep durante install em voo NÃO apaga. */
    private static void testSweepDuranteInstall() throws Exception {
        File cache = Files.createTempDirectory("flow-install-").toFile();
        try {
            SelectedFileFlow.Flow flow = SelectedFileFlow.of(SelectedFileFlow.MAIN);
            File staged = makeStaged(cache, "em-install.bpatch");
            FakeExecutor worker = new FakeExecutor();
            FakeUiExecutor ui = new FakeUiExecutor();

            check("install começa", flow.install(worker, ui, staged,
                    () -> LooseModInstaller.Result.ok("ok"),
                    (result, error) -> { }));
            check("sweep NÃO apaga arquivo em install em voo",
                    SelectedFileFlow.sweepOrphans(cache) == 0 && staged.exists());

            worker.runOne();
            ui.runOne();       // entrega o callback e libera o arquivo
            flow.consume(staged);
            check("depois do callback, consumido apaga", !staged.exists());
            check("flag de install liberada no fim", !flow.installInFlight());
        } finally {
            rmrf(cache);
        }
    }

    /** (3) sweep depois de tudo terminado apaga o órfão. */
    private static void testSweepDepoisDeTudo() throws Exception {
        File cache = Files.createTempDirectory("flow-orphan-").toFile();
        try {
            File orphan = makeStaged(cache, "abandonado.so");
            check("sweep apaga o órfão", SelectedFileFlow.sweepOrphans(cache) == 1);
            check("órfão sumiu", !orphan.exists());
        } finally {
            rmrf(cache);
        }
    }

    /** (4) dois toques = 1 install, pela MESMA API que a Activity chama. */
    private static void testDuploToque() throws Exception {
        File cache = Files.createTempDirectory("flow-double-").toFile();
        try {
            SelectedFileFlow.Flow flow = SelectedFileFlow.of(SelectedFileFlow.DETAIL);
            File staged = makeStaged(cache, "alvo.bmod");
            FakeExecutor worker = new FakeExecutor();
            FakeUiExecutor ui = new FakeUiExecutor();
            final int[] runs = {0};

            boolean first = flow.install(worker, ui, staged, () -> {
                        runs[0]++;
                        return LooseModInstaller.Result.ok("ok");
                    }, (result, error) -> { });
            boolean second = flow.install(worker, ui, staged, () -> {
                        runs[0]++;
                        return LooseModInstaller.Result.ok("ok");
                    }, (result, error) -> { });

            check("primeiro toque começa", first);
            check("segundo toque é recusado pela MESMA API", !second);

            worker.runOne();
            ui.runOne();
            check("o trabalho rodou exatamente 1 vez", runs[0] == 1);
            check("após terminar, novo install é aceito",
                    flow.install(worker, ui, staged,
                            () -> { runs[0]++; return LooseModInstaller.Result.ok("ok2"); },
                            (r, e) -> { }));
            worker.runOne();
            ui.runOne();
            check("segunda rodada rodou", runs[0] == 2);
            flow.consume(staged);
        } finally {
            rmrf(cache);
        }
    }

    /** (5) chamador recriado herda o pendente. */
    private static void testHerancaDeRecriacao() throws Exception {
        try {
            SelectedFileFlow.Flow first = SelectedFileFlow.of(SelectedFileFlow.MAIN);
            File staged = new File("/tmp", "herdado.so");   // caminho simbólico: sem I/O
            check("instância antiga deixa pendente", first.pending().set(staged));

            SelectedFileFlow.Flow recreated = SelectedFileFlow.of(SelectedFileFlow.MAIN);
            check("instância recriada é a MESMA (estado estático)", recreated == first);
            check("herda o pendente", recreated.pending().take() == staged);

            // Fluxo da DETAIL independente do MAIN.
            SelectedFileFlow.Flow detail = SelectedFileFlow.of(SelectedFileFlow.DETAIL);
            check("DETAIL não vê pendente do MAIN", detail.pending().take() == null);
        } finally {
            SelectedFileFlow.of(SelectedFileFlow.MAIN).pending().take();
            SelectedFileFlow.of(SelectedFileFlow.DETAIL).pending().take();
        }
    }

    private static void rmrf(File dir) {
        File[] entries = dir.listFiles();
        if (entries != null) for (File e : entries) {
            File[] inner = e.listFiles();
            if (inner != null) for (File i : inner) i.delete();
            e.delete();
        }
        dir.delete();
    }
}
