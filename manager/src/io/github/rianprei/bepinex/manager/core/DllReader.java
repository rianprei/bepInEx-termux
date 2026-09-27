package io.github.rianprei.bepinex.manager.core;

import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/**
 * Parser mínimo de PE/CLI (ECMA-335) para assemblies .NET.
 * Lê metadata e corpos IL dos métodos. Sem dependência externa.
 * Entrada malformada lança {@link DllReaderException} com mensagem PT-BR.
 *
 * Tabela de opcodes: gerada via reflexão do dotnet (System.Reflection.Emit.OpCodes)
 * e commited como test/fixtures/dll2patch/opcodes_table.csv.
 */
public final class DllReader {

    public static final class DllReaderException extends Exception {
        public DllReaderException(String message) { super(message); }
    }

    // --- estruturas públicas ---
    public record MethodInfo(String name, int flags, int paramCount, byte[] ilBody, int rva) {}
    public record FieldInfo(String name, int flags) {}
    public record TypeInfo(String name, String namespace, List<MethodInfo> methods, List<FieldInfo> fields) {}
    public record CustomAttributeInfo(String typeName, String targetName, String targetKind) {}

    // --- tabela de opcodes ECMA-335 ---
    // Gerada via reflexão do dotnet. Value,Name,OperandType,Size
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

    // Parse da tabela de opcodes: nome -> [operandType, size]
    private static final String[][] PARSED_OPCODES = new String[256][];

    static {
        for (int i = 0; i < 256; i++) {
            PARSED_OPCODES[i] = OPCODE_TABLE[i].split(",");
        }
    }

    private final byte[] data;
    private final int peOffset;
    private final int optionalHeaderSize;
    private final int comDescriptorRva;
    private final int metadataRootRva;
    private final int metadataRootOffset;
    private int stringsOffset;
    private int blobOffset;
    private int usOffset;
    private int guidOffset;
    private final int[] tableRowCounts;
    private final long[] tableValid;
    private final int[] tableOffsets;
    private final int heapSizes;

