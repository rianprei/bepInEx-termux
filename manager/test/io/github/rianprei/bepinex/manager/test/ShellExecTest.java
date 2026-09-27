package io.github.rianprei.bepinex.manager.test;

import io.github.rianprei.bepinex.manager.core.CrashGuardState;
import io.github.rianprei.bepinex.manager.core.DownloadFilePicker;
import io.github.rianprei.bepinex.manager.core.ModInventory;
import io.github.rianprei.bepinex.manager.core.StatusChecker;
import io.github.rianprei.bepinex.manager.core.SuHelper;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.attribute.PosixFilePermission;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.Set;

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
        testCadeiasComChcon();
        testDownloadFileListingAndCopy();
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

    /** "\n" de verdade: o script do stub é escrito em disco, não em tela. */
    private static final String NL = String.valueOf((char) 10);

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

    // ---------- 6) as 4 cadeias com chcon ----------------------------------
    //
    // `chcon` não existe no host, e era o motivo dessas quatro cadeias ficarem
    // de fora do teste de shell. Resolvido com um stub à frente do PATH: ele
    // grava os argumentos num arquivo e sai 0, ou sai 1 se a env pedir.
    // A string que vai para o device é a mesma — o stub só troca o binário.

    /** Cria um stub de chcon e devolve o PATH com ele na frente. */
    private static String pathWithChconStub(File binDir, File logFile, String exitCode) {
        try {
            binDir.mkdirs();
            File stub = new File(binDir, "chcon");
            // Imita o chcon REAL: grava o que recebeu e sai 1 se o alvo não
            // existir. Sem isso, um `; chcon` num alvo inexistente passaria
            // como sucesso e a sabotagem passaria batido (achado da revisão de
            // 2026-09-27).
            String script = "#!/bin/sh" + NL
                    + "printf '%s\\n' \"$*\" >> '" + logFile.getPath() + "'" + NL
                    + "for t; do :; done" + NL
                    + "if [ ! -e \"$t\" ]; then exit 1; fi" + NL
                    + "exit " + exitCode + NL;
            write(stub, script);
            stub.setExecutable(true);
            return binDir.getPath() + ":" + System.getenv("PATH");
        } catch (Exception e) {
            throw new AssertionError("não consegui criar o stub de chcon: " + e);
        }
    }

    private static String chconLog(File logFile) {
        try {
            return Files.exists(logFile.toPath())
                    ? new String(Files.readAllBytes(logFile.toPath()), StandardCharsets.UTF_8) : "";
        } catch (Exception e) {
            throw new AssertionError("não consegui ler o log do stub: " + e);
        }
    }

    private static Result shWithPath(String cmd, String path, File cwd) {
        try {
            ProcessBuilder pb = new ProcessBuilder(Arrays.asList("sh", "-c", cmd));
            pb.environment().put("PATH", path);
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
            Result r = new Result();
            r.code = proc.waitFor();
            r.out = new String(bo.toByteArray(), StandardCharsets.UTF_8);
            return r;
        } catch (Exception e) {
            throw new AssertionError("falha ao executar com PATH custom: " + e);
        }
    }

    private static String mode(File f) {
        try {
            Process proc = new ProcessBuilder(Arrays.asList("stat", "-c", "%a", f.getPath())).start();
            java.io.BufferedReader br = new java.io.BufferedReader(
                    new java.io.InputStreamReader(proc.getInputStream(), StandardCharsets.UTF_8));
            String v = br.readLine();
            proc.waitFor();
            return v == null ? "?" : v.trim();
        } catch (Exception e) {
            throw new AssertionError("stat falhou: " + e);
        }
    }

    private static String read(File f) {
        try {
            return new String(Files.readAllBytes(f.toPath()), StandardCharsets.UTF_8);
        } catch (Exception e) {
            throw new AssertionError("leitura falhou: " + e);
        }
    }

    private static void testCadeiasComChcon() {
        // (1) ensureModDir: cria a pasta com 755 e o contexto certo
        File root = tempDir("chcon-dir");
        File bin = new File(root, "bin");
        File log = new File(root, "chcon.log");
        String path = pathWithChconStub(bin, log, "0");
        // path com ESPAÇO de propósito: a cadeia tem que aguentar
        String dir = new File(root, "mods com espaco/com.foo.jogo").getPath();
        checkSyntax("ensureModDirCommand", SuHelper.ensureModDirCommand(dir));
        Result r = shWithPath(SuHelper.ensureModDirCommand(dir), path, root);
        check("ensureModDir: cadeia sai com 0", r.code == 0);
        check("ensureModDir: a pasta existe", new File(dir).isDirectory());
        check("ensureModDir: modo 755 exato", mode(new File(dir)).equals("755"));
        check("ensureModDir: chcon chamado 1 vez", chconLog(log).trim().split("\n").length == 1);
        check("ensureModDir: chcon com o contexto do contrato e o path",
                chconLog(log).contains(SuHelper.SELINUX_MOD_CONTEXT) && chconLog(log).contains(dir));

        // (2) writeTextFile: copia conteúdo e deixa 644
        File root2 = tempDir("chcon-write");
        File bin2 = new File(root2, "bin");
        File log2 = new File(root2, "chcon.log");
        String path2 = pathWithChconStub(bin2, log2, "0");
        File tmp = new File(root2, "origem.tmp");
        write(tmp, "conteudo do conf\nsegunda linha\n");
        String dest = new File(root2, "destino com espaco.conf").getPath();
        checkSyntax("writeTextFileCommand", SuHelper.writeTextFileCommand(tmp.getPath(), dest));
        Result r2 = shWithPath(SuHelper.writeTextFileCommand(tmp.getPath(), dest), path2, root2);
        check("writeTextFile: cadeia sai com 0", r2.code == 0);
        check("writeTextFile: conteúdo idêntico", read(new File(dest)).equals("conteudo do conf\nsegunda linha\n"));
        check("writeTextFile: modo 644 exato", mode(new File(dest)).equals("644"));
        check("writeTextFile: chcon 1x no destino", chconLog(log2).trim().equals(
                SuHelper.SELINUX_MOD_CONTEXT + " " + dest));

        // (3) copyFile: cp -f com o modo pedido
        File root3 = tempDir("chcon-copy");
        File bin3 = new File(root3, "bin");
        File log3 = new File(root3, "chcon.log");
        String path3 = pathWithChconStub(bin3, log3, "0");
        File src3 = new File(root3, "mod.so");
        write(src3, "ELF de mentira");
        String dest3 = new File(root3, "instalado com espaco.so").getPath();
        checkSyntax("copyFileCommand", SuHelper.copyFileCommand(src3.getPath(), dest3, "755"));
        Result r3 = shWithPath(SuHelper.copyFileCommand(src3.getPath(), dest3, "755"), path3, root3);
        check("copyFile: cadeia sai com 0", r3.code == 0);
        check("copyFile: conteúdo idêntico", read(new File(dest3)).equals("ELF de mentira"));
        check("copyFile: modo 755 exato (o modo do argumento, não 644)", mode(new File(dest3)).equals("755"));
        check("copyFile: chcon 1x no destino", chconLog(log3).trim().equals(
                SuHelper.SELINUX_MOD_CONTEXT + " " + dest3));

        // (4) toggleMod: mv + chcon no nome novo
        File root4 = tempDir("chcon-toggle");
        File bin4 = new File(root4, "bin");
        File log4 = new File(root4, "chcon.log");
        String path4 = pathWithChconStub(bin4, log4, "0");
        String cur = new File(root4, "meu mod.so").getPath();
        String nw = new File(root4, "meu mod.so.off").getPath();
        write(new File(cur), "payload");
        checkSyntax("toggleModCommand", SuHelper.toggleModCommand(cur, nw));
        Result r4 = shWithPath(SuHelper.toggleModCommand(cur, nw), path4, root4);
        check("toggleMod: cadeia sai com 0", r4.code == 0);
        check("toggleMod: o nome antigo sumiu", !new File(cur).exists());
        check("toggleMod: o nome novo tem o conteúdo", read(new File(nw)).equals("payload"));
        check("toggleMod: chcon 1x no nome novo", chconLog(log4).trim().equals(
                SuHelper.SELINUX_MOD_CONTEXT + " " + nw));

        // (5) STUB FALHANDO: a cadeia tem que sair != 0, senão o Java
        // devolveria true com o contexto de SELinux não aplicado — o arquivo
        // fica com o contexto errado e o jogo não lê em modo Enforcing.
        File root5 = tempDir("chcon-fail");
        File bin5 = new File(root5, "bin");
        File log5 = new File(root5, "chcon.log");
        String path5 = pathWithChconStub(bin5, log5, "1");
        String dir5 = new File(root5, "com.foo.jogo").getPath();
        Result rf = shWithPath(SuHelper.ensureModDirCommand(dir5), path5, root5);
        check("stub falhando: a cadeia NÃO sai com 0", rf.code != 0);
        check("stub falhando: o Java traduziu isso em falha (exec.success == code 0)", !(rf.code == 0));
        File src5 = new File(root5, "a.so");
        write(src5, "x");
        String dest5 = new File(root5, "b.so").getPath();
        Result rf2 = shWithPath(SuHelper.copyFileCommand(src5.getPath(), dest5, "644"), path5, root5);
        check("copyFile com stub falhando também sai != 0", rf2.code != 0);

        // (6) O QUE O "&&" COMPRA: se um passo do meio falha, o chcon NÃO
        // pode rodar. Com `;` ele rodaria e a cadeia sairia 0 (o chcon é o
        // último), e o Java devolveria true com modo/contexto errado.
        // Executado de verdade, com um stub de chmod que falha.
        File root6 = tempDir("chmod-fail");
        File bin6 = new File(root6, "bin");
        bin6.mkdirs();
        File chmodLog = new File(root6, "chmod.log");
        File chconLog6 = new File(root6, "chcon.log");
        String stubChmod = "#!/bin/sh" + NL
                + "printf '%s\\n' \"$*\" >> '" + chmodLog.getPath() + "'" + NL
                + "if [ \"$BEP_STUB_CHMOD_FAIL\" = 1 ]; then exit 1; fi" + NL
                + "exit 0" + NL;
        File chmodStub = new File(bin6, "chmod");
        write(chmodStub, stubChmod);
        chmodStub.setExecutable(true);
        File chconStub6 = new File(bin6, "chcon");
        String stubChcon6 = "#!/bin/sh" + NL
                + "printf '%s\\n' \"$*\" >> '" + chconLog6.getPath() + "'" + NL
                + "exit 0" + NL;
        write(chconStub6, stubChcon6);
        chconStub6.setExecutable(true);
        String path6 = bin6.getPath() + ":" + System.getenv("PATH");

        File src6 = new File(root6, "c.so");
        write(src6, "x");
        String dest6 = new File(root6, "d.so").getPath();
        Result okChain = shWithPathEnv(SuHelper.copyFileCommand(src6.getPath(), dest6, "644"),
                path6, root6, "BEP_STUB_CHMOD_FAIL", "0");
        check("com tudo funcionando: a cadeia sai com 0 e o chcon roda",
                okChain.code == 0 && chconLog(chconLog6).contains(dest6));

        String dest6b = new File(root6, "e.so").getPath();
        Result failChain = shWithPathEnv(SuHelper.copyFileCommand(src6.getPath(), dest6b, "644"),
                path6, root6, "BEP_STUB_CHMOD_FAIL", "1");
        check("chmod falhando: a cadeia sai != 0", failChain.code != 0);
        // isto é o que o `&&` garante: com `;` o chcon rodaria e a cadeia
        // sairia 0, e o check abaixo accuse
        check("chmod falhando: o chcon NÃO foi chamado (o && para a cadeia)",
                chconLog(chconLog6).indexOf(dest6b) < 0);

        // (7) O 1º COMANDO DE CADA CADEIA FALHANDO: o `&&` tem que impedir
        // o chcon de rodar (e o chcon que roda em alvo inexistente falha, o
        // que tornaria `;` indistinguível só pelo código de saída — por isso
        // o teste afirma o LOG do stub, não só o exit).
        // Cada cadeia tem seu jeito de o primeiro passo falhar de verdade:
        //   ensureModDir  -> mkdir sob um ARQUIVO (não é diretório)
        //   writeTextFile -> cp com origem inexistente
        //   copyFile      -> cp -f com origem inexistente
        //   toggleMod     -> mv com origem inexistente
        String[] nomes = {"ensureModDir", "writeTextFile", "copyFile", "toggleMod"};
        for (String nome : nomes) {
            File ffRoot = tempDir("firstfail-" + nome);
            File ffBin = new File(ffRoot, "bin");
            File ffLog = new File(ffRoot, "chcon.log");
            String ffPath = pathWithChconStub(ffBin, ffLog, "0");
            String cmd;
            if ("ensureModDir".equals(nome)) {
                File arquivo = new File(ffRoot, "bloqueio");
                write(arquivo, "sou um arquivo, nao um diretorio");
                String dirAlvo = new File(arquivo, "com.foo.jogo").getPath();
                cmd = SuHelper.ensureModDirCommand(dirAlvo);
            } else if ("writeTextFile".equals(nome)) {
                File inexistente = new File(ffRoot, "nao-existe.tmp");
                cmd = SuHelper.writeTextFileCommand(inexistente.getPath(),
                        new File(ffRoot, "destino").getPath());
            } else if ("copyFile".equals(nome)) {
                File inexistente = new File(ffRoot, "nao-existe.so");
                cmd = SuHelper.copyFileCommand(inexistente.getPath(),
                        new File(ffRoot, "destino.so").getPath(), "644");
            } else {
                File inexistente = new File(ffRoot, "nao-existe.so");
                cmd = SuHelper.toggleModCommand(inexistente.getPath(),
                        new File(ffRoot, "alvo.so.off").getPath());
            }

            Result rr = shWithPath(cmd, ffPath, ffRoot);
            check(nome + ": 1o comando falhando => cadeia sai != 0", rr.code != 0);
            check(nome + ": o chcon NÃO foi chamado (log do stub vazio)",
                    chconLog(ffLog).trim().isEmpty());
            check(nome + ": o Result do SuHelper traduz isso em falha (success == false)",
                    !new SuHelper.Result(rr.code, rr.out, "", null).success);
        }

        // e a variante `;` de verdade, para documentar o que o && evita
        String semiChain = SuHelper.copyFileCommand(src6.getPath(), dest6, "644")
                .replace("644 '" + dest6 + "' && chcon", "644 '" + dest6 + "'; chcon");
        Result semi = shWithPathEnv(semiChain, path6, root6, "BEP_STUB_CHMOD_FAIL", "1");
        check("com `;` no lugar do `&&`, o chcon roda e a cadeia sai 0 (o bug)",
                semi.code == 0 && chconLog(chconLog6).contains(dest6));
    }

    private static void testDownloadFileListingAndCopy() {
        File root = tempDir("download-picker");
        File download = new File(root, "Download");
        File documents = new File(root, "Documents");
        check("pasta Download temporária criada", download.mkdirs());
        check("pasta Documents temporária criada", documents.mkdirs());

        File normal = new File(download, "mod com espaco.so");
        write(normal, "arquivo normal");
        File hidden = new File(download, ".hidden-mod.patch");
        write(hidden, "arquivo oculto");
        String hostileName = "quote ' $(touch injected); hostile.so";
        File hostile = new File(documents, hostileName);
        write(hostile, "conteudo-hostil");
        File outside = new File(root, "outside.so");
        write(outside, "fora das pastas compartilhadas");
        File symlink = new File(documents, "atalho.so");
        try {
            Files.createSymbolicLink(symlink.toPath(), outside.toPath());
        } catch (Exception e) {
            throw new AssertionError("não consegui criar link simbólico de teste: " + e);
        }

        String listingCommand = DownloadFilePicker.listCommand(download.getPath(), documents.getPath());
        Result listing = sh(listingCommand, root);
        check("listagem root executa em sh", listing.code == 0);
        List<String> paths = DownloadFilePicker.parseListing(listing.out,
                download.getPath(), documents.getPath());
        check("arquivo com espaço aparece intacto", paths.contains(normal.getPath()));
        check("arquivo oculto também aparece", paths.contains(hidden.getPath()));
        check("nome com aspas, $(), e ; aparece intacto", paths.contains(hostile.getPath()));
        check("links simbólicos não são listados", !paths.contains(symlink.getPath()));
        check("nome hostil não executou substituição de comando",
                !new File(root, "injected").exists());

        File copied = new File(root, "copied.bin");
        write(copied, "placeholder");
        try {
            Files.setPosixFilePermissions(copied.toPath(), Set.of(
                    PosixFilePermission.OWNER_READ, PosixFilePermission.OWNER_WRITE));
        } catch (Exception e) {
            throw new AssertionError("não consegui proteger o arquivo temporário: " + e);
        }
        Object inodeBefore = unixAttribute(copied, "ino");
        String copyCommand = DownloadFilePicker.copyToCacheCommand(hostile.getPath(),
                copied.getPath(), download.getPath(), documents.getPath());
        Result copy = sh(copyCommand, root);
        check("cópia root do nome hostil sai com código 0", copy.code == 0);
        check("cópia mantém o inode temporário criado pelo app",
                inodeBefore.equals(unixAttribute(copied, "ino")));
        check("cópia deixa o arquivo privado", (int) unixAttribute(copied, "mode") % 4096 == 384);
        check("cópia mantém o conteúdo exato",
                "conteudo-hostil".equals(new String(readBytes(copied), StandardCharsets.UTF_8)));
        check("nome hostil não executou comando durante a cópia",
                !new File(root, "injected").exists());
        File symlinkCopy = new File(root, "symlink-copy.bin");
        Result symlinkCopyResult = sh(DownloadFilePicker.copyToCacheCommand(symlink.getPath(),
                symlinkCopy.getPath(), download.getPath(), documents.getPath()), root);
        check("cópia revalida e recusa links simbólicos", symlinkCopyResult.code != 0);

        File empty = new File(root, "Empty");
        check("pasta vazia criada", empty.mkdirs());
        Result emptyListing = sh(DownloadFilePicker.listCommand(empty.getPath(),
                new File(root, "Absent").getPath()));
        check("pasta vazia e pasta ausente não causam erro", emptyListing.code == 0
                && DownloadFilePicker.parseListing(emptyListing.out, empty.getPath(),
                new File(root, "Absent").getPath()).isEmpty());
        Result missingListing = sh(DownloadFilePicker.listCommand(
                new File(root, "MissingDownload").getPath(),
                new File(root, "MissingDocuments").getPath()));
        check("ambas as pastas ausentes retornam lista vazia", missingListing.code == 0
                && DownloadFilePicker.parseListing(missingListing.out,
                new File(root, "MissingDownload").getPath(),
                new File(root, "MissingDocuments").getPath()).isEmpty());
    }

    private static byte[] readBytes(File file) {
        try {
            return Files.readAllBytes(file.toPath());
        } catch (Exception e) {
            throw new AssertionError("não consegui ler " + file + ": " + e);
        }
    }

    private static Object unixAttribute(File file, String attribute) {
        try {
            return Files.getAttribute(file.toPath(), "unix:" + attribute);
        } catch (Exception e) {
            throw new AssertionError("não consegui ler atributo unix:" + attribute + ": " + e);
        }
    }

    /** Como shWithPath, com uma env extra no ambiente do processo. */
    private static Result shWithPathEnv(String cmd, String path, File cwd, String key, String value) {
        try {
            ProcessBuilder pb = new ProcessBuilder(Arrays.asList("sh", "-c", cmd));
            pb.environment().put("PATH", path);
            pb.environment().put(key, value);
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
            Result r = new Result();
            r.code = proc.waitFor();
            r.out = new String(bo.toByteArray(), StandardCharsets.UTF_8);
            return r;
        } catch (Exception e) {
            throw new AssertionError("falha ao executar com env custom: " + e);
        }
    }

    // ---------- 7) probe do sistema -----------------------------------------

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
