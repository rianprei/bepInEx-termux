package io.github.rianprei.bepinex.manager.test;

import io.github.rianprei.bepinex.manager.core.ManifestParser;
import io.github.rianprei.bepinex.manager.core.DownloadFilePicker;
import io.github.rianprei.bepinex.manager.core.ModInventory;
import io.github.rianprei.bepinex.manager.core.StatusChecker;
import io.github.rianprei.bepinex.manager.core.SuHelper;
import io.github.rianprei.bepinex.manager.model.ModManifest;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * Contagem de chamadas root: o Manager roda como root e CADA `su` é um
 * processo novo. O travamento de 2026-09-27 foi exatamente isso — a tela de
 * jogos fazia uma listFiles por app instalado, e um celular com centenas de
 * apps esgotou a memória do aparelho.
 *
 * Aqui o numero de chamadas é medido com um fake: 300 apps instalados, e a
 * tela inicial tem que fazer no máximo 2 chamadas root no total (o status do
 * sistema + o inventário), com a contagem de chamadas INDEPENDENTE do número
 * de apps.
 */
public class RootCallBudgetTest {
    /** RootCall que conta e devolve uma saída fixa. */
    private static final class CountingRoot implements ModInventory.RootCall, StatusChecker.RootCall {
        int calls;
        String out;

        CountingRoot(String out) { this.out = out; }

        @Override public String exec() { calls++; return out; }
    }

    public static void run() {
        testInventoryUmaChamadaCom300Apps();
        testStatusCheckerUmaChamada();
        testContagemTelaInicial();
        testParseIgnoraLixo();
        testBundleDeManifests();
        testDownloadListingUmaChamada();
        System.out.println("  [OK] RootCallBudgetTest (orçamento de chamadas root)");
    }

    private static void check(String what, boolean cond) {
        TestRunner.check(what, cond);
    }

    private static List<String> apps(int n) {
        List<String> out = new ArrayList<>();
        for (int i = 0; i < n; i++) out.add("com.app" + i + ".jogo");
        return out;
    }

    private static String inventoryOut() {
        StringBuilder sb = new StringBuilder(ModInventory.command().length());
        sb.append("bepinex-mods-begin\n");
        for (int i = 0; i < 300; i++) {
            if (i % 7 == 0) sb.append("com.app").append(i).append(".jogo ").append(i % 3)
                    .append(" ").append(i % 2).append("\n");
        }
        sb.append("bepinex-mods-end\n");
        return sb.toString();
    }

    private static void testInventoryUmaChamadaCom300Apps() {
        List<String> pkgs = apps(300);
        List<String> allPkgs = apps(300);
        CountingRoot root = new CountingRoot(inventoryOut());
        Map<String, ModInventory.Counts> inv = ModInventory.inventory(pkgs, root);
        check("300 apps => UMA chamada root (era uma por app)", root.calls == 1);
        check("a contagem não depende do nº de apps: 10 apps dão 1 chamada também",
                callsFor(10) == 1);
        check("1 app dá 1 chamada", callsFor(1) == 1);
        check("0 apps dá 1 chamada (o comando sai vazio)", callsFor(0) == 1);
        // e os números conferem: o device reporta toda pasta que existe (mesmo
        // com 0 mods), e o que NÃO tem pasta não entra no mapa.
        ModInventory.Counts zero = inv.get("com.app0.jogo");   // i%3=0, i%2=0
        check("pasta com 0 mods entra com total=0 (o device viu a pasta)", zero != null
                && zero.total == 0 && zero.active == 0);
        ModInventory.Counts um = inv.get("com.app7.jogo");    // i%3=1, i%2=1
        check("pasta com 1 mod entra com total=1 e active=1", um != null
                && um.total == 1 && um.active == 1);
        check("app que não tem pasta não entra", !inv.containsKey("com.app1.jogo"));
        check("o filtro por lista não inventa pasta", inv.size() < allPkgs.size());
    }

    private static int callsFor(int n) {
        CountingRoot root = new CountingRoot(inventoryOut());
        ModInventory.inventory(apps(n), root);
        return root.calls;
    }

