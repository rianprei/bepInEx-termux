package io.github.rianprei.bepinex.manager.test;

import io.github.rianprei.bepinex.manager.core.DllReader;
import io.github.rianprei.bepinex.manager.core.ModContentDetector;
import io.github.rianprei.bepinex.manager.core.HarmonyTranslator;
import io.github.rianprei.bepinex.manager.core.PatchGenerator;
import io.github.rianprei.bepinex.manager.model.PatchRule;

import java.io.BufferedReader;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Arrays;
import java.util.List;
import java.util.Random;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;

public final class Dll2PatchTest {
    private static int failures;

    private Dll2PatchTest() {}

    public static void run() throws Exception {
        System.out.println("=== Dll2PatchTest: tradutor DLL Harmony -> C4 ===");
        Path fixture = findPath("test/fixtures/dll2patch_fixture.dll");
        byte[] dll = Files.readAllBytes(fixture);
        DllReader reader = DllReader.parse(dll);
        testFacts(reader);
        testReturnParameterMetadata(reader);
        testExternalHarmonyAttributes(reader);
        testTranslation(dll, reader);
        testOpcodes();
        testTableIndexGuard(reader);
        testDetectorIntegration(dll);
        testFuzz(dll);
        if (failures != 0) throw new AssertionError("Dll2PatchTest falhou com " + failures + " erros");
        System.out.println("  [OK] Dll2PatchTest: todos os testes passaram");
    }

    /**
     * O tradutor produz TEXTO C4, e o texto nao tem extensao. O que decide o
     * nome do arquivo instalado e o ModContentDetector, que decide pelo
     * CONTEUDO. Este teste fecha a ligacao: a saida do tradutor tem que ser
     * reconhecida como regra e virar &lt;id&gt;.bpatch.
     *
     * Sem ele, o tradutor continua correto e o Manager entrega um arquivo com a
     * extensao errada — e nada no gate acusaria, porque cada lado passa
     * sozinho.
     */
    private static void testDetectorIntegration(byte[] dll) throws Exception {
        HarmonyTranslator.TranslationResult result = HarmonyTranslator.translate(dll);
        String text = result.patchText();
        check("o tradutor produz texto nao vazio", !text.isEmpty());

        // 1. o texto traduzido vira um Sample do detector. O NOME e neutro (o
        //    id vem do nome), e o CONTEUDO que tem que decidir.
        ModContentDetector.Sample sample = rulesSample("meu_hack", text);
        ModContentDetector.Detection d = ModContentDetector.detect(sample, true);
        check("a saida do tradutor e reconhecida como regra (Kind.PATCH)",
                d != null && d.kind == ModContentDetector.Kind.PATCH);
        check("a extensao instalada e a nova (RULES_EXT)",
                d != null && ModContentDetector.RULES_EXT.equals(d.targetExt));
        check("o id sai do nome do arquivo", d != null && "meu_hack".equals(d.targetId));
        check("e instala", d != null && d.installable);

        // 2. o MESMO texto com um nome ".patch" (extensao antiga) tem que dar o
        //    MESMO resultado: e o que garante que o item 3 do rename continua
        //    valendo depois que o tradutor entrou.
        ModContentDetector.Detection legacy = ModContentDetector.detect(
                rulesSample("meu_hack.patch", text), true);
        check("com a extensao antiga da o mesmo destino",
                legacy != null && ModContentDetector.RULES_EXT.equals(legacy.targetExt)
                        && "meu_hack".equals(legacy.targetId));

        // 3. e sem extensao nenhuma tambem.
        ModContentDetector.Detection noExt = ModContentDetector.detect(
                rulesSample("meu_hack_sem_ext", text), true);
        check("sem extensao da o mesmo destino",
                noExt != null && ModContentDetector.RULES_EXT.equals(noExt.targetExt));

        // 4. as recusas estritas continuam recusando. O tradutor e对手 de um
        //    detector permissivo: texto que nao e C4 tem que continuar sendo
        //    "isto nao e um mod", e nao virar um .bpatch vazio.
        ModContentDetector.Detection junk = ModContentDetector.detect(
                rulesSample("lixo.bpatch", "isto aqui nao e regra nenhuma\n"), true);
        check("texto que nao e C4 continua recusado (nao virou .bpatch)",
                junk != null && junk.kind == ModContentDetector.Kind.TEXT_OTHER
                        && !junk.installable);

        // 5. o nome sugerido para quem for gravar o arquivo tem que usar a
        //    extensao da constante, e nao um literal novo.
        check("o nome sugerido usa RULES_EXT",
                ("meu_hack" + ModContentDetector.RULES_EXT)
                        .equals(ModContentDetector.rulesFileName("meu_hack")));
    }

