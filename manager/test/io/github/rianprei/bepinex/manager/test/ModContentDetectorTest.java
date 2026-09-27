package io.github.rianprei.bepinex.manager.test;

import io.github.rianprei.bepinex.manager.core.ModContentDetector;
import io.github.rianprei.bepinex.manager.core.ModContentDetector.Detection;
import io.github.rianprei.bepinex.manager.core.ModContentDetector.Kind;
import io.github.rianprei.bepinex.manager.core.ModContentDetector.Sample;

import java.nio.charset.StandardCharsets;

public class ModContentDetectorTest {
    public static void run() {
        testBmod();
        testElfArm64();
        testElfOtherArch();
        testFridaGadget();
        testPatch();
        testFridaJs();
        testDotNet();
        testOtherBinaries();
        testBaseId();
        testElfMalformed();
        System.out.println("  [OK] ModContentDetectorTest (C7)");
    }

    private static void check(String what, boolean cond) {
        if (!cond) throw new AssertionError("falhou: " + what);
    }

    // --- amostras sintéticas -------------------------------------------------

    private static final String BMOD_MANIFEST_JSON =
            "{\"format\":1,\"id\":\"meu\",\"name\":\"Meu Mod\",\"engine\":\"unity-il2cpp\"," 
                    + "\"type\":\"native\",\"game\":\"*\"}";

    private static Sample zip(boolean withManifest) {
        byte[] head = {'P', 'K', 3, 4, 0, 0, 0, 0};
        byte[] manifest = withManifest
                ? BMOD_MANIFEST_JSON.getBytes(java.nio.charset.StandardCharsets.UTF_8)
                : null;
        return new Sample("mod.bmod", head, null, withManifest, false, head.length,
                manifest, null);
    }

    /**
     * ELF64 LE minimo e COERENTE: cabecalho de 64 bytes, uma tabela com um
     * PT_LOAD de 256 bytes em 0x1000, arquivo de 0x2000 bytes. E o .so
     * sintetico que o C7 precisa aceitar.
     */
    private static Sample elf(String name, int machine) {
        byte[] h = new byte[4096];
        h[0] = 0x7F; h[1] = 'E'; h[2] = 'L'; h[3] = 'F';
        h[4] = 2;          // ELFCLASS64
        h[5] = 1;          // ELFDATA2LSB
        h[6] = 1;          // EV_CURRENT
        put16(h, 16, 3);   // e_type = ET_DYN (biblioteca)
        put16(h, 18, machine);
        put32(h, 20, 1);   // e_version
        put64(h, 32, 64);  // e_phoff = logo depois do cabecalho
        put16(h, 52, 64);  // e_ehsize
        put16(h, 54, 56);  // e_phentsize
        put16(h, 56, 1);   // e_phnum
        // programa 0 em 64
        put32(h, 64, 1);   // p_type = PT_LOAD
        put32(h, 64 + 4, 5);   // p_flags = R+X
        put64(h, 64 + 8, 0x1000L);   // p_offset
        put64(h, 64 + 16, 0x1000L);  // p_vaddr
        put64(h, 64 + 24, 0x1000L);  // p_paddr
        put64(h, 64 + 32, 256);      // p_filesz
        put64(h, 64 + 40, 256);      // p_memsz
        put64(h, 64 + 48, 0x1000L);  // p_align
        return new Sample(name, h, null, false, false, 0x2000L);
    }

    private static void put16(byte[] b, int off, int v) {
        b[off] = (byte) v;
        b[off + 1] = (byte) (v >> 8);
    }

    private static void put32(byte[] b, int off, int v) {
        b[off] = (byte) v;
        b[off + 1] = (byte) (v >> 8);
        b[off + 2] = (byte) (v >> 16);
        b[off + 3] = (byte) (v >> 24);
    }

    private static void put64(byte[] b, int off, long v) {
        for (int i = 0; i < 8; i++) b[off + i] = (byte) (v >> (8 * i));
    }

    private static Sample text(String name, String content) {
        return new Sample(name, content.getBytes(StandardCharsets.UTF_8), content, false, false);
    }

