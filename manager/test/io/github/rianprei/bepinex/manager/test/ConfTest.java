package io.github.rianprei.bepinex.manager.test;

import io.github.rianprei.bepinex.manager.core.ConfManager;
import io.github.rianprei.bepinex.manager.model.ModManifest;
import io.github.rianprei.bepinex.manager.model.ModOption;

import java.util.LinkedHashMap;
import java.util.Map;

public class ConfTest {
    public static void run() {
        testParseAndFormat();
        testUpdatePreservingComments();
        testGetDefaults();
        System.out.println("  [OK] ConfTest (C3)");
    }

    private static void testParseAndFormat() {
        String conf = "# Comentario de cabecalho\n" +
                "mult=2.5\n" +
                "# Outro comentario\n" +
                "godmode=true\n" +
                "\n" +
                "ammo=999\n";

        Map<String, String> map = ConfManager.parse(conf);
        if (!"2.5".equals(map.get("mult"))) throw new AssertionError("mult incorreto: " + map.get("mult"));
        if (!"true".equals(map.get("godmode"))) throw new AssertionError("godmode incorreto");
        if (!"999".equals(map.get("ammo"))) throw new AssertionError("ammo incorreto");

        String formatted = ConfManager.format(map);
        if (!formatted.contains("mult=2.5") || !formatted.contains("godmode=true")) {
            throw new AssertionError("format falhou");
        }
    }

    private static void testUpdatePreservingComments() {
        String initial = "# Config inicial\nmult=2\ngodmode=false\n";
        Map<String, String> updates = new LinkedHashMap<>();
        updates.put("mult", "5");
        updates.put("extra", "novo");

        String updated = ConfManager.update(initial, updates);
        if (!updated.contains("# Config inicial")) throw new AssertionError("Comentario foi perdido");
        if (!updated.contains("mult=5")) throw new AssertionError("mult nao foi atualizado");
        if (!updated.contains("godmode=false")) throw new AssertionError("godmode foi perdido");
        if (!updated.contains("extra=novo")) throw new AssertionError("extra nao foi adicionado");
    }

    private static void testGetDefaults() {
        ModManifest m = new ModManifest();
        m.options.add(new ModOption("speed", "Velocidade", "float", 1.5));
        m.options.add(new ModOption("invulnerable", "Invencivel", "bool", true));

        Map<String, String> defaults = ConfManager.getDefaults(m);
        if (!"1.5".equals(defaults.get("speed"))) throw new AssertionError("default speed falhou");
        if (!"true".equals(defaults.get("invulnerable"))) throw new AssertionError("default invulnerable falhou");
    }
}
