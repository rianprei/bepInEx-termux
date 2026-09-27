package io.github.rianprei.bepinex.manager.test;

import io.github.rianprei.bepinex.manager.core.PendingStagedFile;
import io.github.rianprei.bepinex.manager.core.SelectedFileStager;

import java.io.File;
import java.nio.file.Files;

public final class PendingStagedFileTest {
    private PendingStagedFileTest() {}

    public static void run() throws Exception {
        testHolderSemantica();
        testEscopoDeProcesso();
        testSweepPreservaPendente();
        System.out.println("  [OK] PendingStagedFileTest (staged nunca fica órfão)");
    }

    private static void check(String what, boolean cond) {
        if (!cond) throw new AssertionError("falhou: " + what);
    }

    private static void testHolderSemantica() {
        File first = new File("staged-primeiro");
        File second = new File("staged-segundo");

        // Cenário do órfão: Activity recebe outro staged com o holder ocupado
        // (install anterior ainda em voo). set=false obriga o chamador a
        // apagar o novo em vez de sobrescrever e perder o primeiro.
        check("primeiro staged entra", PendingStagedFile.SHARED.set(first));
        check("segundo com holder ocupado é recusado (o chamador apaga o dele)",
                !PendingStagedFile.SHARED.set(second));
        check("take devolve o primeiro e libera", PendingStagedFile.SHARED.take() == first);
        check("holder vazio não devolve nada", PendingStagedFile.SHARED.take() == null);

        // Reaproveite na rotação: a instância nova faz take e volta a setar.
        PendingStagedFile.SHARED.set(second);
        check("arquivo sobrevive à troca de instância", PendingStagedFile.SHARED.take() == second);
    }

    private static void testEscopoDeProcesso() {
        // Os holders vivem em campos STATIC das Activities: rotação cria
        // Activity NOVA, e holder de instância novo/vazio deixava o staged
        // órfão (achado do Maestro). SHARED e DETAIL separados: o pendente
        // de uma tela não pode ser instalado pela outra no jogo errado.
        check("SHARED e DETAIL são instâncias distintas de processo",
                PendingStagedFile.SHARED != PendingStagedFile.DETAIL);
        File mine = new File("staged-da-detail");
        check("DETAIL aceita com SHARED ocupado (são independentes)",
                PendingStagedFile.DETAIL.set(mine));
        check("peek lê sem consumir", PendingStagedFile.DETAIL.peek() == mine);
        check("take consome", PendingStagedFile.DETAIL.take() == mine);
        check("peek após take é nulo", PendingStagedFile.DETAIL.peek() == null);
    }

    private static void testSweepPreservaPendente() throws Exception {
        // Retaguarda do órfão: 3 staged velhos (processo morto, rodadas
        // antigas) + 1 pendente de verdade. O sweep apaga os 3 e preserva
        // exatamente o diretório do pendente.
        File cache = Files.createTempDirectory("bep-sweep-test-").toFile();
        try {
            File velho1 = stage(cache, "velho1.so");
            File velho2 = stage(cache, "velho2.bmod");
            File velho3 = stage(cache, "velho3.bpatch");
            File pendente = stage(cache, "atual.so");
            File estranho = new File(cache, "nao-e-staging.txt");
            Files.writeString(estranho.toPath(), "x");

            int removed = SelectedFileStager.sweep(cache, pendente,
                    PendingStagedFile.SHARED.peek(), PendingStagedFile.DETAIL.peek());

            check("sweep apaga os 3 órfãos", removed == 3);
            check("órfão 1 sumiu", !velho1.getParentFile().exists() || !velho1.exists());
            check("órfão 2 sumiu", !velho2.exists());
            check("órfão 3 sumiu", !velho3.exists());
            check("pendente sobrevive", pendente.exists());
            check("diretório do pendente sobrevive", pendente.getParentFile().isDirectory());
            check("arquivo fora do staging não é tocado", estranho.exists());
        } finally {
            File[] entries = cache.listFiles();
            if (entries != null) for (File e : entries) {
                File[] inner = e.listFiles();
                if (inner != null) for (File i : inner) i.delete();
                e.delete();
            }
            cache.delete();
        }
    }

    private static File stage(File cache, String name) throws Exception {
        File directory = Files.createTempDirectory(cache.toPath(), "bepinex-import-").toFile();
        File staged = new File(directory, name);
        Files.writeString(staged.toPath(), "conteudo");
        return staged;
    }
}