    /** A Sample que o detector espera: (fileName, head, text, zip?, frida?). */
    private static ModContentDetector.Sample rulesSample(String name, String text) {
        byte[] head = text.getBytes(StandardCharsets.UTF_8);
        byte[] first = new byte[Math.min(head.length, 64)];
        System.arraycopy(head, 0, first, 0, first.length);
        return new ModContentDetector.Sample(name, first, text, false, false, head.length);
    }

    private static void testTranslation(byte[] dll, DllReader reader) throws Exception {
        HarmonyTranslator.TranslationResult result = HarmonyTranslator.translate(dll);
        // A saída REAL do tradutor vira artefato de gate: este teste grava o
        // patchText() de verdade em test/fixtures/dll2patch/translator_output.bpatch
        // e o harness C++ do u_patch ([Caso 80] do upatch_harness.cpp) repassa cada
        // linha pelas funções REAIS de u_patch_parse.h. Nenhuma réplica Java no meio
        // do caminho: a gravação vem ANTES das checagens para que o gate C++ julgue
        // o que o tradutor emitir, mesmo quando alguma checagem daqui falha.
        Path deviceGatePatch = findPath("test/fixtures/dll2patch/opcodes_table.csv").getParent()
                .resolve("translator_output.bpatch");
        byte[] patchBytes = result.patchText().getBytes(StandardCharsets.UTF_8);
        // Achado #1: o arquivo é COMMITADO e regerado a cada rodada — se nada
        // conferir, a cópia versionada envelhece em silêncio (o teste sobrescreve
        // antes de qualquer um ler). A cópia commitada tem que ser EXATAMENTE a
        // saída de hoje; divergiu, o gate falha e o diff sujo do worktree é a
        // correção pronta pra commitar junto com o golden novo.
        byte[] committed = Files.isRegularFile(deviceGatePatch)
                ? Files.readAllBytes(deviceGatePatch) : new byte[0];
        check("fixture versionada (translator_output.bpatch) bate com a saída gerada"
                        + " — commit a nova quando o golden mudar",
                Arrays.equals(committed, patchBytes));
        Files.write(deviceGatePatch, patchBytes);
        check("saída real do tradutor gravada para o gate C++ (translator_output.bpatch)",
                Arrays.equals(Files.readAllBytes(deviceGatePatch), patchBytes));
        String expected = "# traduzido de Harmony para C4\n"
                + "return Dll2PatchFixture.GameClass GetHealth 0 int 100\n"
                + "return Dll2PatchFixture.GameClass GetMana 0 int 50\n"
                // SEM a linha "static ... MaxScore": o tradutor do uni/dll2patch2
                // (4207b99) passou a RECUSAR a atribuição static — ela é o
                // caminho inseguro (escreve num campo que pode ser de
                // instancia, não estático), e a expectativa antiga ainda pedia
                // a linha. A semântica do dll2patch2 vence aqui; o lado da
                // uni/bpatch carregava a expectativa do tradutor mais velho
                // (70ad114), que ainda emitia static.
                + "mul Dll2PatchFixture.GameClass GetDamage 0 int 2\n";
        // "texto C4 traduzido" e não ".patch": o nome da extensão mudou para
        // .bpatch no rename, e um rótulo de check que diz ".patch" seria uma
        // ocorrência da extensão velha na varredura final.
        check("a-d: o texto C4 traduzido e exatamente o esperado",
                expected.equals(result.patchText()));

        // Achado #2: a réplica Java foi removida (049baeb) e com ela a
        // checagem de que cada regra gerada APONTA para classe/método/campo
        // que EXISTEM na DLL. Reposta sem réplica: quem quebra a linha em
        // campos é o PatchGenerator (código de PRODUÇÃO do Manager, o mesmo
        // que vai rodar no APK) e quem decide a existência é o DllReader
        // vivo (fullName + paramCount) — nenhum clone do parser do u_patch.
        for (String generated : result.patchLines()) {
            List<PatchRule> parsed = PatchGenerator.parse(generated);
            check("regra gerada é C4 válido para o Manager: " + generated,
                    parsed.size() == 1);
            if (parsed.size() == 1) {
                check("classe/método/campo da regra existem na DLL: " + generated,
                        targetExistsInDll(parsed.get(0), reader));
            }
        }

        // A validação do lado do u_patch mora no parser C++ REAL: o Caso 80 do
        // upatch_harness parseia o translator_output.bpatch gravado acima com
        // up_parse_line/up_split_class (a mesma fixture compartilhada C4,
        // test/fixtures/c4_lines.tsv, já corria no harness). A réplica Java do
        // parser (deviceParseLine & cia.) foi REMOVIDA: clone de parser
        // divergiria do C++ sem ninguém notar — o gate agora é o próprio C++.

        report(result, "GetAmmo", "usa Transpiler");
        report(result, "GetShield", "tem if");
        report(result, "GetSpeed", "chama outros métodos");
        report(result, "GetName", "tipo string não suportado");
        report(result, "GetStat", "overload ambíguo");
        report(result, "GetScore", "cadência diferente: Harmony escreve a cada chamada");
        report(result, "GetOtherScore", "cadência diferente: Harmony escreve a cada chamada");
        report(result, "GetNested", "classe aninhada: o u_patch ainda não localiza esse tipo de classe");
        check("cada alvo recusado não tem regra gerada",
                result.patchLines().stream().noneMatch(line -> line.contains("GetAmmo")
                        || line.contains("GetShield") || line.contains("GetSpeed")
                        || line.contains("GetName") || line.contains("GetStat")
                        || line.contains("GetScore") || line.contains("GetOtherScore")
                        || line.contains("GetNested")));
        String nestedRule = "return Dll2PatchFixture.Outer/Inner GetNested 0 int 1";
        check("Manager aceita sintaxe C4 aninhada (o Caso 80 do harness C++ prova que "
                        + "o split do u_patch não resolve essa classe)",
                PatchGenerator.parse(nestedRule).size() == 1);

        DllReader.MethodInfo transpiler = findMethod(reader, "TranspilerCase", "Transpiler");
        check("fixture exercita cabeçalho IL fat", transpiler != null && transpiler.fatHeader());
    }