    private static Sample pe(String name, String content) {
        byte[] h = new byte[64];
        h[0] = 'M'; h[1] = 'Z';
        return new Sample(name, h, content, false, false);
    }

    // --- casos ---------------------------------------------------------------

    private static void testBmod() {
        Detection d = ModContentDetector.detect(zip(true), true);
        check("zip com manifest = BMOD", d.kind == Kind.BMOD);
        check("bmod instala pelo fluxo C2", d.installable);

        Detection p = ModContentDetector.detect(zip(false), true);
        check("zip sem manifest nao instala", p.kind == Kind.ZIP_PLAIN && !p.installable);
        check("explica que .zip != .bmod", p.reason.contains(".bmod"));
    }

    private static void testElfArm64() {
        Detection d = ModContentDetector.detect(elf("meu_mod.so", 183), true);
        check("ELF arm64 instala", d.kind == Kind.ELF_ARM64 && d.installable);
        check("ext .so", ".so".equals(d.targetExt));
        check("id sem extensao", "meu_mod".equals(d.targetId));
    }

    private static void testElfOtherArch() {
        Detection x86 = ModContentDetector.detect(elf("mod_x86_64.so", 62), true);
        check("x86-64 nao instala", x86.kind == Kind.ELF_OTHER_ARCH && !x86.installable);
        check("diz qual arquitetura", x86.reason.contains("x86-64"));

        Detection arm32 = ModContentDetector.detect(elf("mod32.so", 40), true);
        check("ARM 32 nao instala", arm32.kind == Kind.ELF_OTHER_ARCH && !arm32.installable);
    }

    // C7 + item (c): o gadget NUNCA entra como .so de mod.
    private static void testFridaGadget() {
        Detection peloNome = ModContentDetector.detect(elf("frida-gadget-17.19.0-android-arm64.so", 183), true);
        check("gadget por nome nao instala", peloNome.kind == Kind.FRIDA_GADGET && !peloNome.installable);
        check("explica o modo listen", peloNome.reason.contains("listen"));
        check("manda usar .bin + .config", peloNome.reason.contains("frida-gadget.bin")
                && peloNome.reason.contains("frida-gadget.config"));
        check("gadget nao ganha .so", peloNome.targetExt == null && peloNome.targetId == null);

        Detection pelaMarca = ModContentDetector.detect(
                new Sample("helper.so", elf("helper.so", 183).head, null, false, true), true);
        check("gadget por assinatura nao instala", pelaMarca.kind == Kind.FRIDA_GADGET && !pelaMarca.installable);

        // .so normal continua passando: a regra nao pode ser "bloqueia tudo".
        Detection normal = ModContentDetector.detect(elf("u_dump.so", 183), true);
        check("so nome com 'u_dump' nao e gadget", normal.kind == Kind.ELF_ARM64);

        // Nome sem "frida"/"gadget" mas com id reservado tambem nao vira mod.
        Detection curto = ModContentDetector.detect(elf("frida.so", 183), true);
        check("id frida e reservado", curto.kind == Kind.FRIDA_GADGET && !curto.installable);
        check("id frida nao ganha .so", curto.targetExt == null);

        // Nada de deteccao pode devolver .so para o gadget, em hipotese nenhuma.
        check("gadget nunca vira .so", !".so".equals(peloNome.targetExt)
                && !".so".equals(pelaMarca.targetExt) && !".so".equals(curto.targetExt));
    }

    private static void testPatch() {
        String patch = "# regras\n"
                + "return ComplexCreature HasAmmo 0 bool true\n"
                + "field WeaponInfo unlimitedAmmo bool true Update 1\n";
        Detection d = ModContentDetector.detect(text("minhas_regras.patch", patch), true);
        check("texto C4 = patch", d.kind == Kind.PATCH && d.installable);
        check("ext .patch", ".patch".equals(d.targetExt));
        check("id do arquivo", "minhas_regras".equals(d.targetId));
    }

