package io.github.rianprei.bepinex.manager.core;

import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/** Leitura limitada de assemblies PE/CLI e metadata ECMA-335. */
public final class DllReader {
    public static final int MAX_FILE_SIZE = 64 * 1024 * 1024;
    private static final int MAX_SECTIONS = 96;
    private static final int MAX_STREAMS = 64;
    private static final int MAX_TABLE_ROWS = 250_000;
    private static final int MAX_TOTAL_PARAMETERS = 250_000;
    private static final int MAX_TOTAL_IL_SIZE = 16 * 1024 * 1024;
    private static final int MAX_INSTRUCTIONS = 100_000;
    private static final int TABLE_COUNT = 64;

    public static final class DllReaderException extends Exception {
        public DllReaderException(String message) {
            super(message);
        }

        public DllReaderException(String message, Throwable cause) {
            super(message, cause);
        }
    }

    public record Section(String name, long virtualAddress, long virtualSize,
                          long rawSize, long rawOffset) {}
    public record ParameterInfo(String name, String type) {}
    public record MethodInfo(int rid, String name, int flags, String returnType,
                             List<ParameterInfo> parameters, byte[] ilBody,
                             long rva, boolean fatHeader) {
        public int paramCount() {
            return parameters.size();
        }
    }
    public record FieldInfo(String name, int flags, String type) {}
    public record TypeInfo(int rid, String name, String namespace,
                           List<MethodInfo> methods, List<FieldInfo> fields) {
        public String fullName() {
            return namespace == null || namespace.isEmpty() ? name : namespace + "." + name;
        }
    }
    public record CustomAttributeInfo(String typeName, int parentTable, int parentRid,
                                      String parentTypeName, String parentMemberName,
                                      String typeArgument, String methodArgument) {}
    public record DecodedInstruction(String opcode, String operandType,
                                     long operand, int position) {}
    public record FieldReference(String declaringType, String name, String type, boolean isStatic) {}

    private record Stream(int offset, int size) {
        int end() {
            return offset + size;
        }
    }
    private record MethodSignature(String returnType, List<String> parameterTypes) {}
    private record DecodedIndex(int table, int rid) {}

    private final byte[] data;
    private final List<Section> sections;
    private final int metadataStartOffset;
    private final int metadataSize;
    private final Stream tablesStream;
    private final Stream stringsStream;
    private final Stream blobStream;
    private final Stream userStringsStream;
    private final int[] rowCounts = new int[TABLE_COUNT];
    private final int[] tableOffsets = new int[TABLE_COUNT];
    private final int heapSizes;
    private List<TypeInfo> cachedTypes;
    private List<CustomAttributeInfo> cachedAttributes;
    private int[] methodOwners;
    private int[] fieldOwners;

    private DllReader(byte[] data) throws DllReaderException {
        this.data = data;
        Arrays.fill(tableOffsets, -1);

        int peOffset = checkedInt(u32(0x3c), "e_lfanew");
        requireRange(peOffset, 24, "cabeçalho COFF");
        if (u32(peOffset) != 0x00004550L) {
            throw error("assinatura PE ausente");
        }
        int coff = peOffset + 4;
        int sectionCount = u16(coff + 2);
        int optionalSize = u16(coff + 16);
        if (sectionCount <= 0 || sectionCount > MAX_SECTIONS) {
            throw error("quantidade de seções PE inválida: " + sectionCount);
        }

        int optional = coff + 20;
        requireRange(optional, optionalSize, "optional header");
        if (optionalSize < 2) throw error("optional header truncado");
        int magic = u16(optional);
        int directoryStart;
        int directoryCountOffset;
        if (magic == 0x10b) {
            directoryStart = 96;
            directoryCountOffset = 92;
        } else if (magic == 0x20b) {
            directoryStart = 112;
            directoryCountOffset = 108;
        } else {
            throw error("formato de optional header PE não suportado");
        }
        requireRange(optional, directoryStart, "optional header");
        int directoryCount = checkedInt(u32(optional + directoryCountOffset), "diretórios PE");
        if (directoryCount <= 14 || directoryStart + 15L * 8L > optionalSize) {
            throw error("diretório CLI ausente no optional header");
        }

        int sectionTable = optional + optionalSize;
        requireRange(sectionTable, sectionCount * 40, "tabela de seções PE");
        List<Section> parsedSections = new ArrayList<>(sectionCount);
        for (int i = 0; i < sectionCount; i++) {
            int row = sectionTable + i * 40;
            String name = ascii(row, 8);
            parsedSections.add(new Section(name, u32(row + 12), u32(row + 8),
                    u32(row + 16), u32(row + 20)));
        }
        this.sections = Collections.unmodifiableList(parsedSections);

        int cliDirectory = optional + directoryStart + 14 * 8;
        long cliRva = u32(cliDirectory);
        long cliSize = u32(cliDirectory + 4);
        if (cliRva == 0 || cliSize < 16) throw error("assembly sem cabeçalho CLI válido");
        int cliOffset = rvaToFileOffset(cliRva);
        long declaredCliSize = u32(cliOffset);
        if (declaredCliSize < 16 || declaredCliSize > cliSize) {
            throw error("tamanho do cabeçalho CLI inválido");
        }
        long metadataRva = u32(cliOffset + 8);
        long declaredMetadataSize = u32(cliOffset + 12);
        if (metadataRva == 0 || declaredMetadataSize < 20 || declaredMetadataSize > data.length) {
            throw error("diretório de metadata CLI inválido");
        }
        this.metadataStartOffset = rvaToFileOffset(metadataRva);
        this.metadataSize = checkedInt(declaredMetadataSize, "tamanho da metadata");
        requireRange(metadataStartOffset, metadataSize, "metadata CLI");
        if (u32(metadataStartOffset) != 0x424a5342L) {
            throw error("metadata root sem assinatura BSJB");
        }

        Stream[] streams = parseStreams();
        this.tablesStream = streams[0];
        this.stringsStream = streams[1];
        this.blobStream = streams[2];
        this.userStringsStream = streams[3];
        this.heapSizes = u8(tablesStream.offset + 6);
        parseTables();
    }

