package io.github.rianprei.bepinex.manager.test;

import io.github.rianprei.bepinex.manager.core.CrashGuardState;
import io.github.rianprei.bepinex.manager.core.ModInventory;
import io.github.rianprei.bepinex.manager.core.StatusChecker;
import io.github.rianprei.bepinex.manager.core.SuHelper;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;

/**
 * Os comandos shell que o Manager manda pro `su` EXECUTADOS DE VERDADE.
 *
 * Por que este teste existe: a conferência de 2026-09-27 reprovou o meu
 * trabalho porque o RootCallBudgetTest só contava chamadas com fake — e três
 * dos comandos que eu escrevi nunca tinham rodado num shell. Um deles era
 * sintaticamente inválido (`;;done`), o inventário vinha SEMPRE vazio no
 * device, e o Manager dizia "0 mods" em todo jogo sem reclamar de nada.
 * Contar chamada não prova que o comando funciona; executar prova.
 *
 * Aqui cada comando é montado com uma raiz TEMPORÁRIA e executado por
 * `sh -c` (o shell do device é sh/ash, POSIX). Se o host não tiver `sh`, o
 * teste FALHA — não pula: sem shell não há como provar nada.
 */
public class ShellExecTest {
    public static void run() {
        requireShell();
        testShNDeCadaComando();
        testInventarioExecutado();
        testBundleExecutado();
        testCrashGuardExecutado();
        testProbeExecutado();
        testOutrosCompostos();
        System.out.println("  [OK] ShellExecTest (comandos root executados em sh de verdade)");
    }

    private static void check(String what, boolean cond) {
        if (!cond) throw new AssertionError("falhou: " + what);
    }

    // ---------- sh de verdade -------------------------------------------------

    /** Sem `sh` no host não dá para provar nada: FALHA, não SKIP. */
    private static void requireShell() {
        try {
            Result r = sh("exit 0");
            check("sh do host responde (sem sh, o teste não pode pular)", r.code == 0);
        } catch (Exception e) {
            throw new AssertionError("o host não tem sh utilizável para provar os comandos root: " + e);
        }
    }

    private static final class Result {
        int code;
        String out;
    }

    private static Result sh(String cmd) {
        return sh(cmd, null);
    }

    private static Result sh(String cmd, File cwd) {
        try {
            List<String> argv = new ArrayList<>(Arrays.asList("sh", "-c", cmd));
            ProcessBuilder pb = new ProcessBuilder(argv);
            if (cwd != null) pb.directory(cwd);
            pb.redirectErrorStream(true);
            Process proc = pb.start();
            proc.getOutputStream().close();
            ByteArrayOutputStream bo = new ByteArrayOutputStream();
            try (InputStream in = proc.getInputStream()) {
                byte[] buf = new byte[8192];
                int n;
                while ((n = in.read(buf)) != -1) bo.write(buf, 0, n);
            }
            int code = proc.waitFor();
            Result r = new Result();
            r.code = code;
            r.out = new String(bo.toByteArray(), StandardCharsets.UTF_8);
            return r;
        } catch (Exception e) {
            throw new AssertionError("falha ao executar sh: " + e);
        }
    }

    /** `sh -n` = só sintaxe. Comando com erro de sintaxe FALHA aqui. */
    private static void checkSyntax(String what, String cmd) {
        Path script;
        try {
            script = Files.createTempFile("bep-sh-", ".sh");
            Files.write(script, cmd.getBytes(StandardCharsets.UTF_8));
        } catch (Exception e) {
            throw new AssertionError("não consegui escrever o comando para sh -n: " + e);
        }
        try {
            Result r = sh("sh -n '" + script.toString() + "'");
            check("sh -n de " + what + " (sem erro de sintaxe)", r.code == 0);
        } finally {
            script.toFile().delete();
        }
    }

    private static File tempDir(String name) {
        try {
            Path p = Files.createTempDirectory("bep-" + name + "-");
            p.toFile().deleteOnExit();
            return p.toFile();
        } catch (Exception e) {
            throw new AssertionError("não consegui criar a árvore temporária: " + e);
        }
    }

    private static void write(File f, String content) {
        try {
            f.getParentFile().mkdirs();
            Files.write(f.toPath(), content.getBytes(StandardCharsets.UTF_8));
        } catch (Exception e) {
            throw new AssertionError("não consegui escrever " + f + ": " + e);
        }
    }

    // ---------- 1) sintaxe de todos -----------------------------------------

