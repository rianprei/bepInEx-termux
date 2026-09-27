package io.github.rianprei.bepinex.manager.test;

import io.github.rianprei.bepinex.manager.core.DllReader;
import io.github.rianprei.bepinex.manager.core.HarmonyTranslator;
import io.github.rianprei.bepinex.manager.core.PatchGenerator;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.List;

/**
 * Teste do tradutor dll2patch: lê a fixture .dll, traduz para C4, valida
 * contra o parser C4 existente, e testa casos não traduzíveis + fuzz.
 */
public final class Dll2PatchTest {

    private static int failures = 0;

    public static void run() throws Exception {
        System.out.println("=== Dll2PatchTest: tradutor DLL Harmony -> C4 ===");

        // Caminho relativo à raiz do repo
        Path dllPath = Paths.get("../test/fixtures/dll2patch_fixture.dll");
        if (!Files.exists(dllPath)) {
            dllPath = Paths.get("test/fixtures/dll2patch_fixture.dll");
        }
        if (!Files.exists(dllPath)) {
            System.out.println("  [SKIP] fixture .dll não encontrada: " + dllPath.toAbsolutePath());
            return;
        }

        byte[] dllData = Files.readAllBytes(dllPath);
        System.out.println("  DLL carregada: " + dllData.length + " bytes");

        // 1. Traduzir
        HarmonyTranslator.TranslationResult result = HarmonyTranslator.translate(dllData);
        List<String> patchLines = result.patchLines();
        List<String> reportLines = result.reportLines();

        System.out.println("  Patch lines geradas: " + patchLines.size());
        for (String line : patchLines) {
            System.out.println("    " + line);
        }
        System.out.println("  Report lines: " + reportLines.size());
        for (String line : reportLines) {
            System.out.println("    " + line);
        }

        // 2. Validar cada patch line com o parser C4 existente
        for (String line : patchLines) {
            List<?> rules = PatchGenerator.parse(line);
            check("patch line válida no C4: " + line, !rules.isEmpty());
        }

        // 3. Verificar casos específicos
        check("Caso A (Prefix return const): 1 patch line", patchLines.stream().anyMatch(l -> l.contains("return") && l.contains("GetHealth") && l.contains("100")));
        check("Caso B (Postfix return const): 1 patch line", patchLines.stream().anyMatch(l -> l.contains("return") && l.contains("GetMana") && l.contains("50")));
        check("Caso C (Postfix mul): 1 patch line", patchLines.stream().anyMatch(l -> l.contains("mul") && l.contains("GetDamage") && l.contains("2")));
        check("Caso D (static field): 1 patch line", patchLines.stream().anyMatch(l -> l.contains("static") && l.contains("GetScore") && l.contains("9999")));

        // 4. Casos não traduzíveis
        check("Caso E (Transpiler): 0 patch lines", patchLines.stream().noneMatch(l -> l.contains("GetAmmo")));
        check("Caso F (if): 0 patch lines", patchLines.stream().noneMatch(l -> l.contains("GetShield")));
        check("Caso G (method call): 0 patch lines", patchLines.stream().noneMatch(l -> l.contains("GetSpeed")));
        check("Caso H (string): 0 patch lines", patchLines.stream().noneMatch(l -> l.contains("GetName")));
        check("Caso I (overload): 0 patch lines", patchLines.stream().noneMatch(l -> l.contains("GetStat")));

        // 5. Fuzz: 500 mutações de byte
        System.out.println("  Fuzz: 500 mutações de byte...");
        int fuzzFailures = 0;
        for (int i = 0; i < 500; i++) {
            byte[] mutated = dllData.clone();
            int pos = (int) (Math.random() * mutated.length);
            mutated[pos] = (byte) (Math.random() * 256);
            try {
                HarmonyTranslator.translate(mutated);
            } catch (DllReader.DllReaderException e) {
                // Esperado: exceção controlada
            } catch (Exception e) {
                fuzzFailures++;
                System.out.println("    [FUZZ FAIL] mutação " + i + ": " + e.getClass().getSimpleName() + ": " + e.getMessage());
            }
        }
        check("fuzz: 500 mutações sem crash", fuzzFailures == 0);

        // 6. Teste da tabela de opcodes (verificar se o DllReader decodifica corretamente)
        System.out.println("  Teste da tabela de opcodes...");
        byte[] testIl = new byte[] {0x02, 0x1F, 0x64, 0x54, 0x16, 0x2A};
        List<DllReader.DecodedInstruction> decoded = DllReader.decodeIl(testIl);
        check("decode IL: 5 instruções", decoded.size() == 5);
        check("decode IL[0]: ldarg.0", "ldarg.0".equals(decoded.get(0).opcode()));
        check("decode IL[1]: ldc.i4.s", "ldc.i4.s".equals(decoded.get(1).opcode()));
        check("decode IL[1]: operando 100", decoded.get(1).operand() == 100);
        check("decode IL[2]: stind.i4", "stind.i4".equals(decoded.get(2).opcode()));
        check("decode IL[3]: ldc.i4.0", "ldc.i4.0".equals(decoded.get(3).opcode()));
        check("decode IL[4]: ret", "ret".equals(decoded.get(4).opcode()));

        if (failures == 0) {
            System.out.println("  [PASS] Dll2PatchTest: todos os testes passaram");
        } else {
            System.out.println("  [FAIL] Dll2PatchTest: " + failures + " falhas");
            throw new AssertionError("Dll2PatchTest falhou com " + failures + " erros");
        }
    }

    private static void check(String name, boolean cond) {
        if (!cond) {
            failures++;
            System.out.println("    [FAIL] " + name);
        } else {
            System.out.println("    [PASS] " + name);
        }
    }
}
