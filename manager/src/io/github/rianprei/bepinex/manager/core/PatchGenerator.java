package io.github.rianprei.bepinex.manager.core;

import io.github.rianprei.bepinex.manager.model.PatchRule;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.StringReader;
import java.util.ArrayList;
import java.util.List;

// Gerador e analisador de arquivos .bpatch (Contrato C4).
public final class PatchGenerator {
    private PatchGenerator() {}

    // Conjunto de tipos do C4. Fechado de propósito: um tipo fora daqui é
    // rejeitado no Manager E no u_patch (mesma fixture, mesmo contrato).
    private static boolean isC4Type(String t) {
        return "bool".equals(t) || "int".equals(t) || "float".equals(t);
    }

    // nargs: só dígitos, >= 0 e <= 64 (o mesmo teto do up_parse_nargs do
    // u_patch). -1 volta como "inválido" e a linha é descartada.
    private static int parseNargsStrict(String tok) {
        if (tok == null || tok.isEmpty()) return -1;
        int n = 0;
        for (int i = 0; i < tok.length(); i++) {
            char c = tok.charAt(i);
            if (c < '0' || c > '9') return -1;
            n = n * 10 + (c - '0');
            if (n > 64) return -1;
        }
        return n;
    }

    public static List<PatchRule> parse(String content) {
        List<PatchRule> rules = new ArrayList<>();
        if (content == null) return rules;

        try (BufferedReader reader = new BufferedReader(new StringReader(content))) {
            String line;
            while ((line = reader.readLine()) != null) {
                line = line.trim();
                if (line.isEmpty() || line.startsWith("#")) continue;

                String[] parts = line.split("\\s+");
                if (parts.length < 5) continue;

                // C4: o número de campos é EXATO e o tipo sai do conjunto
                // fechado (bool|int|float). Antes o Manager aceitou token
                // extra, nargs negativo e tipo inventado — e o u_patch
                // (que segue a mesma gramática) recusava a linha: o Manager
                // mostrava a regra como pronta e o jogo ignorava. Agora os
                // dois lados leem test/fixtures/c4_lines.tsv e não há como
                // divergir sem o teste JVM/gráfico reclamar.
                String action = parts[0];
                if ("return".equals(action) || "mul".equals(action)) {
                    if (parts.length != 6) continue;
                    String targetClass = parts[1];
                    String method = parts[2];
                    int nargs = parseNargsStrict(parts[3]);
                    if (nargs < 0) continue;
                    String type = parts[4];
                    if (!isC4Type(type)) continue;
                    if ("mul".equals(action) && "bool".equals(type)) continue;  // mul só int|float
                    String value = parts[5];
                    if (value.isEmpty()) continue;
                    rules.add(new PatchRule(action, targetClass, method, nargs, type, value));
                } else if ("static".equals(action)) {
                    if (parts.length != 5) continue;
                    String targetClass = parts[1];
                    String field = parts[2];
                    String type = parts[3];
                    if (!isC4Type(type)) continue;
                    String value = parts[4];
                    if (value.isEmpty()) continue;
                    rules.add(PatchRule.makeStatic(targetClass, field, type, value));
                } else if ("field".equals(action)) {
                    // C4: field <Classe> <campo> <bool|int|float> <valor> [<Método> <nargs>]
                    // 5 tokens = sem método (u_patch escolhe); 7 = método explícito.
                    // 6 (ou 8+) é linha inválida e some, como o resto do parser.
                    if (parts.length == 5) {
                        if (!isC4Type(parts[3]) || parts[4].isEmpty()) continue;
                        rules.add(PatchRule.makeField(parts[1], parts[2], parts[3], parts[4]));
                    } else if (parts.length == 7) {
                        int nargs;
                        try {
                            nargs = Integer.parseInt(parts[6]);
                        } catch (NumberFormatException e) {
                            continue;
                        }
                        if (nargs < 0) continue;
                        if (!isC4Type(parts[3]) || parts[4].isEmpty()) continue;
                        rules.add(PatchRule.makeField(parts[1], parts[2], parts[3], parts[4], parts[5], nargs));
                    }
                }
            }
        } catch (IOException ignored) {}

        return rules;
    }

    public static String format(List<PatchRule> rules) {
        StringBuilder sb = new StringBuilder();
        sb.append("# bepInEx declarative patch (Contrato C4)\n");
        if (rules != null) {
            for (PatchRule rule : rules) {
                sb.append(rule.toLine()).append('\n');
            }
        }
        return sb.toString();
    }
}