    private DllReader(byte[] data) throws DllReaderException {
        this.data = data;
        this.peOffset = readInt32(0x3C);
        if (peOffset < 0 || peOffset + 4 > data.length)
            throw new DllReaderException("offset de PE header inválido: " + peOffset);
        if (readInt32(peOffset) != 0x00004550)
            throw new DllReaderException("não é um arquivo PE (assinatura PE não encontrada)");
        int coffOffset = peOffset + 4;
        int numSections = readInt16(coffOffset + 2);
        this.optionalHeaderSize = readInt16(coffOffset + 16);
        int optionalOffset = coffOffset + 20;
        if (optionalHeaderSize < 112)
            throw new DllReaderException("optional header muito pequeno: " + optionalHeaderSize);
        int ddOffset = optionalOffset + 96;
        this.comDescriptorRva = readInt32(ddOffset + 14 * 8);
        if (comDescriptorRva == 0)
            throw new DllReaderException("não é um assembly .NET (sem COM Descriptor / CLI header)");
        int comOffset = rvaToFileOffset(comDescriptorRva);
        int cliHeaderRva = readInt32(comOffset + 8);
        this.metadataRootRva = readInt32(comOffset + 12);
        this.metadataRootOffset = rvaToFileOffset(metadataRootRva);
        if (metadataRootOffset < 0)
            throw new DllReaderException("metadata root RVA inválido");
        if (readInt32(metadataRootOffset) != 0x424A5342)
            throw new DllReaderException("metadata root inválido (assinatura BSJB não encontrada)");
        int versionLen = readInt32(metadataRootOffset + 12);
        int streamsOffset = metadataRootOffset + 16 + versionLen;
        streamsOffset = (streamsOffset + 3) & ~3;
        int numStreams = readInt16(streamsOffset - 2);
        this.stringsOffset = -1;
        this.blobOffset = -1;
        this.usOffset = -1;
        this.guidOffset = -1;
        int off = streamsOffset;
        for (int i = 0; i < numStreams; i++) {
            int streamOffset = readInt32(off);
            int streamSize = readInt32(off + 4);
            int nameStart = off + 8;
            int nameEnd = nameStart;
            while (nameEnd < data.length && data[nameEnd] != 0) nameEnd++;
            String name = new String(data, nameStart, nameEnd - nameStart, StandardCharsets.UTF_8);
            int absOffset = metadataRootOffset + streamOffset;
            if ("#Strings".equals(name)) this.stringsOffset = absOffset;
            else if ("#Blob".equals(name)) this.blobOffset = absOffset;
            else if ("#US".equals(name)) this.usOffset = absOffset;
            else if ("#GUID".equals(name)) this.guidOffset = absOffset;
            off = nameEnd + 1;
            while ((off - streamsOffset) % 4 != 0) off++;
        }
        if (stringsOffset < 0 || blobOffset < 0)
            throw new DllReaderException("streams #Strings e/ou #Blob não encontrados");
        int tablesOffset = findTablesOffset();
        this.heapSizes = data[tablesOffset + 6];
        long valid = readInt64(tablesOffset + 8);
        this.tableValid = new long[64];
        for (int i = 0; i < 64; i++) tableValid[i] = (valid >> i) & 1;
        int rowCountsOffset = tablesOffset + 16;
        this.tableRowCounts = new int[64];
        for (int i = 0; i < 64; i++) {
            if (tableValid[i] == 1) {
                tableRowCounts[i] = readInt32(rowCountsOffset);
                rowCountsOffset += 4;
            }
        }
        this.tableOffsets = new int[64];
        for (int i = 0; i < 64; i++) {
            if (tableValid[i] == 1) {
                tableOffsets[i] = rowCountsOffset;
                rowCountsOffset += tableRowCounts[i] * tableRowSize(i);
            }
        }
    }

    private int findTablesOffset() throws DllReaderException {
        int versionLen = readInt32(metadataRootOffset + 12);
        int off = metadataRootOffset + 16 + versionLen;
        off = (off + 3) & ~3;
        int numStreams = readInt16(off - 2);
        int p = off;
        for (int i = 0; i < numStreams; i++) {
            int nameStart = p + 8;
            int nameEnd = nameStart;
            while (nameEnd < data.length && data[nameEnd] != 0) nameEnd++;
            String name = new String(data, nameStart, nameEnd - nameStart, StandardCharsets.UTF_8);
            if ("#~".equals(name)) return metadataRootOffset + readInt32(p);
            p = nameEnd + 1;
            while ((p - off) % 4 != 0) p++;
        }
        throw new DllReaderException("stream #~ não encontrado");
    }

    // Tipos de índice como constantes de string (evita problema de ordem de declaração de classes)
    private static final String TYPEDEF_OR_REF = "TypeDefOrRef";
    private static final String HAS_CUSTOM_ATTRIBUTE = "HasCustomAttribute";
    private static final String CUSTOM_ATTRIBUTE_TYPE = "CustomAttributeType";
    private static final String MEMBER_REF_PARENT = "MemberRefParent";
    private static final String RESOLUTION_SCOPE = "ResolutionScope";
    private static final String HAS_CONSTANT = "HasConstant";
    private static final String HAS_SEMANTICS = "HasSemantics";
    private static final String METHOD_DEF_OR_REF = "MethodDefOrRef";
    private static final String STRINGS = "Strings";
    private static final String BLOB = "Blob";
    private static final String GUID = "GUID";
    private static final String FIELD = "Field";
    private static final String METHOD_DEF = "MethodDef";
    private static final String PARAM = "Param";
    private static final String MEMBER_FORWARDED = "MemberForwarded";
    private static final String IMPLEMENTATION = "Implementation";
    private static final String HAS_FIELD_MARSHAL = "HasFieldMarshal";
    private static final String HAS_DECL_SECURITY = "HasDeclSecurity";

