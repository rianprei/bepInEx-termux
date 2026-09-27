package io.github.rianprei.bepinex.manager.test;

import io.github.rianprei.bepinex.manager.core.DownloadFilePicker;
import io.github.rianprei.bepinex.manager.core.ScanFlow;
import io.github.rianprei.bepinex.manager.core.SuHelper;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

/**
 * Enumera entrada hostil × todo construtor público de comando root do
 * Manager. Cada combinação termina em um de dois estados:
 *
 *  1. RECUSA fail-closed: exceção ou false/null ANTES de montar comando
 *     (validação central do SuHelper) — é o que acontece no device;
 *  2. EXECUÇÃO CONTROLADA: o comando resultante (builder puro) roda em sh
 *     real (padrão do ShellExecTest) e o canário NÃO aparece: a entrada
 *     tenta "touch PWNED"/"touch PWNED2" e nenhum dos arquivos pode existir.
 *
 * A única família que leva dado hostil DENTRO do comando é a do
 * DownloadFilePicker (arquivo real em Download com nome hostil): é a que
 * prova o shellQuote por execução — e é onde a sabotagem (tirar o
 * shellQuote) faz o canário nascer.
 */
public final class RootInjectionTableTest {
    private RootInjectionTableTest() {}

    public static void run() throws Exception {
        Path work = Files.createTempDirectory("root-inj-");
        int combinations = 0;
        try {
            String pwned = work.resolve("PWNED").toString();
            String pwned2 = work.resolve("PWNED2").toString();
            // Cada entrada tenta tocar os 2 canários (aspas/crase viram parte
            // literal do nome do arquivo se o quoting segurar).
            String[] entries = {
                    "x'; touch " + pwned + "; #",
                    "x$(touch " + pwned + ")x",
                    "x`touch " + pwned + "`x",
                    "x\n touch " + pwned,
                    "x\"; touch " + pwned + "; #",
                    "x| touch " + pwned,
                    "x&& touch " + pwned,
                    "x> " + pwned2,
                    "..'; touch " + pwned + "; #",
                    "",
                    "-x; touch " + pwned,
                    "a b; touch " + pwned,
            };

            combinations += sinksPorPath(entries);
            combinations += sinksPorPkg(entries);
            combinations += sinksPorExecucao(work, entries, pwned, pwned2);

            if (Files.exists(work.resolve("PWNED")) || Files.exists(work.resolve("PWNED2"))) {
                throw new AssertionError("CANÁRIO EXECUTADO: entrada hostil virou comando root");
            }
            System.out.println("  [OK] RootInjectionTableTest (" + combinations
                    + " combinações hostis: recusa fail-closed ou sh real sem canário)");
        } finally {
            rmrf(work);
        }
    }

    private interface ThrowingSink { boolean run() throws Exception; }

    /** Sinks que recebem o dado como CAMINHO de arquivo (validação requirePath). */
    private static int sinksPorPath(String[] entries) {
        int n = 0;
        for (String e : entries) {
            List<ThrowingSink> sinks = new ArrayList<>();
            // Entrada CRUA como caminho: vazio/".."/metacaractere são todos
            // recusados por requirePath antes de qualquer comando.
            sinks.add(() -> SuHelper.listFiles(e).isEmpty());
            sinks.add(() -> SuHelper.readTextFile(e) == null);
            if (!e.isEmpty()) {
                // Entrada CONCATENADA ao mods root: com "" viraria o caminho
                // legítimo do root (executaria de verdade) — isso não é entrada
                // hostil, é o uso normal; por isso só entra se não-vazia.
                String p = "/data/local/tmp/mods/" + e;
                sinks.add(() -> SuHelper.listFiles(p).isEmpty());
                sinks.add(() -> SuHelper.readTextFile(p) == null);
                sinks.add(() -> !SuHelper.deleteFile(p));
                sinks.add(() -> !SuHelper.writeTextFile(p, "x"));
            }
            sinks.add(() -> !SuHelper.copyFile(e, "/data/local/tmp/mods/com.foo/a.so", "644"));
            sinks.add(() -> !SuHelper.copyFile("/data/local/tmp/mods/com.foo/a.so", e, "644"));
            sinks.add(() -> !SuHelper.readCrashGuard(e).marker
                    && SuHelper.readCrashGuard(e).count == 0);
            for (ThrowingSink sink : sinks) {
                expectFailClosed("path", e, sink);
                n++;
            }
        }
        return n;
    }

