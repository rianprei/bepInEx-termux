package io.github.rianprei.bepinex.manager.test;

import io.github.rianprei.bepinex.manager.core.ModContentDetector;
import io.github.rianprei.bepinex.manager.core.NativeAbiDetector;
import io.github.rianprei.bepinex.manager.core.LooseModInstaller;

import java.io.File;
import java.io.FileOutputStream;
import java.util.Collections;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

public final class NativeAbiDetectorTest {
    private NativeAbiDetectorTest() {}

    public static void run() throws Exception {
        check("nativeLibraryDir arm32 path", NativeAbiDetector.ARM32.equals(
                NativeAbiDetector.detect(new File("/data/app/pkg/lib/arm"), null)));
        check("nativeLibraryDir arm64 path", NativeAbiDetector.ARM64.equals(
                NativeAbiDetector.detect(new File("/data/app/pkg/lib/arm64"), null)));
        check("ELF32 ARM header", NativeAbiDetector.ARM32.equals(
                NativeAbiDetector.abiFromElfHeader(header(1, 40))));
        check("ELF64 AArch64 header", NativeAbiDetector.ARM64.equals(
                NativeAbiDetector.abiFromElfHeader(header(2, 183))));
        byte[] nonElfArm64Header = nonElfArm64Header();
        check("magic ELF ausente recusa header ELF64/AArch64",
                NativeAbiDetector.abiFromElfHeader(nonElfArm64Header) == null);
        check("ELF64 x86_64 identificado",
                "x86_64".equals(NativeAbiDetector.abiFromElfHeader(header(2, 62))));
        check("header ELF desconhecido recusado",
                NativeAbiDetector.abiFromElfHeader(header(2, 999)) == null);

        File tmp = new File(System.getProperty("java.io.tmpdir"),
                "native-abi-test-" + System.nanoTime());
        if (!tmp.mkdir()) throw new AssertionError("não criou pasta de teste");
        try {
            File nativeDir = new File(tmp, "native");
            if (!nativeDir.mkdir()) throw new AssertionError("não criou pasta nativa");
            write(new File(nativeDir, "libgame.so"), header(1, 40));
            check("ABI do ELF extraído", NativeAbiDetector.ARM32.equals(
                    NativeAbiDetector.detect(nativeDir, null)));

            File singleAbiApk = new File(tmp, "single.apk");
            writeApk(singleAbiApk, "lib/armeabi-v7a/libgame.so");
            check("ABI único no APK", NativeAbiDetector.ARM32.equals(
                    NativeAbiDetector.detect(null, Collections.singletonList(singleAbiApk))));

            File multiAbiApk = new File(tmp, "multi.apk");
            writeApk(multiAbiApk, "lib/arm64-v8a/libgame.so", "lib/armeabi-v7a/libgame.so");
            check("APK universal sem ABI instalado fica desconhecido",
                    NativeAbiDetector.detect(null, Collections.singletonList(multiAbiApk)) == null);

            File x86_64Apk = new File(tmp, "x86_64.apk");
            writeApk(x86_64Apk, "lib/x86_64/libgame.so");
            String x86_64Abi = NativeAbiDetector.detect(
                    null, Collections.singletonList(x86_64Apk));
            check("APK x86_64 detecta arquitetura estrangeira", "x86_64".equals(x86_64Abi));
            String x86_64Message = NativeAbiDetector.incompatibilityMessage(
                    NativeAbiDetector.ARM64, x86_64Abi);
            check("erro x86_64 explica arquitetura e limite ARM",
                    x86_64Message.contains("x86-64")
                            && x86_64Message.contains("Manager só carrega mods ARM"));

            File x86Apk = new File(tmp, "x86.apk");
            writeApk(x86Apk, "lib/x86/libgame.so");
            String x86Abi = NativeAbiDetector.detect(null, Collections.singletonList(x86Apk));
            check("APK x86 detecta arquitetura estrangeira", "x86".equals(x86Abi));
            String x86Message = NativeAbiDetector.incompatibilityMessage(
                    NativeAbiDetector.ARM32, x86Abi);
            check("erro x86 explica arquitetura e limite ARM",
                    x86Message.contains("x86 de 32 bits")
                            && x86Message.contains("Manager só carrega mods ARM"));

            File mixedArmApk = new File(tmp, "mixed-arm.apk");
            writeApk(mixedArmApk, "lib/x86/libgame.so", "lib/arm64-v8a/libgame.so");
            check("APK x86 mais ARM64 prefere ARM64",
                    NativeAbiDetector.ARM64.equals(
                            NativeAbiDetector.detect(null, Collections.singletonList(mixedArmApk))));

            File mixedArm32Apk = new File(tmp, "mixed-arm32.apk");
            writeApk(mixedArm32Apk, "lib/x86/libgame.so", "lib/armeabi-v7a/libgame.so");
            check("APK x86 mais ARM32 sem ARM64 prefere ARM32",
                    NativeAbiDetector.ARM32.equals(
                            NativeAbiDetector.detect(null, Collections.singletonList(mixedArm32Apk))));

            File mipsApk = new File(tmp, "mips.apk");
            writeApk(mipsApk, "lib/mips/libgame.so");
            check("APK mips detecta arquitetura estrangeira",
                    "mips".equals(NativeAbiDetector.detect(
                            null, Collections.singletonList(mipsApk))));
            File riscvApk = new File(tmp, "riscv.apk");
            writeApk(riscvApk, "lib/riscv64/libgame.so");
            check("APK riscv64 detecta arquitetura estrangeira",
                    "riscv64".equals(NativeAbiDetector.detect(
                            null, Collections.singletonList(riscvApk))));

            File noNativeApk = new File(tmp, "no-native.apk");
            writeApk(noNativeApk);
            String noNativeAbi = NativeAbiDetector.detect(
                    null, Collections.singletonList(noNativeApk));
            check("APK sem lib .so identificado como jogo sem código nativo",
                    NativeAbiDetector.NO_NATIVE_CODE.equals(noNativeAbi));
            check("mensagem de jogo sem código nativo é específica e acionável",
                    NativeAbiDetector.incompatibilityMessage(
                            NativeAbiDetector.ARM64, noNativeAbi)
                            .contains("Este jogo não inclui bibliotecas nativas (.so)")
                            && NativeAbiDetector.incompatibilityMessage(
                                    NativeAbiDetector.ARM64, noNativeAbi)
                                    .contains("Use uma versão do jogo que inclua bibliotecas nativas"));

            check("erro ABI ARM32 em jogo ARM64 explica causa e download correto",
                    ("ABI incompatível: o jogo usa arm64-v8a, mas o mod usa armeabi-v7a. "
                            + "Baixe a versão arm64-v8a do mod.")
                            .equals(NativeAbiDetector.incompatibilityMessage(
                                    NativeAbiDetector.ARM32, NativeAbiDetector.ARM64)));
            check("erro ABI ARM64 em jogo ARM32 explica causa e download correto",
                    ("ABI incompatível: o jogo usa armeabi-v7a, mas o mod usa arm64-v8a. "
                            + "Baixe a versão armeabi-v7a do mod.")
                            .equals(NativeAbiDetector.incompatibilityMessage(
                                    NativeAbiDetector.ARM64, NativeAbiDetector.ARM32)));
            check("ABI de jogo desconhecida pede confirmação e recusa tentativa",
                    NativeAbiDetector.incompatibilityMessage(NativeAbiDetector.ARM32, null)
                            .contains("Confirme a ABI do jogo e use um mod correspondente; "
                                    + "não instale por tentativa."));
            check("arquivo nativo inválido indica as ABIs aceitas",
                    NativeAbiDetector.incompatibilityMessage(null, NativeAbiDetector.ARM64)
                            .contains("Escolha um mod .so compilado para arm64-v8a ou armeabi-v7a."));
            check("classe ELF roteia para ABI",
                    NativeAbiDetector.ARM32.equals(
                            NativeAbiDetector.abiForKind(ModContentDetector.Kind.ELF_ARM32)));

            File arm64Gadget = new File(tmp, "frida-gadget.so");
            write(arm64Gadget, header(2, 183));
            LooseModInstaller.Result gadgetMismatch = LooseModInstaller.installFromFile(
                    arm64Gadget, "com.example.game", "unity-il2cpp", NativeAbiDetector.ARM32);
            check("Manager mostra ação ABI também para incompatibilidade do gadget",
                    !gadgetMismatch.success && gadgetMismatch.message.equals(
                            "ABI incompatível: o jogo usa armeabi-v7a, mas o mod usa arm64-v8a. "
                                    + "Baixe a versão armeabi-v7a do mod."));

            String invalidGadgetAbi = NativeAbiDetector.abiFromElfHeader(nonElfArm64Header);
            String invalidGadgetError = LooseModInstaller.gadgetAbiError(
                    invalidGadgetAbi, null);
            check("gadget sem magic ELF é recusado mesmo com ABI do jogo desconhecida",
                    invalidGadgetError != null
                            && invalidGadgetError.contains("não é uma biblioteca ELF válida"));
        } finally {
            deleteTree(tmp);
        }
        System.out.println("  [OK] NativeAbiDetectorTest");
    }