    private int tableRowSize(int tableIndex) throws DllReaderException {
        return switch (tableIndex) {
            case 0x00 -> 10;
            case 0x01 -> idxSize(TYPEDEF_OR_REF) + idxSize(RESOLUTION_SCOPE);
            case 0x02 -> 4 + idxSize(STRINGS) * 2 + idxSize(TYPEDEF_OR_REF) + idxSize(FIELD) + idxSize(METHOD_DEF);
            case 0x04 -> 2 + idxSize(STRINGS) + idxSize(BLOB);
            case 0x06 -> 4 + 2 + 2 + idxSize(STRINGS) + idxSize(BLOB) + idxSize(PARAM);
            case 0x08 -> 2 + 2 + idxSize(STRINGS);
            case 0x09 -> 2 + idxSize(METHOD_DEF_OR_REF);
            case 0x0A -> idxSize(MEMBER_REF_PARENT) + idxSize(STRINGS) + idxSize(BLOB);
            case 0x0B -> 1 + 1 + idxSize(HAS_CONSTANT) + idxSize(BLOB);
            case 0x0C -> idxSize(HAS_CUSTOM_ATTRIBUTE) + idxSize(CUSTOM_ATTRIBUTE_TYPE) + idxSize(BLOB);
            case 0x20 -> 4 + 2 + 2 + idxSize(STRINGS) + idxSize(STRINGS) + idxSize(STRINGS) + idxSize(BLOB);
            case 0x1B -> 1 + 1 + idxSize(HAS_SEMANTICS);
            default -> throw new DllReaderException("tabela 0x" + Integer.toHexString(tableIndex) + " não suportada");
        };
    }

    private int idxSize(String type) throws DllReaderException {
        if (TYPEDEF_OR_REF.equals(type)) return 2;
        if (HAS_CUSTOM_ATTRIBUTE.equals(type)) return 4;
        if (CUSTOM_ATTRIBUTE_TYPE.equals(type)) return 2;
        if (MEMBER_REF_PARENT.equals(type)) return 2;
        if (RESOLUTION_SCOPE.equals(type)) return 2;
        if (HAS_CONSTANT.equals(type)) return 2;
        if (HAS_SEMANTICS.equals(type)) return 2;
        if (METHOD_DEF_OR_REF.equals(type)) return 2;
        if (STRINGS.equals(type)) return (heapSizes & 1) != 0 ? 4 : 2;
        if (BLOB.equals(type)) return (heapSizes & 4) != 0 ? 4 : 2;
        if (GUID.equals(type)) return (heapSizes & 2) != 0 ? 4 : 2;
        if (FIELD.equals(type) || METHOD_DEF.equals(type) || PARAM.equals(type)) return 2;
        if (MEMBER_FORWARDED.equals(type)) return 2;
        if (IMPLEMENTATION.equals(type)) return 2;
        if (HAS_FIELD_MARSHAL.equals(type)) return 2;
        if (HAS_DECL_SECURITY.equals(type)) return 2;
        throw new DllReaderException("tipo de índice desconhecido: " + type);
    }

    private int readInt8(int off) { return data[off] & 0xFF; }
    private int readInt16(int off) {
        return (data[off] & 0xFF) | ((data[off + 1] & 0xFF) << 8);
    }
    private int readInt32(int off) {
        return (data[off] & 0xFF) | ((data[off + 1] & 0xFF) << 8) |
               ((data[off + 2] & 0xFF) << 16) | ((data[off + 3] & 0xFF) << 24);
    }
    private long readInt64(int off) {
        return (readInt32(off) & 0xFFFFFFFFL) | ((readInt32(off + 4) & 0xFFFFFFFFL) << 32);
    }