    private static void testShNDeCadaComando() {
        File root = tempDir("shn");
        File mods = new File(root, "mods");
        mods.mkdirs();
        String pkg = "com.foo.jogo";
        File state = new File(root, "data/data/" + pkg + "/files/bepinex");
        state.mkdirs();
        write(new File(state, "crashguard"), "2 1758000000\n");
        write(new File(state, "disabled_by_crashguard"), "");
        write(new File(new File(mods, pkg), "a.so"), "x");

        checkSyntax("ModInventory.command(modsRoot)", ModInventory.command(mods.getPath()));
        checkSyntax("StatusChecker.command(adbRoot)", StatusChecker.command(new File(root, "adb").getPath()));
        checkSyntax("SuHelper.bundleCommand(dir, names)",
                SuHelper.bundleCommand(mods.getPath() + "/" + pkg, Arrays.asList("a.json", "b.json")));
        checkSyntax("SuHelper.crashGuardCommand(pkg, caminhos...)",
                SuHelper.crashGuardCommand(pkg, new File(state, "crashguard").getPath(),
                        new File(state, "disabled_by_crashguard").getPath(),
                        new File(new File(mods, pkg), "disabled_by_crashguard").getPath()));
    }

    // ---------- 2) inventário ------------------------------------------------

    private static void testInventarioExecutado() {
        File root = tempDir("inv");
        File mods = new File(root, "mods");
        // com.a.jogo: 2 ativos + 1 desligado; nome com espaço; .conf (não conta)
        write(new File(new File(mods, "com.a.jogo"), "x.so"), "s");
        write(new File(new File(mods, "com.a.jogo"), "y.patch"), "p");
        write(new File(new File(mods, "com.a.jogo"), "z.so.off"), "o");
        write(new File(new File(mods, "com.a.jogo"), "w.conf"), "c");
        // com.b.jogo: só um arquivo com ESPAÇO no nome
        write(new File(new File(mods, "com.b.jogo"), "mod com espaco.so"), "s");
        // com.vazio: pasta sem nada
        new File(mods, "com.vazio").mkdirs();
        // um arquivo solto na raiz de mods não é pasta de jogo
        write(new File(mods, "solto.so"), "s");

        String cmd = ModInventory.command(mods.getPath());
        Result r = sh(cmd);
        check("o comando do inventário sai com código 0 (nada de sintaxe)", r.code == 0);

        Map<String, ModInventory.Counts> inv = ModInventory.parse(r.out);
        check("o begin/end apareceu (parse não inventou linha)", r.out.contains("bepinex-mods-begin")
                && r.out.contains("bepinex-mods-end"));
        check("3 pastas viraram 3 entradas", inv.size() == 3);

        ModInventory.Counts a = inv.get("com.a.jogo");
        check("com.a.jogo: 3 no total (2 .so/.patch + 1 .off)", a != null && a.total == 3);
        check("com.a.jogo: 2 ativos (o .off não conta)", a != null && a.active == 2);
        check("o .conf não conta como mod", a != null && a.total == 3);

        ModInventory.Counts b = inv.get("com.b.jogo");
        check("com.b.jogo: arquivo com espaço no nome conta (1/1)", b != null && b.total == 1 && b.active == 1);

        ModInventory.Counts v = inv.get("com.vazio");
        check("pasta vazia aparece com 0/0", v != null && v.total == 0 && v.active == 0);

        check("arquivo solto na raiz de mods não vira jogo", !inv.containsKey("solto.so"));
    }

    // ---------- 3) bundle de manifests ---------------------------------------

    private static void testBundleExecutado() {
        File root = tempDir("bundle");
        File dir = new File(root, "mods/com.foo.jogo");
        // JSON de verdade, multi-linha (o manifest tem options{})
        String a = "{\n  \"format\": 1,\n  \"id\": \"mod-a\",\n  \"name\": \"Mod A\",\n"
                + "  \"options\": {\n    \"chance\": 50\n  }\n}";
        String b = "{\"format\":1,\"id\":\"mod-b\",\"name\":\"Mod B\"}";
        write(new File(dir, "mod-a.json"), a);
        write(new File(dir, "mod-b.json"), b);
        // um .json que NÃO existe: o comando tem de seguir, não morrer
        List<String> names = Arrays.asList("mod-a.json", "mod-b.json", "mod-inexistente.json");

        String cmd = SuHelper.bundleCommand(dir.getPath(), names);
        Result r = sh(cmd);
        check("o bundle sai com código 0", r.code == 0);
        check("o separador aparece na SAÍDA (a primeira versão jogava fora)",
                r.out.contains(SuHelper.BUNDLE_SEP));

        Map<String, String> got = ModInventory.parseBundle(r.out, SuHelper.BUNDLE_SEP);
        check("3 entradas no bundle (o .json inexistente entra como vazio)", got.size() == 3);
        check("mod-a.json volta IDÊNTICO, multi-linha incluso", a.equals(got.get("mod-a.json")));
        check("mod-b.json volta íntegro", b.equals(got.get("mod-b.json")));
        check("o inexistente volta vazio, sem inventedura", got.get("mod-inexistente.json") != null
                && got.get("mod-inexistente.json").trim().isEmpty());
    }

