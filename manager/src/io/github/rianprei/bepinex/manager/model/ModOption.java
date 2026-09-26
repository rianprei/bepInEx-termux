package io.github.rianprei.bepinex.manager.model;

import java.util.ArrayList;
import java.util.List;

// Opcao configuravel de um mod (Contrato C2).
public class ModOption {
    public String key;
    public String label;
    public String type; // bool | int | float | choice
    public Object defaultValue;
    public Double min;
    public Double max;
    public List<String> choices = new ArrayList<>();

    public ModOption() {}

    public ModOption(String key, String label, String type, Object defaultValue) {
        this.key = key;
        this.label = label;
        this.type = type;
        this.defaultValue = defaultValue;
    }
}