    private int rvaToFileOffset(int rva) {
        int coffOffset = peOffset + 4;
        int numSections = readInt16(coffOffset + 2);
        int optionalSize = readInt16(coffOffset + 16);
        int sectionOffset = coffOffset + 20 + optionalSize;
        for (int i = 0; i < numSections; i++) {
            int sOff = sectionOffset + i * 40;
            int virtualSize = readInt32(sOff + 8);
            int virtualAddress = readInt32(sOff + 12);
            int rawSize = readInt32(sOff + 16);
            int rawOffset = readInt32(sOff + 20);
            if (rva >= virtualAddress && rva < virtualAddress + virtualSize) {
                int delta = rva - virtualAddress;
                if (delta < rawSize) return rawOffset + delta;
            }
        }
        return -1;
    }

    private String getString(int index) throws DllReaderException {
        if (index < 0 || index >= data.length - stringsOffset)
            throw new DllReaderException("índice de #Strings fora dos limites: " + index);
        int abs = stringsOffset + index;
        if (abs >= data.length) throw new DllReaderException("string fora dos limites");
        int end = abs;
        while (end < data.length && data[end] != 0) end++;
        if (end >= data.length) throw new DllReaderException("string não terminada em null");
        return new String(data, abs, end - abs, StandardCharsets.UTF_8);
    }

    private byte[] getBlob(int index) throws DllReaderException {
        int abs = blobOffset + index;
        if (abs >= data.length) throw new DllReaderException("blob fora dos limites");
        int first = data[abs] & 0xFF;
        int len;
        int contentStart;
        if ((first & 0x80) == 0) {
            len = first;
            contentStart = abs + 1;
        } else if ((first & 0xC0) == 0x80) {
            len = ((first & 0x3F) << 8) | (data[abs + 1] & 0xFF);
            contentStart = abs + 2;
        } else {
            len = ((first & 0x1F) << 24) | ((data[abs + 1] & 0xFF) << 16) |
                  ((data[abs + 2] & 0xFF) << 8) | (data[abs + 3] & 0xFF);
            contentStart = abs + 4;
        }
        if (contentStart + len > data.length)
            throw new DllReaderException("blob além do fim do arquivo");
        return Arrays.copyOfRange(data, contentStart, contentStart + len);
    }

    private String[] getTypeName(int rid) throws DllReaderException {
        int base = tableOffsets[0x02];
        int rowSize = tableRowSize(0x02);
        int rowOff = base + (rid - 1) * rowSize;
        int nameIdx = readInt32(rowOff + 4);
        int nsIdx = readInt32(rowOff + 8);
        return new String[]{getString(nameIdx), getString(nsIdx)};
    }

    private String[] getRefName(int rid) throws DllReaderException {
        int base = tableOffsets[0x01];
        int rowSize = tableRowSize(0x01);
        int rowOff = base + (rid - 1) * rowSize;
        int nameIdx = readInt32(rowOff);
        int nsIdx = readInt32(rowOff + 4);
        return new String[]{getString(nameIdx), getString(nsIdx)};
    }

    private MethodInfo getMethod(int rid) throws DllReaderException {
        int base = tableOffsets[0x06];
        int rowSize = tableRowSize(0x06);
        int rowOff = base + (rid - 1) * rowSize;
        int rva = readInt32(rowOff);
        int flags = readInt16(rowOff + 8);
        int nameIdx = readInt32(rowOff + 12);
        String name = getString(nameIdx);
        int nextRid = rid + 1;
        int paramStart = readInt32(rowOff + 20);
        int paramEnd;
        if (tableValid[0x06] == 1 && nextRid <= tableRowCounts[0x06]) {
            int nextOff = base + nextRid * rowSize;
            paramEnd = readInt32(nextOff + 20);
        } else {
            paramEnd = tableValid[0x08] == 1 ? tableRowCounts[0x08] + 1 : paramStart;
        }
        int paramCount = Math.max(0, paramEnd - paramStart);
        byte[] ilBody = new byte[0];
        if (rva != 0) {
            int ilOffset = rvaToFileOffset(rva);
            if (ilOffset >= 0 && ilOffset < data.length) {
                ilBody = readIlBody(ilOffset);
            }
        }
        return new MethodInfo(name, flags, paramCount, ilBody, rva);
    }

