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
        System.out.println("  [OK] ModContentDetectorTest (C7)");
    }

    private static void check(String what, boolean cond) {
        if (!cond) throw new AssertionError("falhou: " + what);
    }

    // --- amostras sintéticas -------------------------------------------------

    private static Sample zip(boolean withManifest) {
        byte[] head = {'P', 'K', 3, 4, 0, 0, 0, 0};
        return new Sample("mod.bmod", head, null, withManifest, false);
    }

    /** ELF com e_machine no offset 18. */
    private static Sample elf(String name, int machine) {
        byte[] h = new byte[64];
        h[0] = 0x7F; h[1] = 'E'; h[2] = 'L'; h[3] = 'F';
        h[4] = 2; h[5] = 1; h[6] = 1;   // ELF64, little endian
        h[18] = (byte) (machine & 0xFF);
        h[19] = (byte) ((machine >> 8) & 0xFF);
        return new Sample(name, h, null, false, false);
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