    private static void testReturnParameterMetadata(DllReader reader) throws Exception {
        DllReader.MethodInfo method = findMethod(reader, "ReturnParameterMetadataCase", "ReturnAnnotated");
        check("Param com sequência zero é metadado de retorno, não argumento",
                method != null && "bool".equals(method.returnType()) && method.parameters().isEmpty());
    }

    private static boolean targetExistsInDll(PatchRule rule, DllReader reader)
            throws DllReader.DllReaderException {
        for (DllReader.TypeInfo type : reader.getTypes()) {
            if (!type.fullName().equals(rule.targetClass)) continue;
            if ("return".equals(rule.action) || "mul".equals(rule.action)) {
                return type.methods().stream().anyMatch(method ->
                        method.name().equals(rule.member) && method.paramCount() == rule.nargs);
            }
            return type.fields().stream().anyMatch(field -> field.name().equals(rule.member));
        }
        return false;
    }

    private static void testExternalHarmonyAttributes(DllReader reader) throws Exception {
        // A fixture referencia HarmonyLib como assembly EXTERNO (harmony-stubs),
        // como um mod real: os atributos chegam como MemberRefs de construtor
        // cujo pai é um TypeRef de fora. O DllReader tem que resolver o nome do
        // ATRIBUTO (HarmonyLib.HarmonyPatch), não do construtor.
        check("construtores Harmony externos resolvem o nome do atributo",
                reader.getCustomAttributes().stream().anyMatch(attribute ->
                        "HarmonyLib.HarmonyPatch".equals(attribute.typeName())));
    }

