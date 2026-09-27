package io.github.rianprei.bepinex.manager.core;

import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/** Traduz apenas corpos Harmony cujo efeito é exatamente representável por C4. */
public final class HarmonyTranslator {
    public record TranslationResult(List<String> patchLines, List<String> reportLines) {
        public String patchText() {
            StringBuilder text = new StringBuilder();
            if (!patchLines.isEmpty()) {
                text.append("# traduzido de Harmony para C4\n");
                for (String line : patchLines) text.append(line).append('\n');
            }
            return text.toString();
        }
    }

    private record Target(String className, String methodName) {}
    private record Candidate(DllReader.TypeInfo patchType, DllReader.MethodInfo patchMethod,
                             String kind, Target target) {}
    private record Constant(String c4Type, String value, String storeOpcode) {}

    private HarmonyTranslator() {}

    public static TranslationResult translate(byte[] dll) throws DllReader.DllReaderException {
        DllReader reader = DllReader.parse(dll);
        List<DllReader.TypeInfo> types = reader.getTypes();
        List<DllReader.CustomAttributeInfo> attributes = reader.getCustomAttributes();
        Map<Integer, Target> classTargets = new HashMap<>();
        Map<Integer, Target> methodTargets = new HashMap<>();
        Map<Integer, String> methodRoles = new HashMap<>();
        for (DllReader.CustomAttributeInfo attribute : attributes) {
            String name = simpleName(attribute.typeName());
            if ("HarmonyPatch".equals(name) && attribute.typeArgument() != null
                    && attribute.methodArgument() != null) {
                Target target = parseTarget(attribute.typeArgument(), attribute.methodArgument());
                if (attribute.parentTable() == 2) classTargets.put(attribute.parentRid(), target);
                if (attribute.parentTable() == 6) methodTargets.put(attribute.parentRid(), target);
            } else if (attribute.parentTable() == 6) {
                String role = switch (name) {
                    case "HarmonyPrefix" -> "Prefix";
                    case "HarmonyPostfix" -> "Postfix";
                    case "HarmonyTranspiler" -> "Transpiler";
                    default -> null;
                };
                if (role != null) methodRoles.put(attribute.parentRid(), role);
            }
        }

        List<Candidate> candidates = new ArrayList<>();
        for (DllReader.TypeInfo type : types) {
            Target classTarget = classTargets.get(type.rid());
            for (DllReader.MethodInfo method : type.methods()) {
                String role = methodRoles.get(method.rid());
                if (role == null) continue;
                Target target = methodTargets.getOrDefault(method.rid(), classTarget);
                if (target == null) continue;
                candidates.add(new Candidate(type, method, role, target));
            }
        }

        List<String> patchLines = new ArrayList<>();
        List<String> reports = new ArrayList<>();
        for (Candidate candidate : candidates) {
            String reportPrefix = candidate.patchType.fullName() + "." + candidate.patchMethod.name()
                    + " → " + candidate.target.className + "." + candidate.target.methodName + ": ";
            if ("Transpiler".equals(candidate.kind)) {
                reports.add(reportPrefix + "usa Transpiler (edita IL, não traduzível)");
                continue;
            }
            if (candidate.target.className.contains("/")) {
                reports.add(reportPrefix
                        + "classe aninhada: o u_patch ainda não localiza esse tipo de classe");
                continue;
            }
            DllReader.TypeInfo targetType = findType(types, candidate.target.className);
            if (targetType == null) {
                reports.add(reportPrefix + "assembly do método-alvo não está na DLL; não é possível confirmar overload");
                continue;
            }
            List<DllReader.MethodInfo> overloads = new ArrayList<>();
            for (DllReader.MethodInfo method : targetType.methods()) {
                if (method.name().equals(candidate.target.methodName)) overloads.add(method);
            }
            if (overloads.size() != 1) {
                reports.add(reportPrefix + (overloads.isEmpty()
                        ? "método-alvo não encontrado na DLL"
                        : "overload ambíguo: há " + overloads.size() + " métodos com esse nome"));
                continue;
            }
            DllReader.MethodInfo targetMethod = overloads.get(0);
            String refusal = translateOne(reader, targetType, targetMethod, candidate);
            if (refusal.startsWith("PATCH:")) patchLines.add(refusal.substring(6));
            else reports.add(reportPrefix + refusal);
        }
        return new TranslationResult(Collections.unmodifiableList(patchLines),
                Collections.unmodifiableList(reports));
    }