    private static void testFridaJs() {
        String js = "Java.perform(function() {\n"
                + "  var M = Process.getModuleByName('libunity.so');\n"
                + "  Interceptor.attach(M.getExportByName('Update'), { onEnter: function () {} });\n"
                + "});\n";
        Detection d = ModContentDetector.detect(text("meu_script.js", js), true);
        check("script Frida = js", d.kind == Kind.FRIDA_JS && d.installable);
        check("ext .js", ".js".equals(d.targetExt));
        check("id do script", "meu_script".equals(d.targetId));

        // .patch tem prioridade se o texto tiver regra C4 E marca de JS.
        Detection mix = ModContentDetector.detect(
                text("mix.txt", "Interceptor.attach(x);\nreturn Foo Bar 0 int 1\n"), true);
        check("regra C4 ganha de marca JS", mix.kind == Kind.PATCH);
    }

    private static void testDotNet() {
        String mono = "mscorlib System.Reflection AssemblyRef BepInEx HarmonyX MonoBehaviour";
        String il2cpp = "mscorlib System.Reflection Il2CppInterop Il2CppDomain BaseLib";

        Detection monoIl2cpp = ModContentDetector.detect(pe("tabs.dll", mono), true);
        check(".dll Mono em jogo IL2CPP nao roda", monoIl2cpp.kind == Kind.DOTNET_MONO && !monoIl2cpp.installable);
        check("diz NAO RODA", monoIl2cpp.reason.contains("NAO RODA"));
        check("aponta o Mod Maker", monoIl2cpp.reason.contains("Mod Maker"));

        Detection monoMono = ModContentDetector.detect(pe("tabs.dll", mono), false);
        check(".dll Mono em jogo Mono = F13", monoMono.kind == Kind.DOTNET_MONO && !monoMono.installable);
        check("cita F13", monoMono.reason.contains("F13"));

        Detection dIl2cpp = ModContentDetector.detect(pe("mod.dll", il2cpp), false);
        check(".dll IL2CPP = F12", dIl2cpp.kind == Kind.DOTNET_IL2CPP && !dIl2cpp.installable);
        check("cita F12", dIl2cpp.reason.contains("F12"));
    }

    private static void testOtherBinaries() {
        byte[] pe = new byte[64];
        pe[0] = 'M'; pe[1] = 'Z';
        Detection win = ModContentDetector.detect(new Sample("jogo.exe", pe, null, false, false), true);
        check("PE nativo = Windows", win.kind == Kind.PE_NATIVE && !win.installable);

        byte[] mach = {(byte) 0xCF, (byte) 0xFA, (byte) 0xED, (byte) 0xFE, 0, 0, 0, 0};
        Detection mac = ModContentDetector.detect(new Sample("lib.dylib", mach, null, false, false), true);
        check("Mach-O = iOS/macOS", mac.kind == Kind.MACHO && !mac.installable);

        Detection ce = ModContentDetector.detect(text("tabela.xml", "[ENABLE]\nAuto Assembler script\n"), true);
        check("Cheat Engine", ce.kind == Kind.CHEAT_ENGINE && !ce.installable);

        Detection gg = ModContentDetector.detect(text("script.lua", "function main() gg.killAll() end"), true);
        check("GameGuardian = F10", gg.kind == Kind.LUA_GG && !gg.installable);

        Detection txt = ModContentDetector.detect(text("nota.txt", "oi, isto e so um texto"), true);
        check("texto qualquer nao instala", txt.kind == Kind.TEXT_OTHER && !txt.installable);

        byte[] junk = new byte[64];
        junk[0] = 0x00; junk[1] = 0x11; junk[2] = 0x22;
        Detection bin = ModContentDetector.detect(new Sample("desconhecido", junk, null, false, false), true);
        check("binario desconhecido", bin.kind == Kind.BINARY_UNKNOWN && !bin.installable);

        // Nome de componente interno nunca pode ser sobrescrito.
        Detection reservado = ModContentDetector.detect(elf("u_patch.so", 183), true);
        check("u_patch.so é nome reservado", reservado.kind == Kind.ELF_ARM64 && !reservado.installable);
        check("explica o conflito", reservado.reason.contains("interno"));
    }

