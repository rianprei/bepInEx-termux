package io.github.rianprei.bepinex.manager.test;

import io.github.rianprei.bepinex.manager.core.ModContentDetector;
import io.github.rianprei.bepinex.manager.core.SelectedFileRouter;
import io.github.rianprei.bepinex.manager.model.ModManifest;

import java.nio.charset.StandardCharsets;
import java.util.Collections;

public final class SelectedFileRouterTest {
    public static void run() {
        testSoUsesLooseInstallerAndAsksForGame();
        testUniversalBmodAsksForGame();
        testApkUsesDetectorRejection();
        testBepInExPcUsesDetectorRejection();
        System.out.println("  [OK] SelectedFileRouterTest (imports por conteúdo)");
    }

    private static void check(String what, boolean condition) {
        if (!condition) throw new AssertionError("falhou: " + what);
    }

    private static void testSoUsesLooseInstallerAndAsksForGame() {
        ModContentDetector.Detection detection = ModContentDetector.detect(elf("hello.so"), true);
        SelectedFileRouter.Decision route = SelectedFileRouter.decide(detection, null);
        check(".so detectado como ELF arm64", route.kind == ModContentDetector.Kind.ELF_ARM64);
        check(".so pede seleção de jogo", route.action == SelectedFileRouter.Action.SELECT_GAME);
        check(".so usa o LooseModInstaller e nunca o BmodInstaller",
                route.installer == SelectedFileRouter.Installer.LOOSE_MOD_INSTALLER);
        check(".so não roteia para BmodInstaller",
                route.installer != SelectedFileRouter.Installer.BMOD_INSTALLER);
    }

    private static void testUniversalBmodAsksForGame() {
        ModContentDetector.Detection detection = ModContentDetector.detect(c2BmodSample(), true);
        ModManifest manifest = new ModManifest();
        manifest.game = "*";
        SelectedFileRouter.Decision route = SelectedFileRouter.decide(detection, manifest);
        check(".bmod game:* pede seleção de jogo",
                route.action == SelectedFileRouter.Action.SELECT_GAME);
        check(".bmod mantém a rota compartilhada por conteúdo",
                route.installer == SelectedFileRouter.Installer.LOOSE_MOD_INSTALLER);
    }

    private static void testApkUsesDetectorRejection() {
        byte[] zip = {'P', 'K', 3, 4, 0, 0, 0, 0};
        ModContentDetector.Detection detection = ModContentDetector.detect(
                new ModContentDetector.Sample("app.apk", zip, null, false, false), true);
        SelectedFileRouter.Decision route = SelectedFileRouter.decide(detection, null);
        check(".apk recusado", route.action == SelectedFileRouter.Action.REJECT);
        check("a recusa exibe a explicação original do detector",
                detection.reason.equals(route.message) && !route.message.isEmpty());
        check("arquivo recusado não escolhe instalador",
                route.installer == SelectedFileRouter.Installer.NONE);
    }

    private static void testBepInExPcUsesDetectorRejection() {
        byte[] zip = {'P', 'K', 3, 4, 0, 0, 0, 0};
        ModContentDetector.Sample sample = new ModContentDetector.Sample(
                "renamed.zip", zip, null, false, false, zip.length, null,
                Collections.singletonList("BepInEx/plugins/example.dll"));
        ModContentDetector.Detection detection = ModContentDetector.detect(sample, true);
        SelectedFileRouter.Decision route = SelectedFileRouter.decide(detection, null);
        check("zip com layout BepInEx é reconhecido pelo conteúdo",
                detection.kind == ModContentDetector.Kind.BEPINEX_PC);
        check("mod BepInEx de PC recusado", route.action == SelectedFileRouter.Action.REJECT);
        check("VIEW mantém o texto verdadeiro do detector",
                detection.reason.equals(route.message)
                        && route.message.contains("não funciona no celular"));
        check("tipo não instalável não escolhe instalador",
                route.installer == SelectedFileRouter.Installer.NONE);
    }

    private static ModContentDetector.Sample c2BmodSample() {
        byte[] zip = {'P', 'K', 3, 4, 0, 0, 0, 0};
        byte[] manifest = "{\"format\":1,\"id\":\"mod-test\"}".getBytes(StandardCharsets.UTF_8);
        return new ModContentDetector.Sample("mod.bmod", zip, null, true, false,
                zip.length, manifest, Collections.singletonList("manifest.json"));
    }

    private static ModContentDetector.Sample elf(String name) {
        byte[] header = new byte[4096];
        header[0] = 0x7f;
        header[1] = 'E';
        header[2] = 'L';
        header[3] = 'F';
        header[4] = 2;
        header[5] = 1;
        header[6] = 1;
        put16(header, 16, 3);
        put16(header, 18, 183);
        put32(header, 20, 1);
        put64(header, 32, 64);
        put16(header, 52, 64);
        put16(header, 54, 56);
        put16(header, 56, 1);
        put32(header, 64, 1);
        put32(header, 68, 5);
        put64(header, 72, 0x1000);
        put64(header, 80, 0x1000);
        put64(header, 88, 0x1000);
        put64(header, 96, 256);
        put64(header, 104, 256);
        put64(header, 112, 0x1000);
        return new ModContentDetector.Sample(name, header, null, false, false, 0x2000);
    }

    private static void put16(byte[] data, int offset, int value) {
        data[offset] = (byte) value;
        data[offset + 1] = (byte) (value >> 8);
    }

    private static void put32(byte[] data, int offset, int value) {
        for (int i = 0; i < 4; i++) data[offset + i] = (byte) (value >> (8 * i));
    }

    private static void put64(byte[] data, int offset, long value) {
        for (int i = 0; i < 8; i++) data[offset + i] = (byte) (value >> (8 * i));
    }
}
