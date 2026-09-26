package io.github.rianprei.bepinex.manager.model;

// Entrada de classe, metodo ou campo no dump.tsv (Contrato C5).
public class DumpEntry {
    public static final String KIND_CLASS = "C";
    public static final String KIND_METHOD = "M";
    public static final String KIND_FIELD = "F";

    public String kind;        // "C" | "M" | "F"
    public String className;   // "Namespace.Classe"
    public String assembly;    // para C: nome do assembly (ex: Assembly-CSharp.dll)
    public String name;        // para M: metodo; para F: campo
    public int nargs = 0;      // para M: numero de argumentos
    public String type;        // para M: tipo_retorno; para F: tipo
    public boolean isStatic;   // para M e F: static 0|1
    public String offset;      // para F: offset hexadecimal ou decimal

    public String getDisplayName() {
        if (KIND_CLASS.equals(kind)) {
            return "[Classe] " + className;
        } else if (KIND_METHOD.equals(kind)) {
            return (isStatic ? "static " : "") + type + " " + className + "." + name + "(" + nargs + " args)";
        } else if (KIND_FIELD.equals(kind)) {
            return (isStatic ? "static " : "") + type + " " + className + "." + name + " (offset: " + offset + ")";
        }
        return className;
    }
}