    private byte[] readIlBody(int offset) throws DllReaderException {
        if (offset >= data.length) return new byte[0];
        int first = data[offset] & 0xFF;
        int size;
        int codeStart;
        if ((first & 3) == 2) {
            size = (first >> 2) * 4;
            codeStart = offset + 1;
        } else if ((first & 3) == 3) {
            if (offset + 12 > data.length) throw new DllReaderException("fat header IL truncado");
            int flags = readInt16(offset);
            size = flags & 0x0FFF;
            codeStart = offset + 12;
        } else {
            throw new DllReaderException("formato de header IL desconhecido: 0x" + Integer.toHexString(first));
        }
        if (codeStart + size > data.length)
            throw new DllReaderException("corpo IL além do fim do arquivo");
        return Arrays.copyOfRange(data, codeStart, codeStart + size);
    }

    private String[] getMemberRefInfo(int rid) throws DllReaderException {
        int base = tableOffsets[0x0A];
        int rowSize = tableRowSize(0x0A);
        int rowOff = base + (rid - 1) * rowSize;
        int classIdx = readInt32(rowOff);
        int nameIdx = readInt32(rowOff + 4);
        String name = getString(nameIdx);
        int tag = classIdx & 7;
        int parentRid = classIdx >> 3;
        String parentName;
        switch (tag) {
            case 0:
                parentName = getTypeName(parentRid)[0];
                break;
            case 1:
                parentName = getRefName(parentRid)[0];
                break;
            default:
                parentName = "?";
        }
        return new String[]{parentName, name};
    }

    private String getFieldName(int rid) throws DllReaderException {
        int base = tableOffsets[0x04];
        int rowSize = tableRowSize(0x04);
        int rowOff = base + (rid - 1) * rowSize;
        int nameIdx = readInt32(rowOff + 2);
        return getString(nameIdx);
    }

    private CustomAttributeInfo getCustomAttribute(int rid) throws DllReaderException {
        int base = tableOffsets[0x0C];
        int rowSize = tableRowSize(0x0C);
        int rowOff = base + (rid - 1) * rowSize;
        int parentIdx = readInt32(rowOff);
        int typeIdx = readInt32(rowOff + 4);
        int parentTag = parentIdx & 31;
        int parentRid = parentIdx >> 5;
        String targetName;
        String targetKind;
        switch (parentTag) {
            case 0:
                targetName = getMethod(parentRid).name;
                targetKind = "method";
                break;
            case 1:
                targetName = getFieldName(parentRid);
                targetKind = "field";
                break;
            case 2:
                targetName = getRefName(parentRid)[0];
                targetKind = "class";
                break;
            case 3:
                targetName = getTypeName(parentRid)[0];
                targetKind = "class";
                break;
            default:
                targetName = "?";
                targetKind = "?";
        }
        int typeTag = typeIdx & 7;
        int typeRid = typeIdx >> 3;
        String typeName;
        if (typeTag == 0) {
            String[] info = getMemberRefInfo(typeRid);
            typeName = info[0] + "." + info[1];
        } else {
            typeName = "?";
        }
        return new CustomAttributeInfo(typeName, targetName, targetKind);
    }

    public static DllReader parse(byte[] data) throws DllReaderException {
        if (data == null) throw new DllReaderException("dados nulos");
        if (data.length < 64) throw new DllReaderException("arquivo muito pequeno para ser PE: " + data.length + " bytes");
        if ((data[0] & 0xFF) != 0x4D || (data[1] & 0xFF) != 0x5A)
            throw new DllReaderException("não é um arquivo PE (MZ não encontrado)");
        return new DllReader(data);
    }