    private static byte[] header(int elfClass, int machine) {
        byte[] bytes = new byte[20];
        bytes[0] = 0x7f;
        bytes[1] = 'E';
        bytes[2] = 'L';
        bytes[3] = 'F';
        bytes[4] = (byte) elfClass;
        bytes[5] = 1;
        bytes[18] = (byte) machine;
        bytes[19] = (byte) (machine >>> 8);
        return bytes;
    }

    private static byte[] nonElfArm64Header() {
        byte[] bytes = new byte[20];
        bytes[4] = 2;
        bytes[5] = 1;
        bytes[18] = (byte) 183;
        return bytes;
    }

    private static void write(File file, byte[] bytes) throws Exception {
        try (FileOutputStream output = new FileOutputStream(file)) {
            output.write(bytes);
        }
    }

    private static void writeApk(File apk, String... entries) throws Exception {
        try (ZipOutputStream output = new ZipOutputStream(new FileOutputStream(apk))) {
            for (String entry : entries) {
                output.putNextEntry(new ZipEntry(entry));
                output.write(abiHeader(entry));
                output.closeEntry();
            }
        }
    }

    private static byte[] abiHeader(String entry) {
        if (entry.contains("armeabi")) return header(1, 40);
        if (entry.contains("x86/")) return header(1, 3);
        if (entry.contains("mips/")) return header(1, 8);
        if (entry.contains("riscv64/")) return header(2, 243);
        if (entry.contains("x86_64/")) return header(2, 62);
        return header(2, 183);
    }

    private static void deleteTree(File file) {
        File[] children = file.listFiles();
        if (children != null) {
            for (File child : children) deleteTree(child);
        }
        if (!file.delete()) throw new AssertionError("não removeu " + file);
    }

    private static void check(String what, boolean condition) {
        if (!condition) throw new AssertionError("falhou: " + what);
    }
}