    private static void testFacts(DllReader reader) throws Exception {
        List<String> facts = Files.readAllLines(findPath("test/fixtures/dll2patch/pe_facts.tsv"),
                StandardCharsets.UTF_8);
        boolean metadataMatched = false;
        int sectionCount = 0;
        int methodCount = 0;
        for (String line : facts) {
            String[] fields = line.split("\t");
            switch (fields[0]) {
                case "metadata" -> {
                    metadataMatched = reader.metadataStartOffset() == Integer.parseInt(fields[1])
                            && reader.metadataSize() == Integer.parseInt(fields[2]);
                }
                case "section" -> {
                    int virtualAddress = Integer.parseInt(fields[2]);
                    long virtualSize = Long.parseLong(fields[3]);
                    long rawSize = Long.parseLong(fields[4]);
                    long rawOffset = Long.parseLong(fields[5]);
                    boolean match = reader.sections().stream().anyMatch(section ->
                            section.name().equals(fields[1])
                                    && section.virtualAddress() == virtualAddress
                                    && section.virtualSize() == virtualSize
                                    && section.rawSize() == rawSize
                                    && section.rawOffset() == rawOffset);
                    check("section PEReader " + fields[1], match);
                    sectionCount++;
                }
                case "rows" -> {
                    int table = tableNumber(fields[1]);
                    check("row count PEReader " + fields[1],
                            reader.tableRowCount(table) == Integer.parseInt(fields[2]));
                }
                case "method" -> {
                    int rid = Integer.parseInt(fields[1]);
                    String name = fields[2];
                    long rva = Long.parseLong(fields[3]);
                    int params = Integer.parseInt(fields[4]);
                    DllReader.MethodInfo method = reader.getMethodByRid(rid);
                    check("MethodDef PEReader #" + rid, method.name().equals(name)
                            && method.rva() == rva);
                    check("método PEReader #" + rid + " tem Param rows válidas",
                            params <= method.parameters().size() + 1);
                    methodCount++;
                }
                default -> throw new AssertionError("fact PEReader desconhecido: " + line);
            }
        }
        check("metadata PEReader (MetadataStartOffset + size)", metadataMatched);
        check("todas as seções PEReader conferidas", sectionCount >= 1
                && reader.sections().size() == sectionCount);
        check("todos os MethodDef PEReader conferidos", methodCount == reader.tableRowCount(6));
    }

    private static void testOpcodes() throws Exception {
        Path csv = findPath("test/fixtures/dll2patch/opcodes_table.csv");
        int checked = 0;
        try (BufferedReader input = Files.newBufferedReader(csv, StandardCharsets.UTF_8)) {
            String row = input.readLine();
            if (!"Value,Name,OperandType,Size".equals(row)) {
                throw new AssertionError("cabeçalho opcodes_table.csv inválido");
            }
            while ((row = input.readLine()) != null) {
                String[] columns = row.split(",", -1);
                if (columns.length != 4) throw new AssertionError("linha de opcode inválida: " + row);
                if (columns[1].startsWith("prefix")) continue;
                int value = Integer.parseInt(columns[0].substring(2), 16);
                byte[] il = opcodeBytes(value, Integer.parseInt(columns[3]), columns[2]);
                List<DllReader.DecodedInstruction> decoded = DllReader.decodeIl(il);
                check("OpCodes " + columns[1], decoded.size() == 1
                        && decoded.get(0).opcode().equals(columns[1])
                        && decoded.get(0).operandType().equals(columns[2]));
                checked++;
            }
        }
        check("tabela de opcodes não vazia", checked > 200);
    }

