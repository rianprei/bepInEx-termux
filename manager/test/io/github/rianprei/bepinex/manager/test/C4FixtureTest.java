package io.github.rianprei.bepinex.manager.test;

import io.github.rianprei.bepinex.manager.core.PatchGenerator;
import io.github.rianprei.bepinex.manager.model.PatchRule;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.List;

/**
 * Contrato C4 compartilhado: o MESMO arquivo de fixtures que o harness do
 * u_patch lê (test/fixtures/c4_lines.tsv). Cada linha é
 * {@code <regra> TAB <accept|reject>}, e os dois lados têm que concordar:
 * o Manager que gera a regra e o u_patch que a consome não podem discordar
 * sobre o que é uma linha válida — divergência aqui é um mod que o Manager
 * mostra como pronto e o jogo ignora (ou o contrário: um .bpatch que o jogo
 * aplica e o Manager nunca escreveu).
 *
 * <p>A fixture é a fonte da verdade, e ela vem do C4 do ROADMAP. Se um dos
 * lados divergir, corrige-se o lado errado e registra-se no C4 — não se
 * "acerta" a fixture para o código já passar.
 */
public class C4FixtureTest {
    public static void run() {
        File fixture = findFixture();
        if (fixture == null) {
            throw new AssertionError("fixture C4 não encontrada (procurei a partir de "
                    + System.getProperty("user.dir") + "); o Manager e o u_patch tem que ler o MESMO arquivo");
        }
        int lines = 0, divergencias = 0;
        try (BufferedReader r = new BufferedReader(new InputStreamReader(
                new FileInputStream(fixture), StandardCharsets.UTF_8))) {
            String raw;
            while ((raw = r.readLine()) != null) {
                String line = stripComment(raw);
                if (line.isEmpty()) continue;
                int tab = line.indexOf('\t');
                if (tab < 0) {
                    throw new AssertionError("linha da fixture sem TAB: '" + line + "'");
                }
                String rule = line.substring(0, tab);
                String expect = line.substring(tab + 1).trim();
                if (!expect.equals("accept") && !expect.equals("reject")) {
                    throw new AssertionError("fixture com esperado invalido (" + expect + "): '" + line + "'");
                }
                lines++;
                if (!matches(rule, expect)) divergencias++;
            }
        } catch (IOException e) {
            throw new AssertionError("erro de I/O na fixture C4: " + e);
        }
        System.out.println("  [INFO] fixture C4: " + lines + " linhas, " + divergencias + " divergencias");
        if (lines < 25) {
            throw new AssertionError("fixture C4 com " + lines + " linhas (esperado >= 25): "
                    + "o contrato encolheu sem ninguém avisar");
        }
        if (divergencias != 0) {
            throw new AssertionError("fixture C4: " + divergencias + " de " + lines
                    + " linhas divergem (a linha esta no erro de cada uma, acima)");
        }
        System.out.println("  [OK] C4FixtureTest (" + lines + " linhas, mesmo contrato do u_patch)");
    }

    /** Uma linha vale se o PatchGenerator a aceitar (produce 1 regra) e o esperado for accept. */
    private static boolean matches(String rule, String expect) {
        String[] parts = rule.trim().split("\\s+");
        List<PatchRule> out = PatchGenerator.parse(parts.length == 0 ? rule : rule);
        boolean accepted = out.size() == 1;
        boolean wantAccepted = expect.equals("accept");
        if (accepted == wantAccepted) return true;
        System.out.println("  [FAIL] fixture C4: '" + rule + "' esperava " + expect
                + ", o Manager aceitou=" + accepted);
        return false;
    }

    private static String stripComment(String line) {
        int hash = line.indexOf('#');
        if (hash >= 0) line = line.substring(0, hash);
        return line.trim();
    }

    /**
     * A fixture vive na raiz do repo; o teste roda de manager/ (run_tests.sh) ou
     * da raiz (verify_all). Sobe até 4 níveis procurando, em vez de fixar um
     * caminho que quebra quando alguém roda de outro lugar.
     */
    private static File findFixture() {
        String[] rel = {
            "test/fixtures/c4_lines.tsv",
            "../test/fixtures/c4_lines.tsv",
            "../../test/fixtures/c4_lines.tsv",
            "../../../test/fixtures/c4_lines.tsv",
        };
        File dir = new File(System.getProperty("user.dir", ".")).getAbsoluteFile();
        for (File d = dir; d != null; d = d.getParentFile()) {
            for (String r : rel) {
                File f = new File(d, r);
                if (f.isFile()) return f;
            }
        }
        return null;
    }
}
