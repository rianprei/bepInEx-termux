package io.github.rianprei.bepinex.manager.core;

import java.util.ArrayList;
import java.util.List;

/**
 * Tradutor de patches HarmonyX (Prefix/Postfix) para o formato C4 (.patch).
 * Só traduz padrões SIMPLES; qualquer outra coisa gera relatório com motivo.
 *
 * Tabela de opcodes: gerada via reflexão do dotnet (System.Reflection.Emit.OpCodes)
 * e commited como test/fixtures/dll2patch/opcodes_table.csv.
 */
public final class HarmonyTranslator {

    public record TranslationResult(List<String> patchLines, List<String> reportLines) {}

    // --- tabela de opcodes ECMA-335 (mesma do DllReader) ---
        // Tabela de opcodes ECMA-335 gerada via reflexão do dotnet.
    // Commited como test/fixtures/dll2patch/opcodes_table.csv.
    // Formato: nome,operandType,size
        // Tabela de opcodes ECMA-335 gerada via reflexão do dotnet.
    // Commited como test/fixtures/dll2patch/opcodes_table.csv.
    // Formato: nome,operandType,size
    private static final String[] OPCODE_TABLE = {
        "unknown_0x00,InlineNone,1",
        "unknown_0x01,InlineNone,1",
        "unknown_0x02,InlineNone,1",
        "ldarg.1,InlineNone,1",
        "ldarg.2,InlineNone,1",
        "ldarg.3,InlineNone,1",
        "ldloc.0,InlineNone,1",
        "ldloc.1,InlineNone,1",
        "ldloc.2,InlineNone,1",
        "ldloc.3,InlineNone,1",
        "stloc.0,InlineNone,1",
        "stloc.1,InlineNone,1",
        "stloc.2,InlineNone,1",
        "stloc.3,InlineNone,1",
        "ldarg.s,ShortInlineVar,1",
        "ldarga.s,ShortInlineVar,1",
        "starg.s,ShortInlineVar,1",
        "ldloc.s,ShortInlineVar,1",
        "ldloca.s,ShortInlineVar,1",
        "stloc.s,ShortInlineVar,1",
        "ldnull,InlineNone,1",
        "ldc.i4.m1,InlineNone,1",
        "ldc.i4.0,InlineNone,1",
        "ldc.i4.1,InlineNone,1",
        "ldc.i4.2,InlineNone,1",
        "ldc.i4.3,InlineNone,1",
        "ldc.i4.4,InlineNone,1",
        "ldc.i4.5,InlineNone,1",
        "ldc.i4.6,InlineNone,1",
        "ldc.i4.7,InlineNone,1",
        "ldc.i4.8,InlineNone,1",
        "ldc.i4.s,ShortInlineI,1",
        "ldc.i4,InlineI,1",
        "ldc.i8,InlineI8,1",
        "ldc.r4,ShortInlineR,1",
        "ldc.r8,InlineR,1",
        "unknown_0x24,InlineNone,1",
        "dup,InlineNone,1",
        "pop,InlineNone,1",
        "jmp,InlineMethod,1",
        "call,InlineMethod,1",
        "calli,InlineSig,1",
        "ret,InlineNone,1",
        "br.s,ShortInlineBrTarget,1",
        "brfalse.s,ShortInlineBrTarget,1",
        "brtrue.s,ShortInlineBrTarget,1",
        "beq.s,ShortInlineBrTarget,1",
        "bge.s,ShortInlineBrTarget,1",
        "bgt.s,ShortInlineBrTarget,1",
        "ble.s,ShortInlineBrTarget,1",
        "blt.s,ShortInlineBrTarget,1",
        "bne.un.s,ShortInlineBrTarget,1",
        "bge.un.s,ShortInlineBrTarget,1",
        "bgt.un.s,ShortInlineBrTarget,1",
        "ble.un.s,ShortInlineBrTarget,1",
        "blt.un.s,ShortInlineBrTarget,1",
        "br,InlineBrTarget,1",
        "brfalse,InlineBrTarget,1",
        "brtrue,InlineBrTarget,1",
        "beq,InlineBrTarget,1",
        "bge,InlineBrTarget,1",
        "bgt,InlineBrTarget,1",
        "ble,InlineBrTarget,1",
        "blt,InlineBrTarget,1",
        "bne.un,InlineBrTarget,1",
        "bge.un,InlineBrTarget,1",
        "bgt.un,InlineBrTarget,1",
        "ble.un,InlineBrTarget,1",
        "blt.un,InlineBrTarget,1",
        "switch,InlineSwitch,1",
        "ldind.i1,InlineNone,1",
        "ldind.u1,InlineNone,1",
        "ldind.i2,InlineNone,1",
        "ldind.u2,InlineNone,1",
        "ldind.i4,InlineNone,1",
        "ldind.u4,InlineNone,1",
        "ldind.i8,InlineNone,1",
        "ldind.i,InlineNone,1",
        "ldind.r4,InlineNone,1",
        "ldind.r8,InlineNone,1",
        "ldind.ref,InlineNone,1",
        "stind.ref,InlineNone,1",
        "stind.i1,InlineNone,1",
        "stind.i2,InlineNone,1",
        "stind.i4,InlineNone,1",
        "stind.i8,InlineNone,1",
        "stind.r4,InlineNone,1",
        "stind.r8,InlineNone,1",
        "add,InlineNone,1",
        "sub,InlineNone,1",
        "mul,InlineNone,1",
        "div,InlineNone,1",
        "div.un,InlineNone,1",
        "rem,InlineNone,1",
        "rem.un,InlineNone,1",
        "and,InlineNone,1",
        "or,InlineNone,1",
        "xor,InlineNone,1",
        "shl,InlineNone,1",
        "shr,InlineNone,1",
        "shr.un,InlineNone,1",
        "neg,InlineNone,1",
        "not,InlineNone,1",
        "conv.i1,InlineNone,1",
        "conv.i2,InlineNone,1",
        "conv.i4,InlineNone,1",
        "conv.i8,InlineNone,1",
        "conv.r4,InlineNone,1",
        "conv.r8,InlineNone,1",
        "conv.u4,InlineNone,1",
        "conv.u8,InlineNone,1",
        "callvirt,InlineMethod,1",
        "cpobj,InlineType,1",
        "ldobj,InlineType,1",
        "ldstr,InlineString,1",
        "newobj,InlineMethod,1",
        "castclass,InlineType,1",
        "isinst,InlineType,1",
        "conv.r.un,InlineNone,1",
        "unknown_0x77,InlineNone,1",
        "unknown_0x78,InlineNone,1",
        "unbox,InlineType,1",
        "throw,InlineNone,1",
        "ldfld,InlineField,1",
        "ldflda,InlineField,1",
        "stfld,InlineField,1",
        "ldsfld,InlineField,1",
        "ldsflda,InlineField,1",
        "stsfld,InlineField,1",
        "stobj,InlineType,1",
        "conv.ovf.i1.un,InlineNone,1",
        "conv.ovf.i2.un,InlineNone,1",
        "conv.ovf.i4.un,InlineNone,1",
        "conv.ovf.i8.un,InlineNone,1",
        "conv.ovf.u1.un,InlineNone,1",
        "conv.ovf.u2.un,InlineNone,1",
        "conv.ovf.u4.un,InlineNone,1",
        "conv.ovf.u8.un,InlineNone,1",
        "conv.ovf.i.un,InlineNone,1",
        "conv.ovf.u.un,InlineNone,1",
        "box,InlineType,1",
        "newarr,InlineType,1",
        "ldlen,InlineNone,1",
        "ldelema,InlineType,1",
        "ldelem.i1,InlineNone,1",
        "ldelem.u1,InlineNone,1",
        "ldelem.i2,InlineNone,1",
        "ldelem.u2,InlineNone,1",
        "ldelem.i4,InlineNone,1",
        "ldelem.u4,InlineNone,1",
        "ldelem.i8,InlineNone,1",
        "ldelem.i,InlineNone,1",
        "ldelem.r4,InlineNone,1",
        "ldelem.r8,InlineNone,1",
        "ldelem.ref,InlineNone,1",
        "stelem.i,InlineNone,1",
        "stelem.i1,InlineNone,1",
        "stelem.i2,InlineNone,1",
        "stelem.i4,InlineNone,1",
        "stelem.i8,InlineNone,1",
        "stelem.r4,InlineNone,1",
        "stelem.r8,InlineNone,1",
        "stelem.ref,InlineNone,1",
        "ldelem,InlineType,1",
        "stelem,InlineType,1",
        "unbox.any,InlineType,1",
        "unknown_0xa6,InlineNone,1",
        "unknown_0xa7,InlineNone,1",
        "unknown_0xa8,InlineNone,1",
        "unknown_0xa9,InlineNone,1",
        "unknown_0xaa,InlineNone,1",
        "unknown_0xab,InlineNone,1",
        "unknown_0xac,InlineNone,1",
        "unknown_0xad,InlineNone,1",
        "unknown_0xae,InlineNone,1",
        "unknown_0xaf,InlineNone,1",
        "unknown_0xb0,InlineNone,1",
        "unknown_0xb1,InlineNone,1",
        "unknown_0xb2,InlineNone,1",
        "conv.ovf.i1,InlineNone,1",
        "conv.ovf.u1,InlineNone,1",
        "conv.ovf.i2,InlineNone,1",
        "conv.ovf.u2,InlineNone,1",
        "conv.ovf.i4,InlineNone,1",
        "conv.ovf.u4,InlineNone,1",
        "conv.ovf.i8,InlineNone,1",
        "conv.ovf.u8,InlineNone,1",
        "unknown_0xbb,InlineNone,1",
        "unknown_0xbc,InlineNone,1",
        "unknown_0xbd,InlineNone,1",
        "unknown_0xbe,InlineNone,1",
        "unknown_0xbf,InlineNone,1",
        "unknown_0xc0,InlineNone,1",
        "unknown_0xc1,InlineNone,1",
        "refanyval,InlineType,1",
        "ckfinite,InlineNone,1",
        "unknown_0xc4,InlineNone,1",
        "unknown_0xc5,InlineNone,1",
        "mkrefany,InlineType,1",
        "unknown_0xc7,InlineNone,1",
        "unknown_0xc8,InlineNone,1",
        "unknown_0xc9,InlineNone,1",
        "unknown_0xca,InlineNone,1",
        "unknown_0xcb,InlineNone,1",
        "unknown_0xcc,InlineNone,1",
        "unknown_0xcd,InlineNone,1",
        "unknown_0xce,InlineNone,1",
        "unknown_0xcf,InlineNone,1",
        "ldtoken,InlineTok,1",
        "conv.u2,InlineNone,1",
        "conv.u1,InlineNone,1",
        "conv.i,InlineNone,1",
        "conv.ovf.i,InlineNone,1",
        "conv.ovf.u,InlineNone,1",
        "add.ovf,InlineNone,1",
        "add.ovf.un,InlineNone,1",
        "mul.ovf,InlineNone,1",
        "mul.ovf.un,InlineNone,1",
        "sub.ovf,InlineNone,1",
        "sub.ovf.un,InlineNone,1",
        "endfinally,InlineNone,1",
        "leave,InlineBrTarget,1",
        "leave.s,ShortInlineBrTarget,1",
        "stind.i,InlineNone,1",
        "conv.u,InlineNone,1",
        "unknown_0xe1,InlineNone,1",
        "unknown_0xe2,InlineNone,1",
        "unknown_0xe3,InlineNone,1",
        "unknown_0xe4,InlineNone,1",
        "unknown_0xe5,InlineNone,1",
        "unknown_0xe6,InlineNone,1",
        "unknown_0xe7,InlineNone,1",
        "unknown_0xe8,InlineNone,1",
        "unknown_0xe9,InlineNone,1",
        "unknown_0xea,InlineNone,1",
        "unknown_0xeb,InlineNone,1",
        "unknown_0xec,InlineNone,1",
        "unknown_0xed,InlineNone,1",
        "unknown_0xee,InlineNone,1",
        "unknown_0xef,InlineNone,1",
        "unknown_0xf0,InlineNone,1",
        "unknown_0xf1,InlineNone,1",
        "unknown_0xf2,InlineNone,1",
        "unknown_0xf3,InlineNone,1",
        "unknown_0xf4,InlineNone,1",
        "unknown_0xf5,InlineNone,1",
        "unknown_0xf6,InlineNone,1",
        "unknown_0xf7,InlineNone,1",
        "prefix7,InlineNone,1",
        "prefix6,InlineNone,1",
        "prefix5,InlineNone,1",
        "prefix4,InlineNone,1",
        "prefix3,InlineNone,1",
        "prefix2,InlineNone,1",
        "prefix1,InlineNone,1",
        "prefixref,InlineNone,1",
    };