    public static DllReader parse(byte[] bytes) throws DllReaderException {
        if (bytes == null) throw new DllReaderException("arquivo DLL nulo");
        if (bytes.length < 64) throw new DllReaderException("arquivo pequeno demais para ser PE");
        if (bytes.length > MAX_FILE_SIZE) {
            throw new DllReaderException("arquivo DLL excede o limite de " + MAX_FILE_SIZE + " bytes");
        }
        if (u8(bytes, 0) != 'M' || u8(bytes, 1) != 'Z') {
            throw new DllReaderException("assinatura MZ ausente: não é um arquivo PE");
        }
        try {
            DllReader reader = new DllReader(bytes);
            reader.getTypes();
            reader.getCustomAttributes();
            return reader;
        } catch (DllReaderException e) {
            throw e;
        } catch (RuntimeException e) {
            throw new DllReaderException("arquivo PE/CLI malformado: " + safeMessage(e), e);
        }
    }

    public int metadataStartOffset() {
        return metadataStartOffset;
    }

    public int metadataSize() {
        return metadataSize;
    }

    public int tableRowCount(int table) throws DllReaderException {
        if (table < 0 || table >= TABLE_COUNT) throw error("índice de tabela inválido: " + table);
        return rowCounts[table];
    }

    public List<Section> sections() {
        return sections;
    }

    public String userString(int heapOffset) throws DllReaderException {
        if (heapOffset < 0 || heapOffset >= userStringsStream.size) {
            throw error("índice de #US fora dos limites: " + heapOffset);
        }
        int cursor = userStringsStream.offset + heapOffset;
        int length = readCompressedUInt(cursor, userStringsStream.end());
        cursor += compressedSize(u8(cursor));
        if (length == 0 || (length & 1) == 0) {
            throw error("entrada de #US inválida");
        }
        requireRange(cursor, length, userStringsStream.end(), "#US");
        return new String(data, cursor, length - 1, StandardCharsets.UTF_16LE);
    }

    public List<TypeInfo> getTypes() throws DllReaderException {
        if (cachedTypes != null) return cachedTypes;
        List<TypeInfo> result = new ArrayList<>();
        int[] fieldStarts = new int[rowCounts[2]];
        int[] methodStarts = new int[rowCounts[2]];
        long totalParameters = 0;
        long totalIlSize = 0;
        for (int rid = 1; rid <= rowCounts[2]; rid++) {
            int row = tableRow(2, rid);
            int cursor = row + 4;
            String name = getString(readIndex(cursor, "strings"));
            cursor += indexSize("strings");
            String namespace = getString(readIndex(cursor, "strings"));
            cursor += indexSize("strings") + codedSize("TypeDefOrRef");
            fieldStarts[rid - 1] = readIndex(cursor, "table:4");
            cursor += indexSize("table:4");
            methodStarts[rid - 1] = readIndex(cursor, "table:6");
            result.add(new TypeInfo(rid, name, namespace, new ArrayList<>(), new ArrayList<>()));
        }

        methodOwners = new int[rowCounts[6] + 1];
        fieldOwners = new int[rowCounts[4] + 1];
        for (int rid = 1; rid <= rowCounts[2]; rid++) {
            int fieldEnd = rid < rowCounts[2] ? fieldStarts[rid] : rowCounts[4] + 1;
            int methodEnd = rid < rowCounts[2] ? methodStarts[rid] : rowCounts[6] + 1;
            validateListRange(fieldStarts[rid - 1], fieldEnd, rowCounts[4], "FieldList");
            validateListRange(methodStarts[rid - 1], methodEnd, rowCounts[6], "MethodList");
            TypeInfo type = result.get(rid - 1);
            for (int fieldRid = fieldStarts[rid - 1]; fieldRid < fieldEnd; fieldRid++) {
                fieldOwners[fieldRid] = rid;
                type.fields.add(readField(fieldRid));
            }
            for (int methodRid = methodStarts[rid - 1]; methodRid < methodEnd; methodRid++) {
                methodOwners[methodRid] = rid;
                MethodInfo method = getMethodByRid(methodRid);
                totalParameters += method.parameters().size();
                totalIlSize += method.ilBody().length;
                if (totalParameters > MAX_TOTAL_PARAMETERS) {
                    throw error("assembly tem parâmetros demais no total");
                }
                if (totalIlSize > MAX_TOTAL_IL_SIZE) {
                    throw error("corpos IL excedem o limite total permitido");
                }
                type.methods.add(method);
            }
        }
        cachedTypes = Collections.unmodifiableList(result);
        return cachedTypes;
    }

    public List<CustomAttributeInfo> getCustomAttributes() throws DllReaderException {
        if (cachedAttributes != null) return cachedAttributes;
        getTypes();
        List<CustomAttributeInfo> attributes = new ArrayList<>();
        for (int rid = 1; rid <= rowCounts[12]; rid++) {
            int row = tableRow(12, rid);
            int parentIndex = readIndex(row, "coded:HasCustomAttribute");
            DecodedIndex parent = decodeCoded("HasCustomAttribute", parentIndex);
            int typeIndex = readIndex(row + codedSize("HasCustomAttribute"), "coded:CustomAttributeType");
            DecodedIndex constructor = decodeCoded("CustomAttributeType", typeIndex);
            String typeName = attributeTypeName(constructor);
            String parentType = null;
            String parentName = null;
            if (parent.table == 2) {
                TypeInfo type = getType(parent.rid);
                parentType = type.fullName();
                parentName = type.name;
            } else if (parent.table == 6) {
                MethodInfo method = getMethodByRid(parent.rid);
                parentType = getType(methodOwners[parent.rid]).fullName();
                parentName = method.name;
            } else if (parent.table == 4) {
                parentName = readField(parent.rid).name;
            }
            String typeArgument = null;
            String methodArgument = null;
            if (simpleName(typeName).equals("HarmonyPatch")) {
                int valueOffset = row + codedSize("HasCustomAttribute") + codedSize("CustomAttributeType");
                byte[] value = getBlob(readIndex(valueOffset, "blob"));
                String[] arguments = readHarmonyPatchArguments(value);
                if (arguments != null) {
                    typeArgument = arguments[0];
                    methodArgument = arguments[1];
                }
            }
            attributes.add(new CustomAttributeInfo(typeName, parent.table, parent.rid,
                    parentType, parentName, typeArgument, methodArgument));
        }
        cachedAttributes = Collections.unmodifiableList(attributes);
        return cachedAttributes;
    }