    private static void testStatusCheckerUmaChamada() {
        String raw = "B=bepinex-probe-begin\n"
                + "uid=0\n"
                + "module=/data/adb/modules/bepinex-termux\n"
                + "disable=enabled\n"
                + "zygisk=yes\n"
                + "magisk=27.0\n"
                + "B=bepinex-probe-end\n";
        CountingRoot root = new CountingRoot(raw);
        StatusChecker.SystemStatus st = StatusChecker.probe(root);
        check("status do sistema = UMA chamada root", root.calls == 1);
        check("root_ok vem do uid=0", st.rootOk);
        check("módulo instalado e ativo", st.moduleInstalled && st.moduleActive);
        check("zygisk ativo", st.zygiskActive);
        check("info do módulo traz o nome", st.moduleInfo.contains("bepinex-termux"));
        // desativado no Magisk
        StatusChecker.SystemStatus off = StatusChecker.probe(new CountingRoot(
                raw.replace("disable=enabled", "disable=disabled")));
        check("módulo desativado é respeitado", off.moduleInstalled && !off.moduleActive);
        // sem root
        StatusChecker.SystemStatus semRoot = StatusChecker.probe(new CountingRoot(
                raw.replace("uid=0", "uid=10123")));
        check("sem root: rootOk=false", !semRoot.rootOk);
        // saída vazia/nula não quebra
        StatusChecker.SystemStatus vazio = StatusChecker.probe(new CountingRoot(null));
        check("saída nula devolve o status padrão", !vazio.rootOk && !vazio.moduleInstalled);
    }

    private static void testContagemTelaInicial() {
        // A tela inicial = status do sistema (1) + inventário de todos os apps
        // (1). Nada mais pode abrir su durante o carregamento.
        CountingRoot status = new CountingRoot("B=bepinex-probe-begin\nuid=0\nB=bepinex-probe-end\n");
        CountingRoot inv = new CountingRoot(inventoryOut());
        StatusChecker.probe(status);
        ModInventory.inventory(apps(300), inv);
        int total = status.calls + inv.calls;
        check("tela inicial com 300 apps = 2 chamadas root", total == 2);
        check("nenhuma chamada por app sobrou", inv.calls == 1);
    }

    private static void testParseIgnoraLixo() {
        String raw = "WARNING: restricted method\n"
                + "bepinex-mods-begin\n"
                + "com.a.jogo 2 2\n"
                + "isso nao e uma linha de inventario\n"
                + "com.b.jogo 1 0\n"
                + "com..ruim -1 0\n"
                + "lixo; rm -rf / 1 1\n"
                + "com.c.jogo x y\n"
                + "bepinex-mods-end\n"
                + "depois do fim\n";
        Map<String, ModInventory.Counts> inv = ModInventory.parse(raw);
        check("linha válida entra", inv.containsKey("com.a.jogo") && inv.get("com.a.jogo").total == 2);
        check("segunda linha válida entra", inv.containsKey("com.b.jogo"));
        check("linha fora dos delimitadores é ignorada", inv.size() == 2);
        check("pkg malformado não entra", !inv.containsKey("lixo; rm -rf /"));
        check("números não numéricos não entram", inv.size() == 2);
        check("nulo devolve vazio", ModInventory.parse(null).isEmpty());
    }

    private static void testBundleDeManifests() {
        // O mesmo bug de "uma chamada por item" na tela de jogo: os .json dos
        // mods vêm de UMA chamada, delimitados por linha.
        String a = "{\"format\":1,\"id\":\"mod-a\",\"name\":\"Mod A\","
                + "\"game\":\"com.foo\",\"type\":\"patch\",\"engine\":\"unity-il2cpp\"}";
        String b = "{\"format\":1,\"id\":\"mod-b\",\"name\":\"Mod B\","
                + "\"game\":\"com.foo\",\"type\":\"patch\",\"engine\":\"unity-il2cpp\"}";
        String bundle = SuHelper.BUNDLE_SEP + "mod-a.json\n" + a + "\n"
                + SuHelper.BUNDLE_SEP + "mod-b.json\n" + b + "\n";
        Map<String, String> byName = ModInventory.parseBundle(bundle, SuHelper.BUNDLE_SEP);
        check("duas entradas no bundle", byName.size() == 2);
        check("mod-a.json traz o conteúdo", a.equals(byName.get("mod-a.json")));
        ModManifest m = ManifestParser.parse(byName.get("mod-b.json"));
        check("o conteúdo do bundle ainda é um manifest válido", m != null && "Mod B".equals(m.name));
        check("bundle vazio = mapa vazio",
                ModInventory.parseBundle("", SuHelper.BUNDLE_SEP).isEmpty());
    }

    private static void testDownloadListingUmaChamada() {
        final int[] calls = {0};
        DownloadFilePicker.Listing listing = DownloadFilePicker.list(command -> {
            calls[0]++;
            check("a listagem consulta Download e Documents no mesmo comando",
                    command.contains(DownloadFilePicker.DOWNLOAD_DIR)
                            && command.contains(DownloadFilePicker.DOCUMENTS_DIR));
            return "";
        });
        check("a listagem faz exatamente uma chamada root", calls[0] == 1);
        check("listagem vazia é válida", listing.success() && listing.paths.isEmpty());
    }
}
