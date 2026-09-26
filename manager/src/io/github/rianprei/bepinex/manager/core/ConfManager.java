package io.github.rianprei.bepinex.manager.core;

import io.github.rianprei.bepinex.manager.model.ModManifest;
import io.github.rianprei.bepinex.manager.model.ModOption;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.StringReader;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

// Leitor e escritor de arquivos .conf (Contrato C3).
public final class ConfManager {
    private ConfManager() {}

    public static Map<String, String> parse(String content) {
        Map<String, String> map = new LinkedHashMap<>();
        if (content == null) return map;

        try (BufferedReader reader = new BufferedReader(new StringReader(content))) {
            String line;
            while ((line = reader.readLine()) != null) {
                line = line.trim();
                if (line.isEmpty() || line.startsWith("#")) continue;
                int eq = line.indexOf('=');
                if (eq > 0) {
                    String key = line.substring(0, eq).trim();
                    String val = line.substring(eq + 1).trim();
                    map.put(key, val);
                }
            }
        } catch (IOException ignored) {}

        return map;
    }

    public static String format(Map<String, String> values) {
        StringBuilder sb = new StringBuilder();
        if (values != null) {
            for (Map.Entry<String, String> e : values.entrySet()) {
                sb.append(e.getKey()).append('=').append(e.getValue()).append('\n');
            }
        }
        return sb.toString();
    }

    public static String update(String existingContent, Map<String, String> newValues) {
        if (existingContent == null || existingContent.trim().isEmpty()) {
            return format(newValues);
        }

        StringBuilder sb = new StringBuilder();
        Set<String> remaining = new LinkedHashSet<>(newValues != null ? newValues.keySet() : java.util.Collections.emptySet());

        try (BufferedReader reader = new BufferedReader(new StringReader(existingContent))) {
            String line;
            while ((line = reader.readLine()) != null) {
                String trimmed = line.trim();
                if (trimmed.isEmpty() || trimmed.startsWith("#")) {
                    sb.append(line).append('\n');
                    continue;
                }
                int eq = line.indexOf('=');
                if (eq > 0) {
                    String key = line.substring(0, eq).trim();
                    if (newValues != null && newValues.containsKey(key)) {
                        sb.append(key).append('=').append(newValues.get(key)).append('\n');
                        remaining.remove(key);
                    } else {
                        sb.append(line).append('\n');
                    }
                } else {
                    sb.append(line).append('\n');
                }
            }
        } catch (IOException ignored) {}

        for (String key : remaining) {
            sb.append(key).append('=').append(newValues.get(key)).append('\n');
        }

        return sb.toString();
    }

    public static Map<String, String> getDefaults(ModManifest manifest) {
        Map<String, String> map = new LinkedHashMap<>();
        if (manifest != null && manifest.options != null) {
            for (ModOption opt : manifest.options) {
                if (opt.key != null && opt.defaultValue != null) {
                    map.put(opt.key, String.valueOf(opt.defaultValue));
                }
            }
        }
        return map;
    }
}