    public MethodInfo getMethodByRid(int rid) throws DllReaderException {
        if (rid < 1 || rid > rowCounts[6]) throw error("índice MethodDef fora dos limites: " + rid);
        int row = tableRow(6, rid);
        long rva = u32(row);
        int flags = u16(row + 6);
        int cursor = row + 8;
        String name = getString(readIndex(cursor, "strings"));
        cursor += indexSize("strings");
        int signatureIndex = readIndex(cursor, "blob");
        cursor += indexSize("blob");
        int firstParam = readIndex(cursor, "table:8");
        int nextParam = rid < rowCounts[6]
                ? readIndex(tableRow(6, rid + 1) + methodParamListOffset(), "table:8")
                : rowCounts[8] + 1;
        validateListRange(firstParam, nextParam, rowCounts[8], "ParamList");
        MethodSignature signature = readMethodSignature(getBlob(signatureIndex));
        if (nextParam - firstParam > signature.parameterTypes.size() + 1) {
            throw error("tabela Param tem mais parâmetros que a assinatura do método " + name);
        }
        List<ParameterInfo> parameters = new ArrayList<>(signature.parameterTypes.size());
        Map<Integer, String> parameterNames = new HashMap<>();
        boolean returnParameterSeen = false;
        for (int paramRid = firstParam; paramRid < nextParam; paramRid++) {
            int paramRow = tableRow(8, paramRid);
            int sequence = u16(paramRow + 2);
            if (sequence == 0) {
                // Param de sequência zero é metadado do RETORNO (ex.: [return:
                // MarshalAs]) — pode existir sem ser argumento. Validado em
                // separado e fora do binding de argumentos.
                if (returnParameterSeen) {
                    throw error("sequência Param de retorno duplicada no método " + name);
                }
                returnParameterSeen = true;
                continue;
            }
            if (sequence < 1 || sequence > signature.parameterTypes.size()
                    || parameterNames.containsKey(sequence)) {
                throw error("sequência Param inválida no método " + name);
            }
            parameterNames.put(sequence, getString(readIndex(paramRow + 4, "strings")));
        }
        for (int i = 0; i < signature.parameterTypes.size(); i++) {
            parameters.add(new ParameterInfo(parameterNames.getOrDefault(i + 1, ""),
                    signature.parameterTypes.get(i)));
        }
        Body body = rva == 0 ? new Body(new byte[0], false) : readMethodBody(rva);
        return new MethodInfo(rid, name, flags, signature.returnType,
                Collections.unmodifiableList(parameters), body.code, rva, body.fat);
    }

    public FieldReference resolveField(long token) throws DllReaderException {
        int table = (int) ((token >>> 24) & 0xff);
        int rid = (int) (token & 0x00ff_ffff);
        if (table == 4) {
            if (rid < 1 || rid > rowCounts[4]) throw error("token Field fora dos limites");
            getTypes();
            int ownerRid = fieldOwners[rid];
            FieldInfo field = readField(rid);
            return new FieldReference(getType(ownerRid).fullName(), field.name, field.type,
                    (field.flags & 0x0010) != 0);
        }
        if (table == 10) {
            if (rid < 1 || rid > rowCounts[10]) throw error("token MemberRef fora dos limites");
            int row = tableRow(10, rid);
            int parentIndex = readIndex(row, "coded:MemberRefParent");
            DecodedIndex parent = decodeCoded("MemberRefParent", parentIndex);
            int nameOffset = row + codedSize("MemberRefParent");
            String name = getString(readIndex(nameOffset, "strings"));
            byte[] signature = getBlob(readIndex(nameOffset + indexSize("strings"), "blob"));
            String declaringType = parentTypeName(parent);
            String fieldType = signature.length > 1 && u8(signature, 0) == 0x06
                    ? readFieldSignature(signature) : "desconhecido";
            return new FieldReference(declaringType, name, fieldType, true);
        }
        throw error("token IL 0x" + Long.toHexString(token) + " não é um campo");
    }

    public static List<DecodedInstruction> decodeIl(byte[] il) throws DllReaderException {
        if (il == null) throw new DllReaderException("corpo IL nulo");
        if (il.length > MAX_FILE_SIZE) throw new DllReaderException("corpo IL excede o limite permitido");
        List<DecodedInstruction> decoded = new ArrayList<>();
        int position = 0;
        while (position < il.length) {
            if (decoded.size() >= MAX_INSTRUCTIONS) throw new DllReaderException("corpo IL tem instruções demais");
            int start = position;
            int opcodeValue = u8(il, position++);
            if (opcodeValue == 0xfe) {
                if (position >= il.length) throw new DllReaderException("opcode IL de dois bytes truncado");
                opcodeValue = 0xfe00 | u8(il, position++);
            }
            Ecma335OpCodes.OpCode opcode = Ecma335OpCodes.TABLE[opcodeValue];
            if (opcode == null) throw new DllReaderException("opcode IL desconhecido: 0x" + Integer.toHexString(opcodeValue));
            long operand = 0;
            switch (opcode.operandType()) {
                case "InlineNone" -> { }
                case "ShortInlineI" -> {
                    requireIl(il, position, 1, opcode.name());
                    operand = (byte) il[position++];
                }
                case "InlineI", "InlineBrTarget" -> {
                    requireIl(il, position, 4, opcode.name());
                    operand = i32(il, position);
                    position += 4;
                }
                case "InlineI8", "InlineR" -> {
                    requireIl(il, position, 8, opcode.name());
                    operand = i64(il, position);
                    position += 8;
                }
                case "ShortInlineR" -> {
                    requireIl(il, position, 4, opcode.name());
                    operand = i32(il, position) & 0xffff_ffffL;
                    position += 4;
                }
                case "ShortInlineVar" -> {
                    requireIl(il, position, 1, opcode.name());
                    operand = u8(il, position++);
                }
                case "InlineVar" -> {
                    requireIl(il, position, 2, opcode.name());
                    operand = u16(il, position);
                    position += 2;
                }
                case "ShortInlineBrTarget" -> {
                    requireIl(il, position, 1, opcode.name());
                    operand = (byte) il[position++];
                }
                case "InlineField", "InlineMethod", "InlineSig", "InlineString",
                     "InlineTok", "InlineType" -> {
                    requireIl(il, position, 4, opcode.name());
                    operand = u32(il, position);
                    position += 4;
                }
                case "InlineSwitch" -> {
                    requireIl(il, position, 4, opcode.name());
                    int count = i32(il, position);
                    position += 4;
                    if (count < 0 || count > 100_000 || (long) position + (long) count * 4 > il.length) {
                        throw new DllReaderException("tabela de destinos switch inválida");
                    }
                    operand = count;
                    position += count * 4;
                }
                default -> throw new DllReaderException("tipo de operando IL não suportado: " + opcode.operandType());
            }
            decoded.add(new DecodedInstruction(opcode.name(), opcode.operandType(), operand, start));
        }
        return decoded;
    }

