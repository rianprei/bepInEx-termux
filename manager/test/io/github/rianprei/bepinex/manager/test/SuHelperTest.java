package io.github.rianprei.bepinex.manager.test;

import io.github.rianprei.bepinex.manager.core.SuHelper;
import io.github.rianprei.bepinex.manager.core.TemporaryTextFile;

import java.io.File;
import java.nio.file.Files;

public class SuHelperTest {
    public static void run() {
        testPkgHostilRecusado();
        testNomeHostilRecusado();
        testChmodHostilRecusado();
        testCaminhoHostilRecusado();
        testActivityComponentValidation();
        testTemporaryTextFileCleanupOnInterruption();
        testValidosAceitos();
        testOwnerFixCommand();
        testReactivatePlan();
        System.out.println("  [OK] SuHelperTest (validacao central + dono do state dir)");
    }

    private static void check(String what, boolean cond) {
        if (!cond) throw new AssertionError("falhou: " + what);
    }

    private static void checkRejeita(String what, Runnable r) {
        try {
            r.run();
            throw new AssertionError("deveria ter lancado: " + what);
        } catch (IllegalArgumentException e) {
            check("recusa explica o motivo: " + what, e.getMessage() != null && !e.getMessage().isEmpty());
        }
    }

    private static void testPkgHostilRecusado() {
        // O caso do briefing: nome de pacote com '; reboot #' tentaria virar
        // um segundo comando dentro do su -c.
        checkRejeita("pkg com ponto e virgula", () -> SuHelper.requirePkg("a'; reboot #"));
        checkRejeita("pkg com aspas", () -> SuHelper.requirePkg("com.foo\" && id"));
        checkRejeita("pkg com barra", () -> SuHelper.requirePkg("com.foo/bar"));
        checkRejeita("pkg com $(...)", () -> SuHelper.requirePkg("com.$(id)"));
        checkRejeita("pkg com pipe", () -> SuHelper.requirePkg("com.foo|id"));
        checkRejeita("pkg com espaco", () -> SuHelper.requirePkg("com foo"));
        checkRejeita("pkg com traversal", () -> SuHelper.requirePkg("../../etc"));
        checkRejeita("pkg com ponto no fim", () -> SuHelper.requirePkg("com.foo."));
        checkRejeita("pkg comecando com ponto", () -> SuHelper.requirePkg(".com.foo"));
        checkRejeita("pkg com um so segmento", () -> SuHelper.requirePkg("foo"));
        checkRejeita("pkg vazio", () -> SuHelper.requirePkg(""));
        checkRejeita("pkg nulo", () -> SuHelper.requirePkg(null));
        checkRejeita("pkg com nova linha", () -> SuHelper.requirePkg("com.foo\nid"));
    }

    private static void testNomeHostilRecusado() {
        checkRejeita("nome com traversal", () -> SuHelper.requireFileName(".."));
        checkRejeita("nome com traversal dentro", () -> SuHelper.requireFileName("../../x"));
        checkRejeita("nome com barra", () -> SuHelper.requireFileName("a/b.so"));
        checkRejeita("nome com espaco", () -> SuHelper.requireFileName("a b"));
        checkRejeita("nome com ponto e virgula", () -> SuHelper.requireFileName("a.so; id"));
        checkRejeita("nome com $", () -> SuHelper.requireFileName("a$(id).so"));
        checkRejeita("nome vazio", () -> SuHelper.requireFileName(""));
        checkRejeita("nome nulo", () -> SuHelper.requireFileName(null));
        checkRejeita("nome com aspas", () -> SuHelper.requireFileName("a'.so"));
        // O que os helpers devolvem em vez de estourar: os de boolean
        // devolvem false e nada chega no su.
        check("deleteFile hostil nao executa", !SuHelper.deleteFile("/data/local/tmp/mods/com.foo/a; id"));
        check("toggleMod hostil nao executa", !SuHelper.toggleMod("com.foo", "a b.so", false));
        check("deleteMod hostil nao executa", !SuHelper.deleteMod("com.foo", ".."));
        check("ensureModDir com pkg hostil nao executa", !SuHelper.ensureModDir("a'; reboot #"));
    }