    // ELF pela metade ou adulterado nao pode virar <id>.so: o dlopen disso
    // dentro do jogo fecha o processo.
    private static void testElfMalformed() {
        // (1) truncado: o arquivo tem menos bytes do que o PT_LOAD exige.
        Sample trunc = elf("meu.so", 183);
        Sample cortado = new Sample("meu.so", trunc.head, null, false, false, 0x1080L);
        ModContentDetector.Detection d1 = ModContentDetector.detect(cortado, true);
        check("ELF truncado recusado", d1.kind == Kind.ELF_MALFORMED && !d1.installable);
        check("diz que o PT_LOAD nao cabe", d1.reason.contains("PT_LOAD") || d1.reason.contains("fora do arquivo"));
        check("diz que o Manager nao instala pela metade", d1.reason.contains("metade"));

        // (2) e_phoff apontando depois do fim do arquivo.
        Sample fora = elf("meu.so", 183);
        put64(fora.head, 32, 0x900000L);
        ModContentDetector.Detection d2 = ModContentDetector.detect(
                new Sample("meu.so", fora.head, null, false, false, 0x2000L), true);
        check("e_phoff fora do arquivo recusado", d2.kind == Kind.ELF_MALFORMED && !d2.installable);
        check("o motivo cita a tabela de programas", d2.reason.contains("programas"));

        // (3) ET_EXEC: executavel, nao biblioteca.
        Sample exec = elf("meu.so", 183);
        put16(exec.head, 16, 2);
        ModContentDetector.Detection d3 = ModContentDetector.detect(exec, true);
        check("ET_EXEC recusado", d3.kind == Kind.ELF_MALFORMED && !d3.installable);
        check("o motivo explica ET_DYN", d3.reason.contains("ET_DYN"));

        // (4) e_phentsize / e_ehsize errados (cabeçalho adulterado).
        Sample phent = elf("meu.so", 183);
        put16(phent.head, 54, 32);
        check("e_phentsize errado recusado",
                ModContentDetector.detect(phent, true).kind == Kind.ELF_MALFORMED);

        Sample ehsize = elf("meu.so", 183);
        put16(ehsize.head, 52, 40);
        check("e_ehsize errado recusado",
                ModContentDetector.detect(ehsize, true).kind == Kind.ELF_MALFORMED);

        // (5) sem PT_LOAD nenhum: nao ha codigo para o linker mapear.
        Sample semLoad = elf("meu.so", 183);
        put32(semLoad.head, 64, 4);   // PT_NOTE
        check("sem PT_LOAD recusado",
                ModContentDetector.detect(semLoad, true).kind == Kind.ELF_MALFORMED);

        // (6) ELF32/ big-endian nao passam como arm64.
        Sample elf32 = elf("meu.so", 183);
        elf32.head[4] = 1;   // ELFCLASS32
        check("ELF32 recusado", ModContentDetector.detect(elf32, true).kind == Kind.ELF_MALFORMED);
        Sample big = elf("meu.so", 183);
        big.head[5] = 2;     // ELFDATA2MSB
        check("big-endian recusado", ModContentDetector.detect(big, true).kind == Kind.ELF_MALFORMED);

        // (7) o caminho feliz nao pode quebrar: ELF64 arm64 coerente instala.
        ModContentDetector.Detection ok = ModContentDetector.detect(elf("meu.so", 183), true);
        check("ELF64 arm64 coerente instala", ok.kind == Kind.ELF_ARM64 && ok.installable);
    }

    private static void testBaseId() {
        check("nome normal", "meu_mod".equals(ModContentDetector.baseId("meu_mod.so")));
        check("espaco vira hifen", "meu mod".replace(' ', '-').equals(ModContentDetector.baseId("meu mod.so")));
        check("sem extensao", "sem_ext".equals(ModContentDetector.baseId("sem_ext")));
        check("caminho e ignorado", "mod".equals(ModContentDetector.baseId("/tmp/download/mod.so")));
        check("so ponto nao serve", ModContentDetector.baseId(".so") == null);
        check("vazio nao serve", ModContentDetector.baseId("") == null);
        check("nulo nao serve", ModContentDetector.baseId(null) == null);
        check("longo demais nao serve", ModContentDetector.baseId(longName(60) + ".so") == null);
    }

    private static String longName(int n) {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < n; i++) sb.append('a');
        return sb.toString();
    }
}