    private record Body(byte[] code, boolean fat) {}

    private Body readMethodBody(long rva) throws DllReaderException {
        int offset = rvaToFileOffset(rva);
        int first = u8(offset);
        int format = first & 3;
        if (format == 2) {
            int codeSize = first >>> 2;
            requireRange(offset + 1, codeSize, "corpo IL tiny");
            return new Body(Arrays.copyOfRange(data, offset + 1, offset + 1 + codeSize), false);
        }
        if (format != 3) throw error("cabeçalho IL tem formato inválido");
        int flagsAndSize = u16(offset);
        int headerDwords = (flagsAndSize >>> 12) & 0x0f;
        if (headerDwords < 3) throw error("cabeçalho IL fat menor que 12 bytes");
        int headerSize = headerDwords * 4;
        requireRange(offset, headerSize, "cabeçalho IL fat");
        int codeSize = checkedInt(u32(offset + 4), "tamanho do corpo IL");
        if (codeSize > 4 * 1024 * 1024) throw error("corpo IL excede 4 MiB");
        int codeOffset = checkedAdd(offset, headerSize, "corpo IL");
        requireRange(codeOffset, codeSize, "corpo IL fat");
        return new Body(Arrays.copyOfRange(data, codeOffset, codeOffset + codeSize), true);
    }

    private Stream[] parseStreams() throws DllReaderException {
        int versionLength = checkedInt(u32(metadataStartOffset + 12), "tamanho da versão da metadata");
        if (versionLength > metadataSize - 20) throw error("string de versão da metadata truncada");
        long afterVersion = (long) metadataStartOffset + 16L + versionLength;
        int streamHeader = checkedInt((afterVersion + 3L) & ~3L, "cabeçalho de streams");
        requireRange(streamHeader, 4, metadataStartOffset + metadataSize, "cabeçalho de streams");
        int streamCount = u16(streamHeader + 2);
        if (streamCount <= 0 || streamCount > MAX_STREAMS) throw error("quantidade de streams inválida");
        int cursor = streamHeader + 4;
        Stream tables = null;
        Stream strings = null;
        Stream blob = null;
        Stream userStrings = null;
        for (int i = 0; i < streamCount; i++) {
            requireRange(cursor, 8, metadataStartOffset + metadataSize, "descritor de stream");
            long relativeOffset = u32(cursor);
            long streamSize = u32(cursor + 4);
            int nameStart = cursor + 8;
            int nameEnd = nameStart;
            int rootEnd = metadataStartOffset + metadataSize;
            while (nameEnd < rootEnd && data[nameEnd] != 0 && nameEnd - nameStart <= 32) nameEnd++;
            if (nameEnd >= rootEnd || nameEnd - nameStart > 32) throw error("nome de stream inválido");
            String name = new String(data, nameStart, nameEnd - nameStart, StandardCharsets.US_ASCII);
            long absOffset = (long) metadataStartOffset + relativeOffset;
            if (streamSize > Integer.MAX_VALUE || absOffset > Integer.MAX_VALUE) {
                throw error("stream excede os limites");
            }
            requireRange((int) absOffset, (int) streamSize, rootEnd, "stream " + name);
            Stream stream = new Stream((int) absOffset, (int) streamSize);
            switch (name) {
                case "#~", "#-" -> tables = stream;
                case "#Strings" -> strings = stream;
                case "#Blob" -> blob = stream;
                case "#US" -> userStrings = stream;
                default -> { }
            }
            cursor = (nameEnd + 1 + 3) & ~3;
        }
        if (tables == null || strings == null || blob == null || userStrings == null) {
            throw error("faltam streams #~, #Strings, #Blob ou #US");
        }
        return new Stream[]{tables, strings, blob, userStrings};
    }

    private void parseTables() throws DllReaderException {
        requireRange(tablesStream.offset, 24, tablesStream.end(), "cabeçalho #~");
        long valid = i64(tablesStream.offset + 8);
        int cursor = tablesStream.offset + 24;
        long totalRows = 0;
        for (int table = 0; table < TABLE_COUNT; table++) {
            if (((valid >>> table) & 1L) == 0) continue;
            requireRange(cursor, 4, tablesStream.end(), "contagem de linhas #~");
            long count = u32(cursor);
            cursor += 4;
            if (count > MAX_TABLE_ROWS) throw error("tabela " + table + " excede o limite de linhas");
            rowCounts[table] = (int) count;
            totalRows += count;
            if (totalRows > MAX_TABLE_ROWS) throw error("metadata excede o limite total de linhas");
        }
        for (int table = 0; table < TABLE_COUNT; table++) {
            if (((valid >>> table) & 1L) == 0) continue;
            tableOffsets[table] = cursor;
            int rowSize = tableRowSize(table);
            long byteSize = (long) rowSize * rowCounts[table];
            if (byteSize > Integer.MAX_VALUE) throw error("tabela metadata grande demais");
            requireRange(cursor, (int) byteSize, tablesStream.end(), "tabela metadata " + table);
            cursor += (int) byteSize;
        }
    }