    private static final String[][] PARSED_OPCODES = new String[256][];

    static {
        for (int i = 0; i < 256; i++) {
            PARSED_OPCODES[i] = OPCODE_TABLE[i].split(",");
        }
    }

    private HarmonyTranslator() {}

    public static TranslationResult translate(byte[] dllData) throws DllReader.DllReaderException {
        DllReader reader = DllReader.parse(dllData);
        List<String> patchLines = new ArrayList<>();
        List<String> reportLines = new ArrayList<>();

        List<DllReader.CustomAttributeInfo> attrs = reader.getCustomAttributes();
        for (DllReader.CustomAttributeInfo attr : attrs) {
            if (!"HarmonyPatch".equals(attr.typeName())) continue;
            if (!"class".equals(attr.targetKind())) continue;

            String targetClass = attr.targetName();
            for (DllReader.TypeInfo type : reader.getTypes()) {
                if (!type.name().equals(targetClass)) continue;
                for (DllReader.MethodInfo method : type.methods()) {
                    String mname = method.name();
                    if (!mname.startsWith("Prefix") && !mname.startsWith("Postfix")) continue;

                    String kind = mname.startsWith("Prefix") ? "Prefix" : "Postfix";
                    byte[] il = method.ilBody();
                    if (il.length == 0) {
                        reportLines.add(kind + " em " + targetClass + "." + mname + ": sem corpo IL (método abstrato ou sem implementação)");
                        continue;
                    }

                    List<DllReader.DecodedInstruction> instructions = DllReader.decodeIl(il);
                    String result = tryTranslatePattern(reader, targetClass, mname, kind, instructions, method.paramCount());
                    if (result != null) {
                        patchLines.add(result);
                    }
                }
            }
        }

        return new TranslationResult(patchLines, reportLines);
    }

