package io.github.rianprei.bepinex.manager.model;

// Regra declarativa de patch (Contrato C4).
public class PatchRule {
    public String action;        // "return" | "mul" | "static"
    public String targetClass;   // "Namespace.Classe" ou "Classe"
    public String member;        // nome do metodo ou campo
    public int nargs = 0;        // apenas para return / mul
    public String valueType;     // "bool" | "int" | "float"
    public String value;         // literal ou "$key"

    public PatchRule() {}

    public PatchRule(String action, String targetClass, String member, int nargs, String valueType, String value) {
        this.action = action;
        this.targetClass = targetClass;
        this.member = member;
        this.nargs = nargs;
        this.valueType = valueType;
        this.value = value;
    }

    public static PatchRule makeReturn(String targetClass, String method, int nargs, String valueType, String value) {
        return new PatchRule("return", targetClass, method, nargs, valueType, value);
    }

    public static PatchRule makeMul(String targetClass, String method, int nargs, String valueType, String factor) {
        return new PatchRule("mul", targetClass, method, nargs, valueType, factor);
    }

    public static PatchRule makeStatic(String targetClass, String field, String valueType, String value) {
        return new PatchRule("static", targetClass, field, -1, valueType, value);
    }

    public String toLine() {
        if ("static".equals(action)) {
            return "static " + targetClass + " " + member + " " + valueType + " " + value;
        } else {
            return action + " " + targetClass + " " + member + " " + nargs + " " + valueType + " " + value;
        }
    }
}