    private int tableRowSize(int table) throws DllReaderException {
        return switch (table) {
            case 0 -> 2 + indexSize("strings") + 3 * indexSize("guid");
            case 1 -> codedSize("ResolutionScope") + 2 * indexSize("strings");
            case 2 -> 4 + 2 * indexSize("strings") + codedSize("TypeDefOrRef")
                    + indexSize("table:4") + indexSize("table:6");
            case 3 -> indexSize("table:4");
            case 4 -> 2 + indexSize("strings") + indexSize("blob");
            case 5 -> indexSize("table:6");
            case 6 -> 4 + 2 + 2 + indexSize("strings") + indexSize("blob") + indexSize("table:8");
            case 7 -> indexSize("table:8");
            case 8 -> 2 + 2 + indexSize("strings");
            case 9 -> indexSize("table:2") + codedSize("TypeDefOrRef");
            case 10 -> codedSize("MemberRefParent") + indexSize("strings") + indexSize("blob");
            case 11 -> 2 + codedSize("HasConstant") + indexSize("blob");
            case 12 -> codedSize("HasCustomAttribute") + codedSize("CustomAttributeType") + indexSize("blob");
            case 13 -> codedSize("HasFieldMarshal") + indexSize("blob");
            case 14 -> 2 + codedSize("HasDeclSecurity") + indexSize("blob");
            case 15 -> 2 + 4 + indexSize("table:2");
            case 16 -> 4 + indexSize("table:4");
            case 17 -> indexSize("blob");
            case 18 -> indexSize("table:2") + indexSize("table:20");
            case 19 -> indexSize("table:20");
            case 20 -> 2 + indexSize("strings") + codedSize("TypeDefOrRef");
            case 21 -> indexSize("table:2") + indexSize("table:23");
            case 22 -> indexSize("table:23");
            case 23 -> 2 + indexSize("strings") + indexSize("blob");
            case 24 -> 2 + indexSize("table:6") + codedSize("HasSemantics");
            case 25 -> indexSize("table:2") + 2 * codedSize("MethodDefOrRef");
            case 26 -> indexSize("strings");
            case 27 -> indexSize("blob");
            case 28 -> 2 + codedSize("MemberForwarded") + indexSize("strings") + indexSize("table:26");
            case 29 -> 4 + indexSize("table:4");
            case 30 -> 8;
            case 31 -> 4;
            case 32 -> 4 + 2 * 4 + 4 + indexSize("blob") + 2 * indexSize("strings");
            case 33 -> 4;
            case 34 -> 12;
            case 35 -> 2 * 4 + 4 + 2 * indexSize("blob") + 2 * indexSize("strings");
            case 36 -> 4 + indexSize("table:35");
            case 37 -> 12 + indexSize("table:35");
            case 38 -> 4 + indexSize("strings") + indexSize("blob");
            case 39 -> 4 + 4 + 2 * indexSize("strings") + codedSize("Implementation");
            case 40 -> 4 + 4 + indexSize("strings") + codedSize("Implementation");
            case 41 -> 2 * indexSize("table:2");
            case 42 -> 2 + 2 + codedSize("TypeOrMethodDef") + indexSize("strings");
            case 43 -> codedSize("MethodDefOrRef") + indexSize("blob");
            case 44 -> indexSize("table:42") + codedSize("TypeDefOrRef");
            default -> throw error("tabela metadata ECMA-335 não suportada: " + table);
        };
    }

    private int indexSize(String kind) throws DllReaderException {
        if ("strings".equals(kind)) return (heapSizes & 0x01) != 0 ? 4 : 2;
        if ("guid".equals(kind)) return (heapSizes & 0x02) != 0 ? 4 : 2;
        if ("blob".equals(kind)) return (heapSizes & 0x04) != 0 ? 4 : 2;
        if (kind.startsWith("table:")) {
            int table = Integer.parseInt(kind.substring(6));
            if (table < 0 || table >= TABLE_COUNT) throw error("índice de tabela inválido");
            return rowCounts[table] >= 0x1_0000 ? 4 : 2;
        }
        throw error("heap ou tabela desconhecida: " + kind);
    }

    private int codedSize(String kind) throws DllReaderException {
        int bits;
        int[] tables;
        switch (kind) {
            case "TypeDefOrRef" -> { bits = 2; tables = new int[]{2, 1, 27}; }
            case "HasConstant" -> { bits = 2; tables = new int[]{4, 8, 23}; }
            case "HasCustomAttribute" -> {
                bits = 5;
                tables = new int[]{6, 4, 1, 2, 8, 9, 10, 0, 14, 23, 20, 17,
                        26, 27, 32, 35, 38, 39, 40, 42, 44, 43};
            }
            case "HasFieldMarshal" -> { bits = 1; tables = new int[]{4, 8}; }
            case "HasDeclSecurity" -> { bits = 2; tables = new int[]{2, 6, 32}; }
            case "MemberRefParent" -> { bits = 3; tables = new int[]{2, 1, 26, 6, 27}; }
            case "HasSemantics" -> { bits = 1; tables = new int[]{20, 23}; }
            case "MethodDefOrRef" -> { bits = 1; tables = new int[]{6, 10}; }
            case "MemberForwarded" -> { bits = 1; tables = new int[]{4, 6}; }
            case "Implementation" -> { bits = 2; tables = new int[]{38, 35, 39}; }
            case "CustomAttributeType" -> { bits = 3; tables = new int[]{-1, -1, 6, 10}; }
            case "ResolutionScope" -> { bits = 2; tables = new int[]{0, 26, 35, 1}; }
            case "TypeOrMethodDef" -> { bits = 1; tables = new int[]{2, 6}; }
            default -> throw error("índice coded desconhecido: " + kind);
        }
        int maxRows = 0;
        for (int table : tables) {
            if (table >= 0) maxRows = Math.max(maxRows, rowCounts[table]);
        }
        return maxRows >= (1 << (16 - bits)) ? 4 : 2;
    }

