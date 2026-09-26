package io.github.rianprei.bepinex.manager.test;

import io.github.rianprei.bepinex.manager.core.ManifestParser;
import io.github.rianprei.bepinex.manager.model.ModManifest;
import io.github.rianprei.bepinex.manager.model.ModOption;

public class ManifestParserTest {
    public static void run() {
        testValidManifest();
        testInvalidId();
        testInvalidFormat();
        testInvalidType();
        testRoundTrip();
        testBmodCreateAndInspect();
        System.out.println("  [OK] ManifestParserTest (C2)");
    }

    private static void testValidManifest() {
        String json = "{\n" +
                "  \"format\": 1,\n" +
                "  \"id\": \"sa2-infinite-ammo\",\n" +
                "  \"name\": \"Munição infinita\",\n" +
                "  \"version\": \"1.0\",\n" +
                "  \"author\": \"fulano\",\n" +
                "  \"description\": \"Nunca acaba a munição.\",\n" +
                "  \"game\": \"com.hyperdotstudios.swampattack2\",\n" +
                "  \"engine\": \"unity-il2cpp\",\n" +
                "  \"type\": \"patch\",\n" +
                "  \"options\": [\n" +
                "    {\"key\": \"mult\", \"label\": \"Multiplicador\", \"type\": \"float\", \"default\": 2, \"min\": 1, \"max\": 10}\n" +
                "  ]\n" +
                "}";

        ModManifest m = ManifestParser.parse(json);
        if (m.format != 1) throw new AssertionError("format esperado 1, obteve " + m.format);
        if (!"sa2-infinite-ammo".equals(m.id)) throw new AssertionError("id incorreto");
        if (!"Munição infinita".equals(m.name)) throw new AssertionError("name incorreto");
        if (!"unity-il2cpp".equals(m.engine)) throw new AssertionError("engine incorreto");
        if (!"patch".equals(m.type)) throw new AssertionError("type incorreto");
        if (m.options.size() != 1) throw new AssertionError("options tamanho esperado 1");

        ModOption opt = m.options.get(0);
        if (!"mult".equals(opt.key)) throw new AssertionError("option key incorreta");
        if (opt.min != 1.0 || opt.max != 10.0) throw new AssertionError("min/max incorretos");
        if (!m.matchesGame("com.hyperdotstudios.swampattack2")) throw new AssertionError("game match falhou");
        if (m.matchesGame("com.outro.jogo")) throw new AssertionError("game match nao deveria bater");
        if (!m.matchesEngine("unity-il2cpp")) throw new AssertionError("engine match falhou");
    }

    private static void testInvalidId() {
        String json = "{\"format\": 1, \"id\": \"bad ID!\", \"name\": \"X\", \"game\": \"*\", \"engine\": \"native\", \"type\": \"patch\"}";
        try {
            ManifestParser.parse(json);
            throw new AssertionError("Deveria falhar para ID invalido");
        } catch (IllegalArgumentException expected) {}
    }

    private static void testInvalidFormat() {
        String json = "{\"format\": 2, \"id\": \"mod-test\", \"name\": \"X\", \"game\": \"*\", \"engine\": \"native\", \"type\": \"patch\"}";
        try {
            ManifestParser.parse(json);
            throw new AssertionError("Deveria falhar para format != 1");
        } catch (IllegalArgumentException expected) {}
    }

    private static void testInvalidType() {
        String json = "{\"format\": 1, \"id\": \"mod-test\", \"name\": \"X\", \"game\": \"*\", \"engine\": \"native\", \"type\": \"other\"}";
        try {
            ManifestParser.parse(json);
            throw new AssertionError("Deveria falhar para type != patch/native");
        } catch (IllegalArgumentException expected) {}
    }

    private static void testRoundTrip() {
        ModManifest m = new ModManifest();
        m.id = "my-test-mod";
        m.name = "Test Mod";
        m.game = "*";
        m.engine = "unity-il2cpp";
        m.type = "native";
        m.options.add(new ModOption("godmode", "Imortal", "bool", true));

        String json = ManifestParser.toJson(m);
        ModManifest parsed = ManifestParser.parse(json);
        if (!parsed.id.equals(m.id)) throw new AssertionError("Roundtrip id falhou");
        if (!parsed.name.equals(m.name)) throw new AssertionError("Roundtrip name falhou");
        if (parsed.options.size() != 1) throw new AssertionError("Roundtrip options falhou");
    }

    private static void testBmodCreateAndInspect() {
        try {
            ModManifest m = new ModManifest();
            m.id = "test-pack";
            m.name = "Test Pack Mod";
            m.game = "com.test.game";
            m.engine = "unity-il2cpp";
            m.type = "patch";

            java.io.File tmpDir = java.io.File.createTempFile("bmod_test_", "");
            tmpDir.delete();
            tmpDir.mkdirs();

            java.io.File bmod = io.github.rianprei.bepinex.manager.core.BmodInstaller.createBmod(
                    m, "return Player IsDead 0 bool false\n", true, tmpDir);

            if (!bmod.exists() || bmod.length() == 0) {
                throw new AssertionError(".bmod nao foi criado corretamente");
            }

            ModManifest read = io.github.rianprei.bepinex.manager.core.BmodInstaller.inspect(bmod);
            if (!read.id.equals(m.id)) throw new AssertionError("Inspecao do .bmod retornou id incorreto");
            if (!read.name.equals(m.name)) throw new AssertionError("Inspecao do .bmod retornou name incorreto");

            bmod.delete();
            tmpDir.delete();
        } catch (Exception e) {
            throw new RuntimeException(e);
        }
    }
}