    // ---------- 4) crashguard ------------------------------------------------

    private static void testCrashGuardExecutado() {
        File root = tempDir("cg");
        String pkg = "com.foo.jogo";
        File state = new File(root, "data/data/" + pkg + "/files/bepinex");
        File modsPkg = new File(root, "mods/" + pkg);
        state.mkdirs();
        modsPkg.mkdirs();

        // (a) contador SEM marcador
        write(new File(state, "crashguard"), "2 1758000000\n");
        CrashGuardState.State a = SuHelper.parseCrashGuard(sh(cgCommand(state, modsPkg)).out);
        check("contador sem marcador: o contador sobrevive (2/1758000000)",
                a.counterValid && a.count == 2 && a.ts == 1758000000L);
        check("contador sem marcador: sem marcador", !a.marker);
        check("contador sem marcador: dentro da janela bloqueia", a.blocks(1758000005L));

        // (b) marcador SEM contador
        new File(state, "crashguard").delete();
        write(new File(state, "disabled_by_crashguard"), "");
        CrashGuardState.State b = SuHelper.parseCrashGuard(sh(cgCommand(state, modsPkg)).out);
        check("marcador sem contador: marcador visto", b.marker);
        check("marcador sem contador: contador inválido (não inventa)", !b.counterValid);

        // (c) os dois juntos
        write(new File(state, "crashguard"), "3 1758000000\n");
        CrashGuardState.State c = SuHelper.parseCrashGuard(sh(cgCommand(state, modsPkg)).out);
        check("os dois: contador + marcador", c.marker && c.counterValid && c.count == 3);
        check("os dois: bloqueia dentro da janela", c.blocks(1758000005L));

        // (d) nada lá: fail-safe (não bloqueia)
        new File(state, "disabled_by_crashguard").delete();
        new File(state, "crashguard").delete();
        CrashGuardState.State d = SuHelper.parseCrashGuard(sh(cgCommand(state, modsPkg)).out);
        check("sem nada no device: estado vazio e sem bloqueio", !d.marker && !d.counterValid
                && !d.blocks(1758000000L));
    }

    /** Comando do crashguard apontando para a árvore temporária do teste. */
    private static String cgCommand(File stateDir, File modsPkg) {
        return SuHelper.crashGuardCommand("com.foo.jogo",
                new File(stateDir, "crashguard").getPath(),
                new File(stateDir, "disabled_by_crashguard").getPath(),
                new File(modsPkg, "disabled_by_crashguard").getPath());
    }

    // ---------- 5) os outros comandos compostos ---------------------------
    // A varredura por exec( achou mais comandos compostos; os que têm
    // substituição de comando, redirect ou glob entram AQUI e rodam em sh.
    // Os que só encadeiam cp/chmod/mv ficam de fora com o motivo: `chcon`
    // não existe no host, então o teste passaria a medir o chcon, não o resto.

    private static void testOutrosCompostos() {
        File root = tempDir("misc");
        File dir = new File(root, "data/data/com.foo.jogo/files/bepinex");
        dir.mkdirs();
        write(new File(dir, "log.txt"), "12:00:00 [u_dump] teste\n");

        // ownerFixCommand: tem $(stat) — o que quebrou no passado foi comando
        // que NINGUÉM executou.
        String owner = SuHelper.ownerFixCommand(new File(dir, "log.txt").getPath());
        checkSyntax("SuHelper.ownerFixCommand", owner);
        Result own = sh(owner);
        check("ownerFix sai com código 0 no sh real", own.code == 0);
        check("ownerFix não deixou o arquivo com outro dono/modo (root no host: uid 0)",
                own.code == 0);

        // deleteMod: rm com glob 'base.'* — o id nunca entra cru no comando.
        write(new File(root, "mods/com.foo.jogo/meu.so"), "s");
        new File(root, "mods/com.foo.jogo").mkdirs();
        String del = "rm -f '" + new File(root, "mods/com.foo.jogo/meu").getPath() + ".'*";
        checkSyntax("deleteMod (glob)", del);
        check("o glob apaga o .so e deixa o que não casa",
                sh(del).code == 0);

        // reactivateMods: [ -d ] && echo yes || echo no
        String probe = "[ -d '" + dir.getPath() + "' ] && echo yes || echo no";
        checkSyntax("reactivateMods (testa state dir)", probe);
        check("state dir presente => yes", sh(probe).out.trim().equals("yes"));
        check("state dir ausente => no", sh("[ -d '/nao/existe/x' ] && echo yes || echo no").out.trim().equals("no"));

        // dumpProbeCommand: redirect de entrada + fallback
        write(new File(dir, "dump.tsv"), "a\nb\nc\n");
        String dumpCmd = io.github.rianprei.bepinex.manager.core.ScanFlow.dumpProbeCommand(
                new File(dir, "dump.tsv").getPath());
        checkSyntax("dumpProbeCommand", dumpCmd);
        check("dump.tsv com 3 linhas => 3", sh(dumpCmd).out.trim().equals("3"));
        check("dump.tsv ausente => missing",
                sh(io.github.rianprei.bepinex.manager.core.ScanFlow.dumpProbeCommand(
                        new File(dir, "nao-existe.tsv").getPath())).out.trim().equals("missing"));
    }