    private DecodedIndex decodeCoded(String kind, int value) throws DllReaderException {
        int bits;
        int[] tables;
        switch (kind) {
            case "TypeDefOrRef" -> { bits = 2; tables = new int[]{2, 1, 27}; }
            case "HasCustomAttribute" -> {
                bits = 5;
                tables = new int[]{6, 4, 1, 2, 8, 9, 10, 0, 14, 23, 20, 17,
                        26, 27, 32, 35, 38, 39, 40, 42, 44, 43};
            }
            case "MemberRefParent" -> { bits = 3; tables = new int[]{2, 1, 26, 6, 27}; }
            case "CustomAttributeType" -> { bits = 3; tables = new int[]{-1, -1, 6, 10}; }
            default -> throw error("índice coded sem decodificador: " + kind);
        }
        int tag = value & ((1 << bits) - 1);
        int rid = value >>> bits;
        if (tag >= tables.length || rid == 0 || tables[tag] < 0 || rid > rowCounts[tables[tag]]) {
            throw error("índice coded inválido em " + kind + ": " + value);
        }
        return new DecodedIndex(tables[tag], rid);
    }

    private FieldInfo readField(int rid) throws DllReaderException {
        int row = tableRow(4, rid);
        int flags = u16(row);
        int nameIndex = readIndex(row + 2, "strings");
        int signatureIndex = readIndex(row + 2 + indexSize("strings"), "blob");
        return new FieldInfo(getString(nameIndex), flags, readFieldSignature(getBlob(signatureIndex)));
    }

    private MethodSignature readMethodSignature(byte[] signature) throws DllReaderException {
        SigReader reader = new SigReader(signature);
        int callingConvention = reader.readByte();
        if ((callingConvention & 0x10) != 0) reader.readCompressed();
        int parameterCount = reader.readCompressed();
        if (parameterCount > 1024) throw error("método tem parâmetros demais");
        String returnType = reader.readType(0);
        List<String> parameters = new ArrayList<>(parameterCount);
        for (int i = 0; i < parameterCount; i++) {
            if (reader.peek() == 0x41) reader.readByte();
            parameters.add(reader.readType(0));
        }
        if (!reader.atEnd()) throw error("bytes extras na assinatura de método");
        return new MethodSignature(returnType, parameters);
    }

    private String readFieldSignature(byte[] signature) throws DllReaderException {
        SigReader reader = new SigReader(signature);
        if (reader.readByte() != 0x06) throw error("assinatura de campo inválida");
        String type = reader.readType(0);
        if (!reader.atEnd()) throw error("bytes extras na assinatura de campo");
        return type;
    }

    private String readTypeName(int codedIndex) throws DllReaderException {
        DecodedIndex decoded = decodeCoded("TypeDefOrRef", codedIndex);
        if (decoded.table == 2) return getType(decoded.rid).fullName();
        if (decoded.table == 1) return typeRefName(decoded.rid);
        return "TypeSpec#" + decoded.rid;
    }

    private String typeRefName(int rid) throws DllReaderException {
        int row = tableRow(1, rid);
        int cursor = row + codedSize("ResolutionScope");
        String name = getString(readIndex(cursor, "strings"));
        String namespace = getString(readIndex(cursor + indexSize("strings"), "strings"));
        return namespace.isEmpty() ? name : namespace + "." + name;
    }

    private TypeInfo getType(int rid) throws DllReaderException {
        if (rid < 1 || rid > rowCounts[2]) throw error("índice TypeDef fora dos limites: " + rid);
        if (cachedTypes != null) return cachedTypes.get(rid - 1);
        int row = tableRow(2, rid);
        String name = getString(readIndex(row + 4, "strings"));
        String namespace = getString(readIndex(row + 4 + indexSize("strings"), "strings"));
        return new TypeInfo(rid, name, namespace, Collections.emptyList(), Collections.emptyList());
    }

    private String parentTypeName(DecodedIndex parent) throws DllReaderException {
        return switch (parent.table) {
            case 2 -> getType(parent.rid).fullName();
            case 1 -> typeRefName(parent.rid);
            case 6 -> methodOwners != null && parent.rid < methodOwners.length
                    ? getType(methodOwners[parent.rid]).fullName() : "";
            case 26 -> getString(readIndex(tableRow(26, parent.rid), "strings"));
            case 27 -> "TypeSpec#" + parent.rid;
            default -> "";
        };
    }

    private String attributeTypeName(DecodedIndex constructor) throws DllReaderException {
        if (constructor.table == 10) {
            int row = tableRow(10, constructor.rid);
            DecodedIndex parent = decodeCoded("MemberRefParent",
                    readIndex(row, "coded:MemberRefParent"));
            String parentName = parentTypeName(parent);
            int nameOffset = row + codedSize("MemberRefParent");
            String constructorName = getString(readIndex(nameOffset, "strings"));
            // Construtor MemberRef de assembly REFERENCIADO (HarmonyLib externo):
            // o nome do atributo é o TIPO declarante — o ".ctor" do final não.
            return parentName.isEmpty() ? constructorName : parentName;
        }
        if (constructor.table == 6) {
            if (methodOwners == null || constructor.rid >= methodOwners.length
                    || methodOwners[constructor.rid] == 0) {
                throw error("construtor de atributo sem tipo proprietário");
            }
            return getType(methodOwners[constructor.rid]).fullName();
        }
        return "";
    }

    private String[] readHarmonyPatchArguments(byte[] blob) throws DllReaderException {
        if (blob.length < 2 || u16(blob, 0) != 1) return null;
        int cursor = 2;
        String type = readSerString(blob, cursor);
        if (type == null) return null;
        cursor += serStringEncodedSize(blob, cursor);
        String method = readSerString(blob, cursor);
        if (method == null) return null;
        cursor += serStringEncodedSize(blob, cursor);
        if (cursor + 2 != blob.length || u16(blob, cursor) != 0) return null;
        return new String[]{type, method};
    }

    private static String readSerString(byte[] bytes, int offset) throws DllReaderException {
        if (offset < 0 || offset >= bytes.length) throw new DllReaderException("SerString truncada");
        if (u8(bytes, offset) == 0xff) return null;
        int length = readCompressed(bytes, offset);
        int prefix = compressedSize(u8(bytes, offset));
        long start = (long) offset + prefix;
        long end = start + length;
        if (end > bytes.length) throw new DllReaderException("SerString excede o blob");
        return new String(bytes, (int) start, length, StandardCharsets.UTF_8);
    }