    private static String tryTranslatePattern(DllReader reader, String targetClass, String methodName,
                                              String kind, List<DllReader.DecodedInstruction> instructions, int paramCount) {
        // Padrão A: Prefix com __result = CONST; return false;
        if ("Prefix".equals(kind)) {
            String result = tryPrefixResultConst(instructions, targetClass, methodName);
            if (result != null) return result;
        }

        // Padrão B: Postfix com __result = CONST;
        if ("Postfix".equals(kind)) {
            String result = tryPostfixResultConst(instructions, targetClass, methodName);
            if (result != null) return result;
        }

        // Padrão C: Postfix com __result *= K
        if ("Postfix".equals(kind)) {
            String result = tryPostfixMul(instructions, targetClass, methodName);
            if (result != null) return result;
        }

        // Padrão D: Prefix/Postfix com T.CampoEstatico = CONST
        {
            String result = tryStaticFieldAssign(reader, instructions, targetClass, methodName);
            if (result != null) return result;
        }

        return null;
    }

    private static String tryPrefixResultConst(List<DllReader.DecodedInstruction> ins, String cls, String method) {
        // Esperado: ldarg.0, ldc.i4.s/ldc.i4/ldc.r4, stind.i4/stind.r4, ldc.i4.0, ret
        if (ins.size() != 6) return null;
        if (!"ldarg.0".equals(ins.get(0).opcode())) return null;
        DllReader.DecodedInstruction load = ins.get(1);
        if (!isLdc(load)) return null;
        DllReader.DecodedInstruction store = ins.get(2);
        if (!isStind(store)) return null;
        if (!"ldc.i4.0".equals(ins.get(3).opcode())) return null; // return false
        if (!"ret".equals(ins.get(4).opcode())) return null;

        String type;
        String value;
        if (isLdcI4(load.opcode())) {
            type = "int";
            value = String.valueOf(load.operand());
        } else if ("ldc.r4".equals(load.opcode())) {
            type = "float";
            value = String.valueOf(Float.intBitsToFloat((int) load.operand()));
        } else {
            return null;
        }

        return "return " + cls + " " + method + " 0 " + type + " " + value;
    }