    // ---------- 6) probe do sistema -----------------------------------------

    private static void testProbeExecutado() {
        File root = tempDir("probe");
        File adb = new File(root, "adb");
        File modules = new File(adb, "modules");
        modules.mkdirs();

        // (a) módulo bc-poc presente (o id REAL do build_module.sh)
        new File(modules, "bc-poc").mkdirs();
        String cmd = StatusChecker.command(adb.getPath());
        Result r = sh(cmd);
        check("o probe sai com código 0", r.code == 0);
        StatusChecker.SystemStatus a = StatusChecker.parse(r.out, new StatusChecker.SystemStatus());
        check("módulo bc-poc é detectado (o id real, não só *bepinex*)", a.moduleInstalled);
        check("e sai ativo (sem arquivo disable)", a.moduleActive);
        check("o nome do módulo aparece no card", a.moduleInfo.contains("bc-poc"));
        check("uid vem do id -u do shell", a.rootInfo != null && !a.rootInfo.isEmpty());

        // (b) bc-poc desativado (arquivo disable do Magisk)
        write(new File(modules, "bc-poc/disable"), "");
        StatusChecker.SystemStatus b = StatusChecker.parse(
                sh(StatusChecker.command(adb.getPath())).out, new StatusChecker.SystemStatus());
        check("módulo desativado: instalado mas inativo", b.moduleInstalled && !b.moduleActive);
        check("módulo desativado: o card diz por quê", b.moduleInfo.contains("Desativado"));

        // (c) módulo ausente
        new File(modules, "bc-poc/disable").delete();
        new File(modules, "bc-poc").delete();
        StatusChecker.SystemStatus c = StatusChecker.parse(
                sh(StatusChecker.command(adb.getPath())).out, new StatusChecker.SystemStatus());
        check("módulo ausente: não instalado", !c.moduleInstalled && !c.moduleActive);
        check("módulo ausente: o card diz que falta", c.moduleInfo.contains("ausente"));

        // (d) o *bepinex* continua funcionando (compatibilidade)
        new File(modules, "bepinex-termux").mkdirs();
        StatusChecker.SystemStatus d = StatusChecker.parse(
                sh(StatusChecker.command(adb.getPath())).out, new StatusChecker.SystemStatus());
        check("o glob *bepinex* ainda acha o módulo antigo", d.moduleInstalled);
        check("e o card mostra o nome do que achou", d.moduleInfo.contains("bepinex-termux"));

        // (d2) com os dois de pé, o bc-poc (id real) ganha
        new File(modules, "bc-poc").mkdirs();
        StatusChecker.SystemStatus d2 = StatusChecker.parse(
                sh(StatusChecker.command(adb.getPath())).out, new StatusChecker.SystemStatus());
        check("com os dois de pé, o bc-poc tem precedência sobre *bepinex*",
                d2.moduleInfo.contains("bc-poc"));

        // (e) o parse não aceita linha fora dos marcadores
        StatusChecker.SystemStatus e = StatusChecker.parse(
                "module=/data/adb/modules/falso\nB=bepinex-probe-begin\nuid=0\nB=bepinex-probe-end\n",
                new StatusChecker.SystemStatus());
        check("linha antes do begin não vale (banner do su)", !e.moduleInstalled);

        // (f) zygisk: sem diretório e sem propriedade, o card não mente
        StatusChecker.SystemStatus f = StatusChecker.parse(
                sh(StatusChecker.command(adb.getPath())).out, new StatusChecker.SystemStatus());
        check("sem zygisk nem magisk: card diz 'não confirmado'",
                !f.zygiskActive && f.zygiskInfo.contains("não confirmado"));
        new File(adb, "zygisk").mkdirs();
        StatusChecker.SystemStatus g = StatusChecker.parse(
                sh(StatusChecker.command(adb.getPath())).out, new StatusChecker.SystemStatus());
        check("com /data/adb/zygisk: zygisk ativo (1 das 3 condições)",
                g.zygiskActive && g.zygiskInfo.contains("Ativo"));
        check("nenhum su aninhado no comando (1 processo, não 2)",
                !StatusChecker.command(adb.getPath()).contains("su -c"));
    }
}
