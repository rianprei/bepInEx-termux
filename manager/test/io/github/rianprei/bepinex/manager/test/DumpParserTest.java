package io.github.rianprei.bepinex.manager.test;

import io.github.rianprei.bepinex.manager.core.DumpParser;
import io.github.rianprei.bepinex.manager.model.DumpEntry;

import java.io.BufferedReader;
import java.io.StringReader;

public class DumpParserTest {
    public static void run() {
        testHeader();
        testEntries();
        testSearchPagination();
        System.out.println("  [OK] DumpParserTest (C5)");
    }

    private static void testHeader() {
        String headerLine = "# pkg=com.hyperdotstudios.swampattack2 il2cpp_size=52428800 unity=2021.3.15f1";
        DumpParser.Header h = DumpParser.parseHeader(headerLine);
        if (h == null) throw new AssertionError("Header nulo");
        if (!"com.hyperdotstudios.swampattack2".equals(h.pkg)) throw new AssertionError("pkg incorreto");
        if (h.il2cppSize != 52428800L) throw new AssertionError("size incorreto: " + h.il2cppSize);
        if (!"2021.3.15f1".equals(h.unityVersion)) throw new AssertionError("unity incorreto");
    }

    private static void testEntries() {
        String classLine = "C\tAssembly-CSharp.dll\tNamespace.MyClass";
        DumpEntry c = DumpParser.parseLine(classLine);
        if (c == null || !DumpEntry.KIND_CLASS.equals(c.kind) || !"Assembly-CSharp.dll".equals(c.assembly) || !"Namespace.MyClass".equals(c.className)) {
            throw new AssertionError("Parse class falhou");
        }

        String methodLine = "M\tComplexCreature\tHasAmmo\t0\tSystem.Boolean\t0";
        DumpEntry m = DumpParser.parseLine(methodLine);
        if (m == null || !DumpEntry.KIND_METHOD.equals(m.kind) || !"ComplexCreature".equals(m.className) || !"HasAmmo".equals(m.name) || m.nargs != 0 || !"System.Boolean".equals(m.type) || m.isStatic) {
            throw new AssertionError("Parse method falhou");
        }

        String fieldLine = "F\tGameConfig\tGodMode\tSystem.Boolean\t1\t0x18";
        DumpEntry f = DumpParser.parseLine(fieldLine);
        if (f == null || !DumpEntry.KIND_FIELD.equals(f.kind) || !"GameConfig".equals(f.className) || !"GodMode".equals(f.name) || !f.isStatic || !"0x18".equals(f.offset)) {
            throw new AssertionError("Parse field falhou");
        }
    }

    private static void testSearchPagination() {
        StringBuilder sb = new StringBuilder();
        sb.append("# pkg=com.test il2cpp_size=1000\n");
        for (int i = 0; i < 200; i++) {
            sb.append("M\tClass").append(i).append("\tDoAction\t1\tSystem.Void\t0\n");
            sb.append("M\tClass").append(i).append("\tHasAmmo\t0\tSystem.Boolean\t0\n");
            sb.append("F\tClass").append(i).append("\t_health\tSystem.Int32\t0\t0x10\n");
        }

        try {
            // Pagina 1: buscar HasAmmo, limit 10, offset 0
            BufferedReader r1 = new BufferedReader(new StringReader(sb.toString()));
            DumpParser.SearchResult res1 = DumpParser.search(r1, "HasAmmo", DumpEntry.KIND_METHOD, 0, 10);
            if (res1.entries.size() != 10) throw new AssertionError("esperado 10 resultados, obteve " + res1.entries.size());
            if (!res1.hasMore) throw new AssertionError("deveria ter mais resultados");
            if (!"HasAmmo".equals(res1.entries.get(0).name)) throw new AssertionError("metodo incorreto");

            // Pagina 2: offset 10, limit 10
            BufferedReader r2 = new BufferedReader(new StringReader(sb.toString()));
            DumpParser.SearchResult res2 = DumpParser.search(r2, "HasAmmo", DumpEntry.KIND_METHOD, 10, 10);
            if (res2.entries.size() != 10) throw new AssertionError("esperado 10 resultados na pag 2");
            if (!"Class10".equals(res2.entries.get(0).className)) throw new AssertionError("classe incorreta na pag 2: " + res2.entries.get(0).className);

            // Filtro por tipo inexistente
            BufferedReader r3 = new BufferedReader(new StringReader(sb.toString()));
            DumpParser.SearchResult res3 = DumpParser.search(r3, "InexistenteQueryXYZ", null, 0, 10);
            if (!res3.entries.isEmpty() || res3.hasMore) throw new AssertionError("esperado vazio");
        } catch (Exception e) {
            throw new RuntimeException(e);
        }
    }
}