    private static String tryPostfixResultConst(List<DllReader.DecodedInstruction> ins, String cls, String method) {
        // Esperado: ldarg.0, ldc.i4.s/ldc.i4/ldc.r4, stind.i4/stind.r4, ret
        if (ins.size() != 5) return null;
        if (!"ldarg.0".equals(ins.get(0).opcode())) return null;
        DllReader.DecodedInstruction load = ins.get(1);
        if (!isLdc(load)) return null;
        DllReader.DecodedInstruction store = ins.get(2);
        if (!isStind(store)) return null;
        if (!"ret".equals(ins.get(3).opcode())) return null;

        String type;
        String value;
        if (isLdcI4(load.opcode())) {
            type = "int";
            value = String.valueOf(load.operand());
        } else if ("ldc.r4".equals(load.opcode())) {
            type = "float";
            value = String.valueOf(Float.intBitsToFloat((int) load.operand()));
        } else {
            return null;
        }

        return "return " + cls + " " + method + " 0 " + type + " " + value;
    }

    private static String tryPostfixMul(List<DllReader.DecodedInstruction> ins, String cls, String method) {
        // Esperado: ldarg.0, ldarg.0, ldc.i4/ldc.r4, mul, stind.i4/stind.r4, ret
        if (ins.size() != 7) return null;
        if (!"ldarg.0".equals(ins.get(0).opcode())) return null;
        if (!"ldarg.0".equals(ins.get(1).opcode())) return null;
        DllReader.DecodedInstruction load = ins.get(2);
        if (!isLdc(load)) return null;
        if (!"mul".equals(ins.get(3).opcode())) return null;
        DllReader.DecodedInstruction store = ins.get(4);
        if (!isStind(store)) return null;
        if (!"ret".equals(ins.get(5).opcode())) return null;

        String type;
        String value;
        if (isLdcI4(load.opcode())) {
            type = "int";
            value = String.valueOf(load.operand());
        } else if ("ldc.r4".equals(load.opcode())) {
            type = "float";
            value = String.valueOf(Float.intBitsToFloat((int) load.operand()));
        } else {
            return null;
        }

        return "mul " + cls + " " + method + " 0 " + type + " " + value;
    }