    private static void testChmodHostilRecusado() {
        // "777; id" tentaria rodar id como root logo depois do chmod.
        checkRejeita("chmod 777; id", () -> SuHelper.requireChmodMode("777; id"));
        checkRejeita("chmod com pipe", () -> SuHelper.requireChmodMode("644|id"));
        checkRejeita("chmod com aspas", () -> SuHelper.requireChmodMode("644' && id"));
        checkRejeita("chmod com $()", () -> SuHelper.requireChmodMode("$(id)"));
        checkRejeita("chmod com digito 8", () -> SuHelper.requireChmodMode("888"));
        checkRejeita("chmod curto demais", () -> SuHelper.requireChmodMode("64"));
        checkRejeita("chmod vazio", () -> SuHelper.requireChmodMode(""));
        checkRejeita("chmod nulo", () -> SuHelper.requireChmodMode(null));
        // copyFile/installFile devolvem false em vez de estourar.
        check("installFile com chmod hostil nao executa",
                !SuHelper.installFile("/tmp/a", "/data/local/tmp/mods/com.foo/a.so", "777; id"));
        check("copyFile com src hostil nao executa",
                !SuHelper.copyFile("/tmp/a; id", "/data/local/tmp/mods/com.foo/a.so", "644"));
        check("copyFile com dest hostil nao executa",
                !SuHelper.copyFile("/tmp/a", "/data/local/tmp/mods/com.foo/a.so && id", "644"));
    }

    private static void testCaminhoHostilRecusado() {
        checkRejeita("caminho relativo", () -> SuHelper.requirePath("mods/com.foo", "test"));
        checkRejeita("caminho com metacaractere", () -> SuHelper.requirePath("/data/local/tmp/a;id", "test"));
        checkRejeita("caminho com traversal", () -> SuHelper.requirePath("/data/local/tmp/../../etc", "test"));
        checkRejeita("caminho com espaco", () -> SuHelper.requirePath("/data/local/tmp/a b", "test"));
        checkRejeita("caminho com aspa", () -> SuHelper.requirePath("/data/local/tmp/a'b", "test"));
        checkRejeita("caminho gigante", () -> SuHelper.requirePath("/data/" + "x".repeat(600), "test"));
        checkRejeita("caminho nulo", () -> SuHelper.requirePath(null, "test"));
        check("readTextFile hostil devolve null",
                SuHelper.readTextFile("/data/local/tmp/mods/com.foo/a; id") == null);
        check("listFiles hostil devolve lista vazia",
                SuHelper.listFiles("/data/local/tmp/mods/$(id)").isEmpty());
        check("writeTextFile hostil nao executa",
                !SuHelper.writeTextFile("/data/local/tmp/mods/com.foo/a.conf; id", "x=1"));
    }

    private static void testActivityComponentValidation() {
        String valid = SuHelper.requireActivityComponent("com.example.game", "com.example.game/.MainActivity");
        check("componente relativo válido", valid.equals("com.example.game/.MainActivity"));
        check("componente aninhado válido",
                SuHelper.requireActivityComponent("com.example.game",
                        "com.example.game/com.example.game.Main$Home").endsWith("Main$Home"));
        checkRejeita("componente com espaço",
                () -> SuHelper.requireActivityComponent("com.example.game",
                        "com.example.game/.Main Activity"));
        checkRejeita("componente com ponto e vírgula",
                () -> SuHelper.requireActivityComponent("com.example.game",
                        "com.example.game/.Main;id"));
        checkRejeita("componente com substituição de comando",
                () -> SuHelper.requireActivityComponent("com.example.game",
                        "com.example.game/.$(id)"));
        checkRejeita("componente com duas linhas",
                () -> SuHelper.requireActivityComponent("com.example.game",
                        "com.example.game/.Main\ncom.attacker/.Run"));
        checkRejeita("componente com dois separadores",
                () -> SuHelper.requireActivityComponent("com.example.game",
                        "com.example.game/.Main/Other"));
        checkRejeita("componente de outro pacote",
                () -> SuHelper.requireActivityComponent("com.example.game",
                        "com.attacker/.Main"));
        String command = SuHelper.restartGameCommand("com.example.game", valid);
        check("comando passa componente inteiro entre aspas simples",
                command.contains("am start -n 'com.example.game/.MainActivity'"));
        check("comando não resolve atividade pelo shell",
                !command.contains("resolve-activity") && !command.contains("$("));
        // namespace da classe pode diferir do applicationId (o Android
        // permite launcher fora do namespace do pacote): com.foo/br.com.foo.Main
        // é launch válido e era recusado à toa.
        check("classe em namespace alheio ao pacote é aceita",
                "com.foo/br.com.foo.Main".equals(
                        SuHelper.requireActivityComponent("com.foo", "com.foo/br.com.foo.Main")));
        checkRejeita("classe em namespace alheio com metacaractere continua recusada",
                () -> SuHelper.requireActivityComponent("com.foo", "com.foo/br.com'.foo.Main;id"));
        checkRejeita("classe relativa estranha continua recusada",
                () -> SuHelper.requireActivityComponent("com.example.game", "com.example.game/."));
    }