    private static String translateOne(DllReader reader, DllReader.TypeInfo targetType,
                                       DllReader.MethodInfo targetMethod, Candidate candidate)
            throws DllReader.DllReaderException {
        DllReader.MethodInfo patch = candidate.patchMethod;
        String targetClass = targetType.fullName();
        if (!safeC4Token(targetClass) || !safeC4Token(targetMethod.name())) {
            return "namespace, classe ou método contém caracteres que o C4 não aceita";
        }
        if ((patch.flags() & 0x0010) == 0) return "método Harmony não é static";
        if (patch.ilBody().length == 0) return "método sem corpo IL";
        for (DllReader.ParameterInfo parameter : patch.parameters()) {
            if ("__result".equals(parameter.name())
                    && !targetMethod.returnType().equals(valueType(parameter.type()))) {
                return "tipo de __result não coincide com o retorno do método-alvo";
            }
        }

        List<DllReader.DecodedInstruction> il = DllReader.decodeIl(patch.ilBody());
        if ("Prefix".equals(candidate.kind)) {
            String translated = tryPrefixReturn(targetClass, targetMethod, patch, il);
            if (translated != null) return "PATCH:" + translated;
        } else if ("Postfix".equals(candidate.kind)) {
            String translated = tryPostfixReturn(targetClass, targetMethod, patch, il);
            if (translated != null) return "PATCH:" + translated;
            translated = tryPostfixMul(targetClass, targetMethod, patch, il);
            if (translated != null) return "PATCH:" + translated;
        }
        String translated = tryStaticAssign(reader, targetClass, targetMethod, patch, candidate.kind, il);
        if (translated != null) {
            if (translated.startsWith("REFUSE:")) return translated.substring("REFUSE:".length());
            return "PATCH:" + translated;
        }
        return refusalReason(patch, candidate.kind, il);
    }

    private static String tryPrefixReturn(String cls, DllReader.MethodInfo target,
                                          DllReader.MethodInfo patch,
                                          List<DllReader.DecodedInstruction> il) {
        if (patch.parameters().size() != 1
                || !"__result".equals(patch.parameters().get(0).name())
                || !"bool".equals(patch.returnType()) || il.size() != 5
                || !isOpcode(il, 0, "ldarg.0") || !isOpcode(il, 2, storeFor(patch.parameters().get(0).type()))
                || !isOpcode(il, 3, "ldc.i4.0") || !isOpcode(il, 4, "ret")) {
            return null;
        }
        Constant constant = readConstant(il.get(1), valueType(patch.parameters().get(0).type()));
        if (constant == null || !constant.storeOpcode.equals(il.get(2).opcode())) return null;
        return "return " + cls + " " + target.name() + " " + target.paramCount()
                + " " + constant.c4Type + " " + constant.value;
    }

    private static String tryPostfixReturn(String cls, DllReader.MethodInfo target,
                                           DllReader.MethodInfo patch,
                                           List<DllReader.DecodedInstruction> il) {
        if (!"void".equals(patch.returnType()) || !hasResultParameter(patch)
                || il.size() != 4 || !isOpcode(il, 0, "ldarg.0")
                || !isOpcode(il, 3, "ret")) return null;
        Constant constant = readConstant(il.get(1), valueType(patch.parameters().get(0).type()));
        if (constant == null || !constant.storeOpcode.equals(il.get(2).opcode())) return null;
        return "return " + cls + " " + target.name() + " " + target.paramCount()
                + " " + constant.c4Type + " " + constant.value;
    }