    /** Sinks que recebem o dado como NOME DE PACOTE (validação requirePkg). */
    private static int sinksPorPkg(String[] entries) {
        int n = 0;
        for (String e : entries) {
            List<ThrowingSink> sinks = new ArrayList<>();
            sinks.add(() -> !SuHelper.ensureModDir(e));
            sinks.add(() -> !SuHelper.restartGame(e, e));
            sinks.add(() -> !SuHelper.reactivateMods(e));
            sinks.add(() -> !SuHelper.deleteMod(e, "mod.so"));
            sinks.add(() -> !SuHelper.clearLog(e));
            sinks.add(() -> !SuHelper.readCrashGuard(e).marker
                    && SuHelper.readCrashGuard(e).count == 0);
            for (ThrowingSink sink : sinks) {
                expectFailClosed("pkg", e, sink);
                n++;
            }
        }
        return n;
    }

    /**
     * Sinks onde o dado hostil REAL entra no comando entre aspas: a prova é
     * por execução em sh real — o comando roda e o canário não nasce.
     */
    private static int sinksPorExecucao(Path work, String[] entries,
                                        String pwned, String pwned2) throws Exception {
        int n = 0;
        // (a) ScanFlow.dumpProbeCommand: path hostil entre aspas simples fixas.
        for (String e : entries) {
            String cmd = ScanFlow.dumpProbeCommand("/data/data/" + e + "/files/bepinex/dump.tsv");
            sh(cmd, work);
            checkCanario("dumpProbeCommand", e, pwned, pwned2);
            n++;
        }
        // (b) DownloadFilePicker: arquivo REAL com nome hostil dentro de
        //     /sdcard/Download simulado; o path inteiro entra no comando.
        Path download = work.resolve("sdcard").resolve("Download");
        Path cache = work.resolve("cache");
        Files.createDirectories(download);
        Files.createDirectories(cache);
        String[] nomes = {
                // Nome de arquivo NÃO pode conter '/': o canário aqui é nu
                // (touch PWNED), criado no diretório de trabalho do sh.
                "-a'; touch PWNED; #.so",
                "a$(touch PWNED)b.so",
                "a`touch PWNED`b.so",
                "a\nb; touch PWNED.so",
                "a b; touch PWNED.so",
                "-rf; touch PWNED.so",
        };
        for (String nome : nomes) {
            Path src = download.resolve(nome);
            Files.writeString(src, "conteudo");
            String dest = cache.resolve("destino.so").toString();
            String cmd = DownloadFilePicker.copyToCacheCommand(src.toString(), dest,
                    download.toString(), work.resolve("sdcard/Documents").toString());
            sh(cmd, work);
            checkCanario("copyToCacheCommand", nome, pwned, pwned2);
            Files.deleteIfExists(src);
            Files.deleteIfExists(Path.of(dest));
            n++;
        }
        // (c) a listagem dos 2 dirs fixos: comando constante, só sanity.
        sh(DownloadFilePicker.listCommand(), work);
        checkCanario("listCommand", "<constante>", pwned, pwned2);
        n++;
        return n;
    }

    /** Falha se a sink executar algo (não recusou) OU se lançar erro inesperado. */
    private static void expectFailClosed(String kind, String entry, ThrowingSink sink) {
        boolean rejected;
        try {
            rejected = sink.run();
        } catch (IllegalArgumentException expected) {
            rejected = true;
        } catch (Exception other) {
            throw new AssertionError("sink " + kind + " lançou erro inesperado com ["
                    + entry + "]: " + other, other);
        }
        if (!rejected) {
            throw new AssertionError("sink " + kind + " ACEITOU entrada hostil: [" + entry + "]");
        }
    }

    private static void sh(String command, Path work) throws IOException, InterruptedException {
        Process p = new ProcessBuilder("sh", "-c", command)
                .directory(work.toFile())
                .redirectErrorStream(true)
                .start();
        if (!p.waitFor(10, java.util.concurrent.TimeUnit.SECONDS)) {
            p.destroyForcibly();
            throw new AssertionError("comando não terminou em 10s: " + command);
        }
    }

    private static void checkCanario(String sink, String entry, String pwned, String pwned2) {
        if (new java.io.File(pwned).exists() || new java.io.File(pwned2).exists()) {
            throw new AssertionError("CANÁRIO EXECUTADO em " + sink + " com entrada [" + entry + "]");
        }
    }

    private static void rmrf(Path dir) {
        FileDelete.delete(dir);
    }

    /** Deleção recursiva simples (java.util.File não tem; manter dependência zero). */
    private static final class FileDelete {
        static void delete(Path path) {
            java.io.File f = path.toFile();
            java.io.File[] children = f.listFiles();
            if (children != null) {
                for (java.io.File c : children) delete(c.toPath());
            }
            f.delete();
        }
    }
}