    private static void testTemporaryTextFileCleanupOnInterruption() {
        File cache;
        try {
            cache = Files.createTempDirectory("bep-write-test-").toFile();
        } catch (Exception e) {
            throw new AssertionError("não conseguiu criar cache de teste: " + e);
        }
        try {
            boolean interrupted = false;
            try {
                TemporaryTextFile.write(cache, "name=value", tempPath -> {
                    check("temporário existe durante cópia falsa", new File(tempPath).isFile());
                    throw new IllegalStateException("exec interrompido");
                });
            } catch (IllegalStateException expected) {
                interrupted = true;
            } catch (java.io.IOException e) {
                throw new AssertionError("falha inesperada ao escrever temporário: " + e);
            }
            check("exec falso simulou interrupção", interrupted);
            String[] leftovers = cache.list((directory, name) -> name.startsWith("bep_su_write_")
                    && name.endsWith(".tmp"));
            check("interrupção não deixa bep_su_write_*.tmp no cache",
                    leftovers != null && leftovers.length == 0);
        } finally {
            File[] leftovers = cache.listFiles();
            if (leftovers != null) {
                for (File leftover : leftovers) leftover.delete();
            }
            cache.delete();
        }
    }

    /**
     * Causa raiz do "log.txt root:root" (rodada de device 2026-09-26): o
     * Manager roda como root e qualquer arquivo que ele crie em
     * /data/data/<pkg>/files fica root:root — aí o processo do jogo (outro
     * uid) não abre mais pra append e TODOS os mods ficam mudos. O fix é
     * devolver o dono do diretório pai depois de escrever.
     */
    private static void testOwnerFixCommand() {
        String cmd = SuHelper.ownerFixCommand("/data/data/com.foo/files/bepinex/log.txt");
        check("pega o uid:gig do PAIO com stat (toybox não tem chown --reference)",
                cmd.contains("stat -c %u:%g '/data/data/com.foo/files/bepinex'"));
        check("chown usa o resultado do stat no arquivo certo",
                cmd.contains("chown \"$(stat -c %u:%g") && cmd.contains("'/data/data/com.foo/files/bepinex/log.txt'"));
        check("devolve também o modo 0644", cmd.contains("chmod 644"));
        check("o caminho do contador também funciona",
                SuHelper.ownerFixCommand("/data/data/com.foo/files/bepinex/crashguard")
                        .contains("'/data/data/com.foo/files/bepinex/crashguard'"));
        // Caminho hostil não chega a montar comando: ensureOwner devolve false.
        check("ensureOwner com caminho hostil não executa",
                !SuHelper.ensureOwner("/data/data/com.foo/; id"));
    }

    /**
     * O invariante que quebrou o device: o Manager roda como root e, se criar
     * /data/data/<pkg>/files/bepinex, ela fica root:root 0755 — o app (outro
     * uid) só ganha r-x, o open do log dá EACCES e todos os mods ficam
     * mudos. Logo: o plano do Reativar NÃO pode conter mkdir.
     */
    private static void testReactivatePlan() {
        String semPasta = SuHelper.reactivateCommand("com.foo", false);
        check("sem state dir: plano é nulo (o app cria com o dono certo)", semPasta == null);
        String comPasta = SuHelper.reactivateCommand("com.foo", true);
        check("com state dir: plano existe", comPasta != null);
        check("NUNCA cria o state dir do app (seria root:root)",
                comPasta != null && !comPasta.contains("mkdir"));
        check("apaga os dois marcadores",
                comPasta != null && comPasta.contains("disabled_by_crashguard"));
        check("zera o contador", comPasta != null && comPasta.contains("crashguard"));
        check("não passa a senha do keystore em comando nenhum (não é build)",
                comPasta != null && !comPasta.contains("pass:"));
    }

    private static void testValidosAceitos() {
        // O caminho feliz nao pode quebrar: pacote, nome e modo validos
        // continuam aceitos e montando o path certo.
        check("pkg real aceito", "/data/local/tmp/mods/com.mixtilabs.monsterstrike2/".equals(
                SuHelper.modsDir("com.mixtilabs.monsterstrike2")));
        SuHelper.requirePkg("com.mixtilabs.monsterstrike2");
        SuHelper.requireFileName("meu_mod.so");
        SuHelper.requireFileName("u_frida.so.off");
        SuHelper.requireChmodMode("644");
        SuHelper.requireChmodMode("0644");
        SuHelper.requireChmodMode("755");
        SuHelper.requirePath("/data/data/com.foo/files/bepinex/log.txt", "log");
        check("modsFile monta o caminho", "/data/local/tmp/mods/com.foo/meu.bpatch".equals(
                SuHelper.modsFile("com.foo", "meu.bpatch")));
        check("stateFile monta o caminho", "/data/data/com.foo/files/bepinex/crashguard".equals(
                SuHelper.stateFile("com.foo", "crashguard")));
        // Um cache local (caminho do proprio app) tambem e aceito: e o
        // src do copyFile, e nao vem de fora.
        String cache = new File(System.getProperty("java.io.tmpdir"), "bmod_x.tmp").getAbsolutePath();
        SuHelper.requirePath(cache, "tmp");
    }
}