    private static byte[] opcodeBytes(int value, int opcodeSize, String operandType) {
        int operandSize = switch (operandType) {
            case "InlineNone" -> 0;
            case "ShortInlineI", "ShortInlineVar", "ShortInlineBrTarget" -> 1;
            case "InlineVar" -> 2;
            case "InlineI", "InlineBrTarget", "ShortInlineR", "InlineField",
                    "InlineMethod", "InlineSig", "InlineString", "InlineTok", "InlineType" -> 4;
            case "InlineI8", "InlineR" -> 8;
            case "InlineSwitch" -> 4;
            default -> throw new AssertionError("OperandType não coberto: " + operandType);
        };
        byte[] result = new byte[opcodeSize + operandSize];
        if (opcodeSize == 2) {
            result[0] = (byte) 0xfe;
            result[1] = (byte) value;
        } else {
            result[0] = (byte) value;
        }
        return result;
    }

    private static void testTableIndexGuard(DllReader reader) throws Exception {
        Method tableRow = DllReader.class.getDeclaredMethod("tableRow", int.class, int.class);
        tableRow.setAccessible(true);
        try {
            tableRow.invoke(reader, 6, reader.tableRowCount(6) + 1);
            check("índice de tabela fora do limite rejeitado", false);
        } catch (InvocationTargetException e) {
            check("índice de tabela fora do limite rejeitado",
                    e.getCause() instanceof DllReader.DllReaderException);
        }
    }

    private static void testFuzz(byte[] dll) throws Exception {
        Random random = new Random(0xD112A7C4L);
        int completed = 0;
        for (int i = 0; i < 500; i++) {
            byte[] mutated = dll.clone();
            mutated[random.nextInt(mutated.length)] = (byte) random.nextInt(256);
            var executor = Executors.newSingleThreadExecutor(task -> {
                Thread thread = new Thread(task, "dll2patch-fuzz");
                thread.setDaemon(true);
                return thread;
            });
            Future<?> future = executor.submit(() -> {
                try {
                    HarmonyTranslator.translate(mutated);
                } catch (DllReader.DllReaderException expected) {
                    return;
                } catch (Exception unexpected) {
                    throw new IllegalStateException(unexpected);
                }
            });
            try {
                future.get(1, TimeUnit.SECONDS);
                completed++;
            } catch (TimeoutException e) {
                future.cancel(true);
                check("fuzz 500 mutações sem travar (caso " + i + ")", false);
                break;
            } catch (ExecutionException e) {
                check("fuzz só encerra normalmente ou com erro controlado (caso " + i + ")",
                        false);
                break;
            } finally {
                executor.shutdownNow();
            }
        }
        check("fuzz determinístico: 500 mutações controladas", completed == 500);
    }

    private static DllReader.MethodInfo findMethod(DllReader reader, String typeSuffix, String name)
            throws DllReader.DllReaderException {
        for (DllReader.TypeInfo type : reader.getTypes()) {
            if (!type.fullName().endsWith("." + typeSuffix)) continue;
            for (DllReader.MethodInfo method : type.methods()) {
                if (method.name().equals(name)) return method;
            }
        }
        return null;
    }

    private static void report(HarmonyTranslator.TranslationResult result, String target,
                               String requiredReason) {
        long matching = result.reportLines().stream()
                .filter(line -> line.contains(target) && line.contains(requiredReason)).count();
        check(target + " recusado com motivo concreto", matching == 1
                && result.patchLines().stream().noneMatch(line -> line.contains(target)));
    }

    private static int tableNumber(String name) {
        return switch (name) {
            case "TypeDef" -> 2;
            case "MethodDef" -> 6;
            case "CustomAttribute" -> 12;
            case "MemberRef" -> 10;
            case "TypeRef" -> 1;
            case "Field" -> 4;
            case "Param" -> 8;
            default -> throw new AssertionError("tabela PEReader desconhecida: " + name);
        };
    }

    private static Path findPath(String relative) {
        Path directory = Paths.get("").toAbsolutePath();
        for (Path current = directory; current != null; current = current.getParent()) {
            Path candidate = current.resolve(relative);
            if (Files.isRegularFile(candidate)) return candidate;
        }
        throw new AssertionError("arquivo de fixture ausente: " + relative);
    }

    private static void check(String name, boolean condition) {
        if (!condition) {
            failures++;
            System.out.println("  [FAIL] " + name);
        }
    }
}
