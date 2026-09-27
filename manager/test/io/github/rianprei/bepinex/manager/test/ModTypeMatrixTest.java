package io.github.rianprei.bepinex.manager.test;

import io.github.rianprei.bepinex.manager.core.BmodInstaller;
import io.github.rianprei.bepinex.manager.core.LooseModInstaller;
import io.github.rianprei.bepinex.manager.core.ModContentDetector;
import io.github.rianprei.bepinex.manager.core.ModContentDetector.Detection;
import io.github.rianprei.bepinex.manager.core.ModContentDetector.Kind;

import java.io.File;
import java.io.IOException;

/**
 * Matriz de tipos do C7: cada arquivo REAL de test/fixtures/modtypes/ passa
 * pelo probe() do LooseModInstaller (o mesmo caminho do usuario) e tem que
 * receber o Kind esperado, com um trecho OBRIGATORIO da explicacao PT-BR.
 *
 * <p>O corpus e hostil de proposito: extensao mentirosa nos dois sentidos
 * (ELF com nome .png instala porque o CONTEUDO e .so arm64; zip com nome
 * .so e recusado como zip), APK/OBB/XAPK/asset/save sao do jogo e nao de
 * mod, e arquivo vazio nao vira nada por causa de extensao.
 *
 * <p>Este teste e a rede que segura as sabotagens pedidas: (a) detector que
 * so olha extensao, (b) detector que aceita ELF de outra arquitetura e
 * (c) instalador .bmod sem a checagem de zip-slip. Se qualquer um deles
 * voltar atras, a matriz abaixo falha — e o corpus e deterministico, entao
 * a falha e reproduvel byte a byte.
 */
public class ModTypeMatrixTest {
    // Entrada direta (sem o TestRunner inteiro): util para depurar a matriz
    // isoladamente e para provar sabotagens com saida curta.
    public static void main(String[] args) throws Exception {
        run();
        System.exit(0);
    }

