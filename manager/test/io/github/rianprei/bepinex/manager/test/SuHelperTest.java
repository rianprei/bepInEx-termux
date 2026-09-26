package io.github.rianprei.bepinex.manager.test;

import io.github.rianprei.bepinex.manager.core.SuHelper;

import java.io.File;

public class SuHelperTest {
    public static void run() {
        testPkgHostilRecusado();
        testNomeHostilRecusado();
        testChmodHostilRecusado();
        testCaminhoHostilRecusado();
        testValidosAceitos();
        System.out.println("  [OK] SuHelperTest (validacao central)");
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
        check("modsFile monta o caminho", "/data/local/tmp/mods/com.foo/meu.patch".equals(
                SuHelper.modsFile("com.foo", "meu.patch")));
        check("stateFile monta o caminho", "/data/data/com.foo/files/bepinex/crashguard".equals(
                SuHelper.stateFile("com.foo", "crashguard")));
        // Um cache local (caminho do proprio app) tambem e aceito: e o
        // src do copyFile, e nao vem de fora.
        String cache = new File(System.getProperty("java.io.tmpdir"), "bmod_x.tmp").getAbsolutePath();
        SuHelper.requirePath(cache, "tmp");
    }
}