    private static String tryPostfixMul(String cls, DllReader.MethodInfo target,
                                        DllReader.MethodInfo patch,
                                        List<DllReader.DecodedInstruction> il) {
        if (!"void".equals(patch.returnType()) || !hasResultParameter(patch) || il.size() != 7
                || !isOpcode(il, 0, "ldarg.0") || !isOpcode(il, 6, "ret")) return null;
        String resultType = valueType(patch.parameters().get(0).type());
        String load = "int".equals(resultType) ? "ldind.i4"
                : "float".equals(resultType) ? "ldind.r4" : null;
        if (load == null) return null;
        boolean prefixDup = isOpcode(il, 1, "dup") && isOpcode(il, 2, load);
        boolean repeatedAddress = isOpcode(il, 1, "ldarg.0") && isOpcode(il, 2, load);
        if (!prefixDup && !repeatedAddress) return null;
        if (!isOpcode(il, 4, "mul")
                || !isOpcode(il, 5, storeFor(patch.parameters().get(0).type()))) return null;
        Constant factor = readConstant(il.get(3), resultType);
        if (factor == null) return null;
        return "mul " + cls + " " + target.name() + " " + target.paramCount()
                + " " + factor.c4Type + " " + factor.value;
    }

    private static String tryStaticAssign(DllReader reader, String cls, DllReader.MethodInfo target,
                                          DllReader.MethodInfo patch, String kind,
                                          List<DllReader.DecodedInstruction> il)
            throws DllReader.DllReaderException {
        boolean postfixVoid = "Postfix".equals(kind) && "void".equals(patch.returnType());
        boolean prefixContinue = "Prefix".equals(kind) && "bool".equals(patch.returnType());
        int retIndex = postfixVoid ? 2 : 3;
        if ((!postfixVoid && !prefixContinue) || !patch.parameters().isEmpty()
                || il.size() != retIndex + 1 || !isOpcode(il, retIndex, "ret")
                || !isOpcode(il, 1, "stsfld")) return null;
        if (prefixContinue && !isOpcode(il, 2, "ldc.i4.1")) return null;
        DllReader.DecodedInstruction valueInstruction = il.get(0);
        DllReader.FieldReference field = reader.resolveField(il.get(1).operand());
        if (!field.isStatic() || !field.declaringType().equals(cls)) return null;
        Constant value = readConstant(valueInstruction, field.type());
        if (value == null) return null;
        return "REFUSE:cadência diferente: Harmony escreve a cada chamada; u_patch static escreve no carregamento e reaplica a cada 2s (até 32 regras)";
    }

    private static String refusalReason(DllReader.MethodInfo patch, String kind,
                                        List<DllReader.DecodedInstruction> il) {
        if (containsOpcode(il, "ldstr") || hasStringResult(patch)) {
            return "tipo string não suportado pelo u_patch";
        }
        for (DllReader.DecodedInstruction instruction : il) {
            if (isBranch(instruction.opcode())) return "tem if ou outro fluxo condicional";
        }
        for (DllReader.DecodedInstruction instruction : il) {
            if ("call".equals(instruction.opcode()) || "callvirt".equals(instruction.opcode())
                    || "calli".equals(instruction.opcode()) || "newobj".equals(instruction.opcode())) {
                return "chama outros métodos";
            }
        }
        if (containsOpcode(il, "ldsfld")) return "lê campo estático/configuração em vez de constante";
        if (containsOpcode(il, "ldc.r8") || hasDoubleResult(patch)) {
            return "tipo double não suportado pelo u_patch";
        }
        if ("Prefix".equals(kind) && containsOpcode(il, "ldc.i4.1")) {
            return "Prefix retorna true e deixa o método original executar";
        }
        return "corpo IL não corresponde exatamente a um padrão C4 suportado";
    }