    public static void run() throws IOException, InterruptedException {
        File dir = findFixtureDir();
        check("pasta do corpus existe", dir != null && dir.isDirectory());

        int total = 0;
        for (File f : dir.listFiles()) {
            if (f.getName().endsWith(".py") || f.getName().equals("EXPECTED.tsv")
                    || f.getName().endsWith(".md")) {
                continue;
            }
            total++;
        }
        check("corpus tem os arquivos pedidos (>= 25)", total >= 25);

        // --- o CONTEUDO manda sobre a extensao ------------------------------
        matrix(elfArm64(dir), Kind.ELF_ARM64, true, null,
                new String[]{"arm64"});
        matrix(elfArm32(dir), Kind.ELF_OTHER_ARCH, false,
                new String[]{"nao arm64", "ARM (32 bits"},
                new String[]{});
        matrix(elfX86_64(dir), Kind.ELF_OTHER_ARCH, false,
                new String[]{"nao arm64", "x86-64"},
                new String[]{});
        matrix(fridaGadget(dir), Kind.FRIDA_GADGET, false,
                new String[]{"listen", "frida-gadget.bin"},
                new String[]{});
        matrix(dotnetMonoDll(dir), Kind.DOTNET_MONO, false,
                new String[]{"Mono"},
                new String[]{});
        matrix(dotnetIl2cppDll(dir), Kind.DOTNET_IL2CPP, false,
                new String[]{"IL2CPP"},
                new String[]{});
        matrix(peNativeDll(dir), Kind.PE_NATIVE, false,
                new String[]{"Windows"},
                new String[]{});
        matrix(peExe(dir), Kind.PE_NATIVE, false,
                new String[]{"Windows"},
                new String[]{});
        matrix(patchOk(dir), Kind.PATCH, true, null, new String[]{});
        matrix(patchBroken(dir), Kind.TEXT_OTHER, false,
                new String[]{"nao tem regra C4"},
                new String[]{});
        matrix(fridaJs(dir), Kind.FRIDA_JS, true, null, new String[]{});
        matrix(luaGg(dir), Kind.LUA_GG, false,
                new String[]{"GameGuardian"},
                new String[]{});
        matrix(bmodOk(dir), Kind.BMOD, true, null, new String[]{});
        matrix(bepinexPcZip(dir), Kind.ZIP_PLAIN, false,
                new String[]{"Descompacte"},
                new String[]{});

        // --- containers e dados do JOGO: nunca sao mod ----------------------
        matrix(apk(dir), Kind.ZIP_GAME_CONTAINER, false,
                new String[]{"PROPRIO JOGO", "nunca modifica"},
                new String[]{});
        matrix(obb(dir), Kind.ZIP_GAME_CONTAINER, false,
                new String[]{"PROPRIO JOGO"},
                new String[]{});
        matrix(xapk(dir), Kind.ZIP_GAME_CONTAINER, false,
                new String[]{"PROPRIO JOGO"},
                new String[]{});
        matrix(pak(dir), Kind.GAME_DATA, false,
                new String[]{"Unreal"},
                new String[]{});
        matrix(bundle(dir), Kind.GAME_DATA, false,
                new String[]{"UnityFS"},
                new String[]{});
        matrix(saveJson(dir), Kind.SAVE_GAME, false,
                new String[]{"SAVE", "nao um mod"},
                new String[]{});
        matrix(txt(dir), Kind.TEXT_OTHER, false,
                new String[]{"arquivo de texto"},
                new String[]{});

        // --- vazio e 0 byte: extensao nenhuma inventa tipo -------------------
        matrix(emptyFile(dir), Kind.BINARY_UNKNOWN, false,
                new String[]{"VAZIO"},
                new String[]{});
        matrix(zeroSo(dir), Kind.BINARY_UNKNOWN, false,
                new String[]{"VAZIO"},
                new String[]{});

        // --- os dois mentirosos: prova de que conteudo > extensao ------------
        matrix(pngElf(dir), Kind.ELF_ARM64, true, null, new String[]{});
        matrix(soZip(dir), Kind.ZIP_PLAIN, false,
                new String[]{"Descompacte"},
                new String[]{});

        // --- zip-slip: o .bmod com ../ tem que ser barrado -------------------
        zipSlipBmodRecusado(dir);
        determinismo(dir);

        System.out.println("  [OK] ModTypeMatrixTest (" + total + " arquivos, corpus deterministico)");
    }

    // ------------------------------------------------------------------ caso

    private static void matrix(File f, Kind wantKind, boolean wantInstallable,
                               String[] mustHave, String[] mustNotHave) throws IOException {
        String name = (f != null) ? f.getName() : "(fixture ausente)";
        if (f == null || !f.isFile()) {
            throw new AssertionError("fixture ausente no corpus: " + name
                    + " — gere com test/fixtures/modtypes/generate.py");
        }
        ModContentDetector.Sample sample = LooseModInstaller.probe(f);
        Detection d = ModContentDetector.detect(sample, true);
        String reason = (d.reason != null) ? d.reason : "";

        check(name + ": Kind esperado " + wantKind + ", veio " + d.kind
                        + " (motivo: " + reason + ")",
                d.kind == wantKind);
        check(name + ": installable esperado " + wantInstallable,
                d.installable == wantInstallable);
        for (String frag : (mustHave != null) ? mustHave : new String[0]) {
            check(name + ": a explicacao diz '" + frag + "' (veio: " + reason + ")",
                    reason.contains(frag));
        }
        for (String frag : (mustNotHave != null) ? mustNotHave : new String[0]) {
            check(name + ": a explicacao NAO diz '" + frag + "'", !reason.contains(frag));
        }
        // A explicacao e em PT-BR de verdade: sem resto de ingles corrido.
        for (String sotaque : new String[]{"the ", " and ", " is ", " not supported"}) {
            check(name + ": explicacao com sotaque '" + sotaque + "': " + reason,
                    !reason.contains(sotaque));
        }
    }

