package io.github.rianprei.bepinex.manager.core;

import io.github.rianprei.bepinex.manager.model.PatchRule;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.StringReader;
import java.util.ArrayList;
import java.util.List;

// Gerador e analisador de arquivos .patch (Contrato C4).
public final class PatchGenerator {
    private PatchGenerator() {}

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

                String action = parts[0];
                if ("return".equals(action) || "mul".equals(action)) {
                    if (parts.length < 6) continue;
                    String targetClass = parts[1];
                    String method = parts[2];
                    int nargs;
                    try {
                        nargs = Integer.parseInt(parts[3]);
                    } catch (NumberFormatException e) {
                        continue;
                    }
                    String type = parts[4];
                    String value = parts[5];
                    rules.add(new PatchRule(action, targetClass, method, nargs, type, value));
                } else if ("static".equals(action)) {
                    String targetClass = parts[1];
                    String field = parts[2];
                    String type = parts[3];
                    String value = parts[4];
                    rules.add(PatchRule.makeStatic(targetClass, field, type, value));
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
