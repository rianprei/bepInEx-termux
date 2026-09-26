package io.github.rianprei.bepinex.manager.core;

import io.github.rianprei.bepinex.manager.model.ModManifest;
import io.github.rianprei.bepinex.manager.model.ModOption;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.regex.Pattern;

// Parser e validador de manifest.json (Contrato C2).
public final class ManifestParser {
    private static final Pattern ID_PATTERN = Pattern.compile("^[a-z0-9-]{3,48}$");

    private ManifestParser() {}

    @SuppressWarnings("unchecked")
    public static ModManifest parse(String json) {
        if (json == null || json.trim().isEmpty()) {
            throw new IllegalArgumentException("Manifest vazio");
        }
        Map<String, Object> map = MiniJson.parseObject(json);

        ModManifest manifest = new ModManifest();

        Object formatObj = map.get("format");
        if (formatObj instanceof Number) {
            manifest.format = ((Number) formatObj).intValue();
        } else {
            throw new IllegalArgumentException("Campo 'format' ausente ou invalido");
        }
        if (manifest.format != 1) {
            throw new IllegalArgumentException("Versao de formato nao suportada: " + manifest.format);
        }

        manifest.id = (String) map.get("id");
        if (manifest.id == null || !ID_PATTERN.matcher(manifest.id).matches()) {
            throw new IllegalArgumentException("ID invalido (deve ter 3-48 caracteres [a-z0-9-]): " + manifest.id);
        }

        manifest.name = (String) map.get("name");
        if (manifest.name == null || manifest.name.trim().isEmpty()) {
            throw new IllegalArgumentException("Campo 'name' obrigatorio");
        }

        manifest.version = map.containsKey("version") ? String.valueOf(map.get("version")) : "1.0";
        manifest.author = map.containsKey("author") ? String.valueOf(map.get("author")) : "";
        manifest.description = map.containsKey("description") ? String.valueOf(map.get("description")) : "";

        manifest.game = (String) map.get("game");
        if (manifest.game == null || manifest.game.trim().isEmpty()) {
            throw new IllegalArgumentException("Campo 'game' obrigatorio (pacote ou '*')");
        }

        manifest.engine = (String) map.get("engine");
        if (manifest.engine == null || manifest.engine.trim().isEmpty()) {
            throw new IllegalArgumentException("Campo 'engine' obrigatorio");
        }

        manifest.type = (String) map.get("type");
        if (manifest.type == null || (!"patch".equals(manifest.type) && !"native".equals(manifest.type))) {
            throw new IllegalArgumentException("Campo 'type' deve ser 'patch' ou 'native': " + manifest.type);
        }

        Object optsObj = map.get("options");
        if (optsObj instanceof List) {
            List<?> list = (List<?>) optsObj;
            for (Object item : list) {
                if (item instanceof Map) {
                    Map<String, Object> optMap = (Map<String, Object>) item;
                    ModOption opt = new ModOption();
                    opt.key = (String) optMap.get("key");
                    opt.label = (String) optMap.get("label");
                    opt.type = (String) optMap.get("type");
                    opt.defaultValue = optMap.get("default");

                    if (optMap.get("min") instanceof Number) {
                        opt.min = ((Number) optMap.get("min")).doubleValue();
                    }
                    if (optMap.get("max") instanceof Number) {
                        opt.max = ((Number) optMap.get("max")).doubleValue();
                    }

                    Object choicesObj = optMap.get("choices");
                    if (choicesObj instanceof List) {
                        for (Object c : (List<?>) choicesObj) {
                            if (c != null) opt.choices.add(String.valueOf(c));
                        }
                    }
                    manifest.options.add(opt);
                }
            }
        }

        return manifest;
    }

    public static String toJson(ModManifest m) {
        Map<String, Object> map = new LinkedHashMap<>();
        map.put("format", m.format);
        map.put("id", m.id);
        map.put("name", m.name);
        map.put("version", m.version != null ? m.version : "1.0");
        map.put("author", m.author != null ? m.author : "");
        map.put("description", m.description != null ? m.description : "");
        map.put("game", m.game);
        map.put("engine", m.engine);
        map.put("type", m.type);

        if (m.options != null && !m.options.isEmpty()) {
            List<Map<String, Object>> opts = new ArrayList<>();
            for (ModOption opt : m.options) {
                Map<String, Object> om = new LinkedHashMap<>();
                om.put("key", opt.key);
                om.put("label", opt.label);
                om.put("type", opt.type);
                if (opt.defaultValue != null) om.put("default", opt.defaultValue);
                if (opt.min != null) om.put("min", opt.min);
                if (opt.max != null) om.put("max", opt.max);
                if (opt.choices != null && !opt.choices.isEmpty()) om.put("choices", opt.choices);
                opts.add(om);
            }
            map.put("options", opts);
        }

        return MiniJson.toJson(map);
    }
}