    private static void zipSlipBmodRecusado(File dir) throws IOException {
        File f = new File(dir, "zip_slip.bmod");
        check("fixture zip_slip.bmod existe", f.isFile());

        // 1. A inspect() barra SEM tocar em root (falha limpa e rapida se
        // alguem remover a guarda de nomes perigosos).
        try {
            BmodInstaller.inspect(f);
            throw new AssertionError("zip_slip.bmod: inspect aceitou entrada '../evil.so' "
                    + "(checagem de zip-slip removida?)");
        } catch (IOException e) {
            check("zip_slip.bmod: a recusa acusa zip-slip",
                    e.getMessage().contains("zip-slip"));
        }

        // 2. E a instalacao inteira recusa, dizendo que nada foi instalado.
        BmodInstaller.InstallResult r = BmodInstaller.install(f, "com.foo.teste", "unity-il2cpp");
        check("zip_slip.bmod: instalacao recusada: " + r.message, !r.success);
        check("zip_slip.bmod: a recusa diz que nada foi instalado",
                r.message.contains("nada foi instalado"));
    }

    // Rodar o gerador de novo tem que deixar o corpus identico byte a byte;
    // corpus que muda sozinho nao e fixture, e sera reprodutivel nunca.
    private static void determinismo(File dir) throws IOException, InterruptedException {
        Process p = new ProcessBuilder("python3", "generate.py")
                .directory(dir)
                .redirectErrorStream(true)
                .start();
        p.getInputStream().readAllBytes();
        p.waitFor();
        check("generate.py roda limpo", p.exitValue() == 0);
    }

    // --------------------------------------------------------------- fixtures

    private static File findFixtureDir() {
        File dir = new File(System.getProperty("user.dir", ".")).getAbsoluteFile();
        for (File d = dir; d != null; d = d.getParentFile()) {
            File f = new File(d, "test/fixtures/modtypes");
            if (f.isDirectory()) return f;
        }
        return null;
    }

    private static File f(File dir, String name) {
        return (dir != null) ? new File(dir, name) : null;
    }

    private static File elfArm64(File d) { return f(d, "mod_arm64.so"); }

    private static File elfArm32(File d) { return f(d, "mod_arm32.so"); }

    private static File elfX86_64(File d) { return f(d, "mod_x86_64.so"); }

    private static File fridaGadget(File d) { return f(d, "frida-gadget-raw.so"); }

    private static File dotnetMonoDll(File d) { return f(d, "mod_pcinho.dll"); }

    private static File dotnetIl2cppDll(File d) { return f(d, "mod_il2cpp.dll"); }

    private static File peNativeDll(File d) { return f(d, "dll_nativo.dll"); }

    private static File peExe(File d) { return f(d, "hackeador.exe"); }

    private static File patchOk(File d) { return f(d, "regras_boas.patch"); }

    private static File patchBroken(File d) { return f(d, "regras_quebradas.patch"); }

    private static File fridaJs(File d) { return f(d, "script_frida.js"); }

    private static File luaGg(File d) { return f(d, "script_gg.lua"); }

    private static File bmodOk(File d) { return f(d, "pacote_ok.bmod"); }

    private static File bepinexPcZip(File d) { return f(d, "bepinex_pc.zip"); }

    private static File apk(File d) { return f(d, "jogo.apk"); }

    private static File obb(File d) { return f(d, "expansao.obb"); }

    private static File xapk(File d) { return f(d, "pacote.xapk"); }

    private static File pak(File d) { return f(d, "dados.pak"); }

    private static File bundle(File d) { return f(d, "asset.bundle"); }

    private static File saveJson(File d) { return f(d, "save_do_jogo.json"); }

    private static File txt(File d) { return f(d, "leia_me.txt"); }

    private static File emptyFile(File d) { return f(d, "vazio_sem_extensao"); }

    private static File zeroSo(File d) { return f(d, "so_zero.so"); }

    private static File pngElf(File d) { return f(d, "imagem_falsa.png"); }

    private static File soZip(File d) { return f(d, "compactado_mentiroso.so"); }

    private static void check(String what, boolean cond) {
        if (!cond) throw new AssertionError("falhou: " + what);
    }
}