    private static Constant readConstant(DllReader.DecodedInstruction instruction, String targetType) {
        if (instruction == null) return null;
        if ("bool".equals(targetType)) {
            Integer value = readIntConstant(instruction);
            if (value == null || (value != 0 && value != 1)) return null;
            return new Constant("bool", value == 0 ? "false" : "true", "stind.i1");
        }
        if ("int".equals(targetType)) {
            Integer value = readIntConstant(instruction);
            if (value == null) return null;
            return new Constant("int", Integer.toString(value), "stind.i4");
        }
        if ("float".equals(targetType) && "ldc.r4".equals(instruction.opcode())) {
            float value = Float.intBitsToFloat((int) instruction.operand());
            if (!Float.isFinite(value)) return null;
            return new Constant("float", Float.toString(value), "stind.r4");
        }
        return null;
    }

    private static Integer readIntConstant(DllReader.DecodedInstruction instruction) {
        return switch (instruction.opcode()) {
            case "ldc.i4.m1" -> -1;
            case "ldc.i4.0" -> 0;
            case "ldc.i4.1" -> 1;
            case "ldc.i4.2" -> 2;
            case "ldc.i4.3" -> 3;
            case "ldc.i4.4" -> 4;
            case "ldc.i4.5" -> 5;
            case "ldc.i4.6" -> 6;
            case "ldc.i4.7" -> 7;
            case "ldc.i4.8" -> 8;
            case "ldc.i4.s", "ldc.i4" -> (int) instruction.operand();
            default -> null;
        };
    }

    private static boolean hasResultParameter(DllReader.MethodInfo method) {
        return method.parameters().size() == 1
                && "__result".equals(method.parameters().get(0).name())
                && storeFor(method.parameters().get(0).type()) != null;
    }

    private static boolean hasStringResult(DllReader.MethodInfo method) {
        return method.parameters().stream().anyMatch(parameter -> "string&".equals(parameter.type()));
    }

    private static boolean hasDoubleResult(DllReader.MethodInfo method) {
        return method.parameters().stream().anyMatch(parameter -> "double&".equals(parameter.type()));
    }

    private static String storeFor(String type) {
        return switch (type) {
            case "bool&" -> "stind.i1";
            case "int&" -> "stind.i4";
            case "float&" -> "stind.r4";
            default -> null;
        };
    }

    private static String valueType(String type) {
        return type.endsWith("&") ? type.substring(0, type.length() - 1) : type;
    }

    private static boolean isOpcode(List<DllReader.DecodedInstruction> il, int index, String opcode) {
        return opcode != null && index >= 0 && index < il.size() && opcode.equals(il.get(index).opcode());
    }

    private static boolean containsOpcode(List<DllReader.DecodedInstruction> il, String opcode) {
        return il.stream().anyMatch(instruction -> opcode.equals(instruction.opcode()));
    }

    private static boolean isBranch(String opcode) {
        return opcode.startsWith("br") || opcode.startsWith("beq") || opcode.startsWith("bge")
                || opcode.startsWith("bgt") || opcode.startsWith("ble") || opcode.startsWith("blt")
                || opcode.startsWith("bne") || opcode.startsWith("switch") || opcode.startsWith("leave");
    }

    private static DllReader.TypeInfo findType(List<DllReader.TypeInfo> types, String fullName) {
        for (DllReader.TypeInfo type : types) {
            if (type.fullName().equals(fullName)) return type;
        }
        return null;
    }

    private static Target parseTarget(String rawType, String method) {
        String typeName = rawType.trim();
        int assembly = typeName.indexOf(',');
        if (assembly >= 0) typeName = typeName.substring(0, assembly).trim();
        if (typeName.startsWith("global::")) typeName = typeName.substring(8);
        typeName = typeName.replace('+', '/');
        return new Target(typeName, method.trim());
    }

    private static String simpleName(String name) {
        if (name == null) return "";
        int lastDot = name.lastIndexOf('.');
        String simple = lastDot < 0 ? name : name.substring(lastDot + 1);
        return simple.endsWith("Attribute") ? simple.substring(0, simple.length() - 9) : simple;
    }

    private static boolean safeC4Token(String token) {
        if (token == null || token.isEmpty()) return false;
        for (int i = 0; i < token.length(); i++) {
            char c = token.charAt(i);
            if (!(Character.isLetterOrDigit(c) || c == '_' || c == '.' || c == '/')) return false;
        }
        return true;
    }
}
