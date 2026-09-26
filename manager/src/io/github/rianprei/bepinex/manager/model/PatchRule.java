package io.github.rianprei.bepinex.manager.model;

// Regra declarativa de patch (Contrato C4).
public class PatchRule {
    // nargs = -1 marca "regra sem metodo": usado pelo verbo field quando o
    // proprio u_patch escolhe os metodos de instancia da classe.
    public static final int NO_METHOD = -1;

    public String action;        // "return" | "mul" | "static" | "field"
    public String targetClass;   // "Namespace.Classe" ou "Classe"
    public String member;        // nome do metodo ou campo
    public String method;        // so no verbo field: metodo de instancia (null = u_patch escolhe)
    public int nargs = 0;        // return/mul: nº de args; field: nº de args do metodo
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
        return new PatchRule("static", targetClass, field, NO_METHOD, valueType, value);
    }

    // C4: field <Classe> <campo> <bool|int|float> <valor> [<Método> <nargs>]
    // Sem metodo: o u_patch escolhe sozinho (ate 8 metodos de instancia da
    // classe que passam na guarda de tamanho).
    public static PatchRule makeField(String targetClass, String field, String valueType, String value) {
        return new PatchRule("field", targetClass, field, NO_METHOD, valueType, value);
    }

    public static PatchRule makeField(String targetClass, String field, String valueType, String value,
                                      String method, int nargs) {
        PatchRule r = new PatchRule("field", targetClass, field, nargs, valueType, value);
        r.method = method;
        return r;
    }

    public boolean hasMethod() {
        return method != null && !method.isEmpty() && nargs >= 0;
    }

    public String toLine() {
        if ("static".equals(action)) {
            return "static " + targetClass + " " + member + " " + valueType + " " + value;
        } else if ("field".equals(action)) {
            return hasMethod()
                    ? "field " + targetClass + " " + member + " " + valueType + " " + value + " " + method + " " + nargs
                    : "field " + targetClass + " " + member + " " + valueType + " " + value;
        } else {
            return action + " " + targetClass + " " + member + " " + nargs + " " + valueType + " " + value;
        }
    }
}