    private static int serStringEncodedSize(byte[] bytes, int offset) throws DllReaderException {
        int length = readCompressed(bytes, offset);
        return compressedSize(u8(bytes, offset)) + length;
    }

    private String getString(int index) throws DllReaderException {
        if (index == 0) return "";
        if (index < 0 || index >= stringsStream.size) throw error("índice #Strings fora dos limites: " + index);
        int start = stringsStream.offset + index;
        int end = start;
        while (end < stringsStream.end() && data[end] != 0) {
            if (end - start >= 1024 * 1024) throw error("string metadata excede 1 MiB");
            end++;
        }
        if (end == stringsStream.end()) throw error("string #Strings sem terminador");
        return new String(data, start, end - start, StandardCharsets.UTF_8);
    }

    private byte[] getBlob(int index) throws DllReaderException {
        if (index == 0) return new byte[0];
        if (index < 0 || index >= blobStream.size) throw error("índice #Blob fora dos limites: " + index);
        int start = blobStream.offset + index;
        int length = readCompressedUInt(start, blobStream.end());
        int prefix = compressedSize(u8(start));
        long contentStart = (long) start + prefix;
        long end = contentStart + length;
        if (end > blobStream.end()) throw error("blob excede o stream #Blob");
        return Arrays.copyOfRange(data, (int) contentStart, (int) end);
    }

    private int readIndex(int offset, String kind) throws DllReaderException {
        int size;
        if (kind.startsWith("coded:")) size = codedSize(kind.substring(6));
        else size = indexSize(kind);
        requireRange(offset, size, "índice metadata");
        return size == 2 ? u16(offset) : checkedInt(u32(offset), "índice metadata");
    }

    private int tableRow(int table, int rid) throws DllReaderException {
        if (table < 0 || table >= TABLE_COUNT || rid < 1 || rid > rowCounts[table]
                || tableOffsets[table] < 0) {
            throw error("índice da tabela " + table + " fora dos limites: " + rid);
        }
        long offset = (long) tableOffsets[table] + (long) (rid - 1) * tableRowSize(table);
        return checkedInt(offset, "linha metadata");
    }

    private int methodParamListOffset() throws DllReaderException {
        return 8 + indexSize("strings") + indexSize("blob");
    }

    private void validateListRange(int start, int end, int rowCount, String label) throws DllReaderException {
        if (start < 1 || end < start || end > rowCount + 1) {
            throw error(label + " fora dos limites");
        }
    }

    private int rvaToFileOffset(long rva) throws DllReaderException {
        for (Section section : sections) {
            long span = Math.max(section.virtualSize, section.rawSize);
            long virtualEnd = section.virtualAddress + span;
            if (rva >= section.virtualAddress && rva < virtualEnd) {
                long delta = rva - section.virtualAddress;
                if (delta >= section.rawSize) throw error("RVA está em dados não armazenados no arquivo");
                long offset = section.rawOffset + delta;
                if (offset < 0 || offset >= data.length) throw error("RVA aponta fora do arquivo");
                return (int) offset;
            }
        }
        throw error("RVA não pertence a nenhuma seção PE: 0x" + Long.toHexString(rva));
    }

    private String ascii(int offset, int length) throws DllReaderException {
        requireRange(offset, length, "campo PE");
        int end = offset;
        while (end < offset + length && data[end] != 0) end++;
        return new String(data, offset, end - offset, StandardCharsets.US_ASCII);
    }

    private int readCompressedUInt(int offset, int limit) throws DllReaderException {
        if (offset < 0 || offset >= limit) throw error("inteiro comprimido truncado");
        return readCompressed(data, offset, limit);
    }

    private static int readCompressed(byte[] bytes, int offset) throws DllReaderException {
        return readCompressed(bytes, offset, bytes.length);
    }

    private static int readCompressed(byte[] bytes, int offset, int limit) throws DllReaderException {
        if (offset < 0 || offset >= limit) throw new DllReaderException("inteiro comprimido truncado");
        int first = u8(bytes, offset);
        if ((first & 0x80) == 0) return first;
        if ((first & 0xc0) == 0x80) {
            if (offset + 2 > limit) throw new DllReaderException("inteiro comprimido de 2 bytes truncado");
            return ((first & 0x3f) << 8) | u8(bytes, offset + 1);
        }
        if ((first & 0xe0) == 0xc0) {
            if (offset + 4 > limit) throw new DllReaderException("inteiro comprimido de 4 bytes truncado");
            return ((first & 0x1f) << 24) | (u8(bytes, offset + 1) << 16)
                    | (u8(bytes, offset + 2) << 8) | u8(bytes, offset + 3);
        }
        throw new DllReaderException("prefixo de inteiro comprimido reservado");
    }

    private static int compressedSize(int first) throws DllReaderException {
        if ((first & 0x80) == 0) return 1;
        if ((first & 0xc0) == 0x80) return 2;
        if ((first & 0xe0) == 0xc0) return 4;
        throw new DllReaderException("prefixo de inteiro comprimido reservado");
    }

    private void requireRange(int offset, int length, String what) throws DllReaderException {
        requireRange(offset, length, data.length, what);
    }

    private void requireRange(int offset, int length, int limit, String what) throws DllReaderException {
        if (offset < 0 || length < 0 || (long) offset + length > limit || limit > data.length) {
            throw error(what + " truncado ou fora dos limites");
        }
    }

    private int checkedAdd(int a, int b, String what) throws DllReaderException {
        return checkedInt((long) a + b, what);
    }

    private int checkedInt(long value, String what) throws DllReaderException {
        if (value < 0 || value > Integer.MAX_VALUE) throw error(what + " fora dos limites");
        return (int) value;
    }

    private DllReaderException error(String message) {
        return new DllReaderException(message);
    }

    private static String simpleName(String fullName) {
        if (fullName == null) return "";
        int dot = fullName.lastIndexOf('.');
        String name = dot < 0 ? fullName : fullName.substring(dot + 1);
        return name.endsWith("Attribute") ? name.substring(0, name.length() - 9) : name;
    }

    private static String safeMessage(RuntimeException e) {
        String message = e.getMessage();
        return message == null ? e.getClass().getSimpleName() : message;
    }