    public List<TypeInfo> getTypes() throws DllReaderException {
        List<TypeInfo> types = new ArrayList<>();
        if (tableValid[0x02] != 1) return types;
        for (int rid = 1; rid <= tableRowCounts[0x02]; rid++) {
            String[] nameNs = getTypeName(rid);
            String name = nameNs[0];
            String ns = nameNs[1];
            List<MethodInfo> methods = new ArrayList<>();
            List<FieldInfo> fields = new ArrayList<>();
            if (tableValid[0x06] == 1) {
                int mBase = tableOffsets[0x06];
                int mRowSize = tableRowSize(0x06);
                int tBase = tableOffsets[0x02];
                int tRowSize = tableRowSize(0x02);
                int tRowOff = tBase + (rid - 1) * tRowSize;
                int methodListIdx = readInt32(tRowOff + 20);
                int nextMethodListIdx;
                if (rid < tableRowCounts[0x02]) {
                    int nextRowOff = tBase + rid * tRowSize;
                    nextMethodListIdx = readInt32(nextRowOff + 20);
                } else {
                    nextMethodListIdx = tableRowCounts[0x06] + 1;
                }
                for (int i = methodListIdx; i < nextMethodListIdx && i <= tableRowCounts[0x06]; i++) {
                    methods.add(getMethod(i));
                }
            }
            if (tableValid[0x04] == 1) {
                int tBase = tableOffsets[0x02];
                int tRowSize = tableRowSize(0x02);
                int tRowOff = tBase + (rid - 1) * tRowSize;
                int fieldListIdx = readInt32(tRowOff + 16);
                int nextFieldListIdx;
                if (rid < tableRowCounts[0x02]) {
                    int nextRowOff = tBase + rid * tRowSize;
                    nextFieldListIdx = readInt32(nextRowOff + 16);
                } else {
                    nextFieldListIdx = tableRowCounts[0x04] + 1;
                }
                for (int i = fieldListIdx; i < nextFieldListIdx && i <= tableRowCounts[0x04]; i++) {
                    int fBase = tableOffsets[0x04];
                    int fRowSize = tableRowSize(0x04);
                    int fRowOff = fBase + (i - 1) * fRowSize;
                    int fNameIdx = readInt32(fRowOff + 2);
                    int fFlags = readInt16(fRowOff);
                    fields.add(new FieldInfo(getString(fNameIdx), fFlags));
                }
            }
            types.add(new TypeInfo(name, ns, methods, fields));
        }
        return types;
    }

    public List<CustomAttributeInfo> getCustomAttributes() throws DllReaderException {
        List<CustomAttributeInfo> attrs = new ArrayList<>();
        if (tableValid[0x0C] != 1) return attrs;
        for (int rid = 1; rid <= tableRowCounts[0x0C]; rid++) {
            attrs.add(getCustomAttribute(rid));
        }
        return attrs;
    }