    private static String tryStaticFieldAssign(DllReader reader, List<DllReader.DecodedInstruction> ins, String cls, String method) {
        // Esperado: ldc.i4/ldc.r4, stsfld <field>, ret
        if (ins.size() != 4) return null;
        DllReader.DecodedInstruction load = ins.get(0);
        if (!isLdc(load)) return null;
        if (!"stsfld".equals(ins.get(1).opcode())) return null;
        if (!"ret".equals(ins.get(2).opcode())) return null;

        String type;
        String value;
        if (isLdcI4(load.opcode())) {
            type = "int";
            value = String.valueOf(load.operand());
        } else if ("ldc.r4".equals(load.opcode())) {
            type = "float";
            value = String.valueOf(Float.intBitsToFloat((int) load.operand()));
        } else {
            return null;
        }

        // Resolver nome do campo estático via token
        String fieldName = resolveFieldName(reader, ins.get(1).operand());
        if (fieldName == null) return null;

        return "static " + cls + " " + fieldName + " " + type + " " + value;
    }

    private static String resolveFieldName(DllReader reader, long token) {
        // Token de metadata para Field: tabela 0x04, RID = token >> 24
        int tableId = (int) (token >> 24);
        int rid = (int) (token & 0xFFFFFF);
        if (tableId != 0x04) return null;
        try {
            // Usar reflexão para acessar o método privado getFieldName
            var m = DllReader.class.getDeclaredMethod("getFieldName", int.class);
            m.setAccessible(true);
            return (String) m.invoke(reader, rid);
        } catch (Exception e) {
            return null;
        }
    }

    private static boolean isLdc(DllReader.DecodedInstruction ins) {
        return isLdcI4(ins.opcode()) || "ldc.r4".equals(ins.opcode()) || "ldc.r8".equals(ins.opcode());
    }

    private static boolean isLdcI4(String opcode) {
        return "ldc.i4".equals(opcode) || "ldc.i4.s".equals(opcode) || "ldc.i4.0".equals(opcode) ||
               "ldc.i4.1".equals(opcode) || "ldc.i4.2".equals(opcode) || "ldc.i4.3".equals(opcode) ||
               "ldc.i4.4".equals(opcode) || "ldc.i4.5".equals(opcode) || "ldc.i4.6".equals(opcode) ||
               "ldc.i4.7".equals(opcode) || "ldc.i4.8".equals(opcode) || "ldc.i4.m1".equals(opcode);
    }

    private static boolean isStind(DllReader.DecodedInstruction ins) {
        return "stind.i4".equals(ins.opcode()) || "stind.i8".equals(ins.opcode()) ||
               "stind.r4".equals(ins.opcode()) || "stind.r8".equals(ins.opcode());
    }
}