    private static void requireIl(byte[] il, int offset, int length, String opcode) throws DllReaderException {
        if (offset < 0 || length < 0 || (long) offset + length > il.length) {
            throw new DllReaderException("operando truncado no opcode " + opcode);
        }
    }

    private static int u8(byte[] bytes, int offset) throws DllReaderException {
        if (offset < 0 || offset >= bytes.length) throw new DllReaderException("leitura de byte fora dos limites");
        return bytes[offset] & 0xff;
    }

    private int u8(int offset) throws DllReaderException {
        requireRange(offset, 1, "byte");
        return data[offset] & 0xff;
    }

    private int u16(int offset) throws DllReaderException {
        requireRange(offset, 2, "inteiro de 2 bytes");
        return (data[offset] & 0xff) | ((data[offset + 1] & 0xff) << 8);
    }

    private long u32(int offset) throws DllReaderException {
        requireRange(offset, 4, "inteiro de 4 bytes");
        return (data[offset] & 0xffL) | ((data[offset + 1] & 0xffL) << 8)
                | ((data[offset + 2] & 0xffL) << 16) | ((data[offset + 3] & 0xffL) << 24);
    }

    private long i64(int offset) throws DllReaderException {
        requireRange(offset, 8, "inteiro de 8 bytes");
        return u32(offset) | (u32(offset + 4) << 32);
    }

    private static int u16(byte[] bytes, int offset) throws DllReaderException {
        if (offset < 0 || (long) offset + 2 > bytes.length) throw new DllReaderException("inteiro truncado");
        return (bytes[offset] & 0xff) | ((bytes[offset + 1] & 0xff) << 8);
    }

    private static int i32(byte[] bytes, int offset) throws DllReaderException {
        if (offset < 0 || (long) offset + 4 > bytes.length) throw new DllReaderException("inteiro truncado");
        return (bytes[offset] & 0xff) | ((bytes[offset + 1] & 0xff) << 8)
                | ((bytes[offset + 2] & 0xff) << 16) | ((bytes[offset + 3] & 0xff) << 24);
    }

    private static long u32(byte[] bytes, int offset) throws DllReaderException {
        return i32(bytes, offset) & 0xffff_ffffL;
    }

    private static long i64(byte[] bytes, int offset) throws DllReaderException {
        return (i32(bytes, offset) & 0xffff_ffffL) | ((long) i32(bytes, offset + 4) << 32);
    }

    private final class SigReader {
        private final byte[] bytes;
        private int cursor;

        SigReader(byte[] bytes) {
            this.bytes = bytes;
        }

        int readByte() throws DllReaderException {
            if (cursor >= bytes.length) throw error("assinatura CLI truncada");
            return bytes[cursor++] & 0xff;
        }

        int peek() throws DllReaderException {
            if (cursor >= bytes.length) throw error("assinatura CLI truncada");
            return bytes[cursor] & 0xff;
        }

        int readCompressed() throws DllReaderException {
            int value = DllReader.readCompressed(bytes, cursor);
            cursor += compressedSize(bytes[cursor] & 0xff);
            return value;
        }

        String readType(int depth) throws DllReaderException {
            if (depth > 16) throw error("tipo CLI aninhado demais");
            int element = readByte();
            return switch (element) {
                case 0x01 -> "void";
                case 0x02 -> "bool";
                case 0x03 -> "char";
                case 0x04 -> "int8";
                case 0x05 -> "uint8";
                case 0x06 -> "int16";
                case 0x07 -> "uint16";
                case 0x08 -> "int";
                case 0x09 -> "uint";
                case 0x0a -> "long";
                case 0x0b -> "ulong";
                case 0x0c -> "float";
                case 0x0d -> "double";
                case 0x0e -> "string";
                case 0x0f -> readType(depth + 1) + "*";
                case 0x10 -> readType(depth + 1) + "&";
                case 0x11 -> "valuetype " + readTypeName(readCompressed());
                case 0x12 -> readTypeName(readCompressed());
                case 0x13 -> "!" + readCompressed();
                case 0x14 -> readArray(depth);
                case 0x15 -> readGenericInstance(depth);
                case 0x16 -> "typedref";
                case 0x18 -> "native int";
                case 0x19 -> "native uint";
                case 0x1b -> {
                    readMethodSignatureBytes(depth + 1);
                    yield "methodptr";
                }
                case 0x1c -> "object";
                case 0x1d -> readType(depth + 1) + "[]";
                case 0x1e -> "!!" + readCompressed();
                case 0x1f, 0x20 -> {
                    readCompressed();
                    yield readType(depth + 1);
                }
                case 0x41, 0x45 -> readType(depth + 1);
                default -> throw error("tipo de elemento CLI não suportado: 0x" + Integer.toHexString(element));
            };
        }

        private String readArray(int depth) throws DllReaderException {
            String element = readType(depth + 1);
            int rank = readCompressed();
            int sizes = readCompressed();
            if (rank > 64 || sizes > rank) throw error("array CLI inválido");
            for (int i = 0; i < sizes; i++) readCompressed();
            int lowerBounds = readCompressed();
            if (lowerBounds > rank) throw error("limites de array CLI inválidos");
            for (int i = 0; i < lowerBounds; i++) readCompressed();
            return element + "[,]";
        }

        private String readGenericInstance(int depth) throws DllReaderException {
            int kind = readByte();
            if (kind != 0x11 && kind != 0x12) throw error("instância genérica CLI inválida");
            String type = readTypeName(readCompressed());
            int count = readCompressed();
            if (count > 256) throw error("tipo CLI tem argumentos genéricos demais");
            List<String> args = new ArrayList<>(count);
            for (int i = 0; i < count; i++) args.add(readType(depth + 1));
            return type + "<" + String.join(",", args) + ">";
        }

        private void readMethodSignatureBytes(int depth) throws DllReaderException {
            if (depth > 16) throw error("assinatura CLI aninhada demais");
            int convention = readByte();
            if ((convention & 0x10) != 0) readCompressed();
            int count = readCompressed();
            if (count > 1024) throw error("assinatura CLI tem parâmetros demais");
            readType(depth + 1);
            for (int i = 0; i < count; i++) readType(depth + 1);
        }

        boolean atEnd() {
            return cursor == bytes.length;
        }
    }
}
