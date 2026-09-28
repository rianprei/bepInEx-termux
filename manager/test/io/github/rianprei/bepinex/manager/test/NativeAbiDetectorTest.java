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
        check("header non-ARM recusado", NativeAbiDetector.abiFromElfHeader(header(2, 62)) == null);

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
            check("mod ARM32 vs jogo ARM64 recusado",
                    !NativeAbiDetector.ARM32.equals(NativeAbiDetector.ARM64)
                            && NativeAbiDetector.incompatibilityMessage(
                                    NativeAbiDetector.ARM32, NativeAbiDetector.ARM64).contains("armeabi-v7a"));
            check("classe ELF roteia para ABI",
                    NativeAbiDetector.ARM32.equals(
                            NativeAbiDetector.abiForKind(ModContentDetector.Kind.ELF_ARM32)));

            File arm64Gadget = new File(tmp, "frida-gadget.so");
            write(arm64Gadget, header(2, 183));
            LooseModInstaller.Result gadgetMismatch = LooseModInstaller.installFromFile(
                    arm64Gadget, "com.example.game", "unity-il2cpp", NativeAbiDetector.ARM32);
            check("Manager recusa gadget ARM64 para jogo ARM32",
                    !gadgetMismatch.success && gadgetMismatch.message.contains(NativeAbiDetector.ARM64));
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

    private static void write(File file, byte[] bytes) throws Exception {
        try (FileOutputStream output = new FileOutputStream(file)) {
            output.write(bytes);
        }
    }

    private static void writeApk(File apk, String... entries) throws Exception {
        try (ZipOutputStream output = new ZipOutputStream(new FileOutputStream(apk))) {
            for (String entry : entries) {
                output.putNextEntry(new ZipEntry(entry));
                output.write(header(entry.contains("armeabi") ? 1 : 2,
                        entry.contains("armeabi") ? 40 : 183));
                output.closeEntry();
            }
        }
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