    // Decodificar IL em instruções
    public static List<DecodedInstruction> decodeIl(byte[] il) throws DllReaderException {
        List<DecodedInstruction> instructions = new ArrayList<>();
        int pos = 0;
        while (pos < il.length) {
            int startPos = pos;
            int b = il[pos++] & 0xFF;
            String opcodeName;
            String operandType;
            int operandSize;
            long operand = 0;

            if (b == 0xFE) {
                // Opcode de 2 bytes
                if (pos >= il.length) throw new DllReaderException("opcode de 2 bytes truncado em 0x" + Integer.toHexString(startPos));
                int b2 = il[pos++] & 0xFF;
                int idx = 0xFE00 | b2;
                if (idx >= PARSED_OPCODES.length || PARSED_OPCODES[idx] == null)
                    throw new DllReaderException("opcode de 2 bytes desconhecido: 0xFE" + Integer.toHexString(b2));
                String[] parts = PARSED_OPCODES[idx];
                opcodeName = parts[0];
                operandType = parts[1];
                operandSize = Integer.parseInt(parts[2]);
            } else {
                if (PARSED_OPCODES[b] == null)
                    throw new DllReaderException("opcode desconhecido: 0x" + Integer.toHexString(b));
                String[] parts = PARSED_OPCODES[b];
                opcodeName = parts[0];
                operandType = parts[1];
                operandSize = Integer.parseInt(parts[2]);
            }

            // Ler operando baseado no tipo
            switch (operandType) {
                case "ShortInlineI":
                    if (pos + 1 > il.length) throw new DllReaderException("operando ShortInlineI truncado");
                    operand = (byte) il[pos]; // signed
                    pos += 1;
                    break;
                case "InlineI":
                    if (pos + 4 > il.length) throw new DllReaderException("operando InlineI truncado");
                    operand = readInt32(il, pos);
                    pos += 4;
                    break;
                case "InlineI8":
                    if (pos + 8 > il.length) throw new DllReaderException("operando InlineI8 truncado");
                    operand = readInt64(il, pos);
                    pos += 8;
                    break;
                case "ShortInlineR":
                    if (pos + 4 > il.length) throw new DllReaderException("operando ShortInlineR truncado");
                    operand = readInt32(il, pos);
                    pos += 4;
                    break;
                case "InlineR":
                    if (pos + 8 > il.length) throw new DllReaderException("operando InlineR truncado");
                    operand = readInt64(il, pos);
                    pos += 8;
                    break;
                case "ShortInlineVar":
                    if (pos + 1 > il.length) throw new DllReaderException("operando ShortInlineVar truncado");
                    operand = il[pos] & 0xFF;
                    pos += 1;
                    break;
                case "InlineVar":
                    if (pos + 2 > il.length) throw new DllReaderException("operando InlineVar truncado");
                    operand = readInt16(il, pos);
                    pos += 2;
                    break;
                case "InlineBrTarget":
                    if (pos + 4 > il.length) throw new DllReaderException("operando InlineBrTarget truncado");
                    operand = readInt32(il, pos);
                    pos += 4;
                    break;
                case "ShortInlineBrTarget":
                    if (pos + 1 > il.length) throw new DllReaderException("operando ShortInlineBrTarget truncado");
                    operand = (byte) il[pos];
                    pos += 1;
                    break;
                case "InlineSwitch":
                    if (pos + 4 > il.length) throw new DllReaderException("operando InlineSwitch truncado");
                    int n = readInt32(il, pos);
                    pos += 4;
                    if (pos + n * 4 > il.length) throw new DllReaderException("operando InlineSwitch além do fim");
                    pos += n * 4;
                    operand = n;
                    break;
                case "InlineMethod":
                case "InlineField":
                case "InlineType":
                case "InlineString":
                case "InlineSig":
                case "InlineTok":
                    if (pos + 4 > il.length) throw new DllReaderException("operando " + operandType + " truncado");
                    operand = readInt32(il, pos);
                    pos += 4;
                    break;
                case "InlineNone":
                    break;
                default:
                    throw new DllReaderException("tipo de operando desconhecido: " + operandType);
            }

            instructions.add(new DecodedInstruction(opcodeName, operandType, operand, startPos));
        }
        return instructions;
    }

    public record DecodedInstruction(String opcode, String operandType, long operand, int position) {}

    private static int readInt16(byte[] data, int pos) {
        return (data[pos] & 0xFF) | ((data[pos + 1] & 0xFF) << 8);
    }
    private static int readInt32(byte[] data, int pos) {
        return (data[pos] & 0xFF) | ((data[pos + 1] & 0xFF) << 8) |
               ((data[pos + 2] & 0xFF) << 16) | ((data[pos + 3] & 0xFF) << 24);
    }
    private static long readInt64(byte[] data, int pos) {
        return (readInt32(data, pos) & 0xFFFFFFFFL) | ((readInt32(data, pos + 4) & 0xFFFFFFFFL) << 32);
    }
}
