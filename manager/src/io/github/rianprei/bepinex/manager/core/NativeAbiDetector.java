package io.github.rianprei.bepinex.manager.core;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.util.Enumeration;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

/** Detects the ABI installed for a game, preferring its extracted native-library directory. */
public final class NativeAbiDetector {
    public static final String ARM64 = "arm64-v8a";
    public static final String ARM32 = "armeabi-v7a";
    public static final String NO_NATIVE_CODE = "no-native-code";
    private static final int ELF_HEADER_BYTES = 20;

    private NativeAbiDetector() {}

    public static String detect(File nativeLibraryDir, List<File> apks) {
        String fromPath = abiInPath(nativeLibraryDir == null ? null : nativeLibraryDir.getPath());
        if (fromPath != null) return fromPath;

        String fromLibraries = abiFromExtractedLibraries(nativeLibraryDir);
        if (fromLibraries != null) return fromLibraries;

        ApkAbiScan scan = collectApkAbis(apks);
        // Prefer ARM64 in mixed APKs; use ARM32 only when ARM64 is absent.
        // ARM64+ARM32 without a foreign ABI stays ambiguous and falls through.
        if (scan.abis.contains(ARM64) && hasForeignAbi(scan.abis)) return ARM64;
        if (!scan.abis.contains(ARM64) && scan.abis.contains(ARM32)
                && hasForeignAbi(scan.abis)) return ARM32;
        if (scan.abis.size() == 1) return scan.abis.iterator().next();
        if (scan.complete && !scan.hasNativeLibraries) return NO_NATIVE_CODE;
        return null;
    }

    public static String abiFromElfHeader(byte[] header) {
        if (header == null || header.length < ELF_HEADER_BYTES
                || header[0] != 0x7f || header[1] != 'E' || header[2] != 'L' || header[3] != 'F'
                || header[5] != 1) {
            return null;
        }
        int machine = (header[18] & 0xff) | ((header[19] & 0xff) << 8);
        if (header[4] == 2 && machine == 183) return ARM64;
        if (header[4] == 1 && machine == 40) return ARM32;
        if (header[4] == 1 && machine == 3) return "x86";
        if (header[4] == 2 && machine == 62) return "x86_64";
        if ((header[4] == 1 || header[4] == 2) && machine == 8) {
            return header[4] == 2 ? "mips64" : "mips";
        }
        if (header[4] == 2 && machine == 243) return "riscv64";
        return null;
    }

    public static String abiForKind(ModContentDetector.Kind kind) {
        if (kind == ModContentDetector.Kind.ELF_ARM64) return ARM64;
        if (kind == ModContentDetector.Kind.ELF_ARM32) return ARM32;
        return null;
    }

    public static String incompatibilityMessage(String payloadAbi, String gameAbi) {
        if (payloadAbi == null) {
            return "ABI inválida: o arquivo não é uma biblioteca ARM do Android. Escolha um mod "
                    + ".so compilado para arm64-v8a ou armeabi-v7a.";
        }
        if (NO_NATIVE_CODE.equals(gameAbi)) {
            return "Este jogo não inclui bibliotecas nativas (.so), então o Manager não consegue "
                    + "validar uma ABI ARM. Use uma versão do jogo que inclua bibliotecas nativas "
                    + "arm64-v8a ou armeabi-v7a.";
        }
        if (gameAbi == null) {
            return "ABI do jogo desconhecida: o Manager não consegue confirmar se ele usa "
                    + "arm64-v8a ou armeabi-v7a. Confirme a ABI do jogo e use um mod correspondente; "
                    + "não instale por tentativa.";
        }
        if (!ARM64.equals(gameAbi) && !ARM32.equals(gameAbi)) {
            int machine = machineForAbi(gameAbi);
            String architecture = machine >= 0
                    ? ModContentDetector.archName(machine) : gameAbi;
            return "ABI incompatível: este jogo é " + architecture + " (" + gameAbi
                    + "), mas o Manager só carrega mods ARM (arm64-v8a ou armeabi-v7a). "
                    + "Use uma versão ARM do jogo, se disponível.";
        }
        return "ABI incompatível: o jogo usa " + gameAbi + ", mas o mod usa " + payloadAbi
                + ". Baixe a versão " + gameAbi + " do mod.";
    }

    private static int machineForAbi(String abi) {
        switch (abi) {
            case "x86": return 3;
            case "x86_64": return 62;
            case "mips":
            case "mips64": return 8;
            case "riscv64": return 243;
            default: return -1;
        }
    }

    private static boolean hasForeignAbi(Set<String> abis) {
        for (String abi : abis) {
            if (!ARM64.equals(abi) && !ARM32.equals(abi)) return true;
        }
        return false;
    }

    private static boolean isKnownAbi(String abi) {
        return ARM64.equals(abi) || ARM32.equals(abi) || machineForAbi(abi) >= 0;
    }

    private static String abiInPath(String path) {
        if (path == null) return null;
        String[] segments = path.replace('\\', '/').split("/");
        for (String segment : segments) {
            if (ARM64.equals(segment) || "arm64".equals(segment)) return ARM64;
            if (ARM32.equals(segment) || "arm".equals(segment)) return ARM32;
            if (machineForAbi(segment) >= 0) return segment;
        }
        return null;
    }

    private static String abiFromExtractedLibraries(File nativeLibraryDir) {
        if (nativeLibraryDir == null || !nativeLibraryDir.isDirectory()) return null;
        File[] libraries = nativeLibraryDir.listFiles((dir, name) -> name.endsWith(".so"));
        if (libraries == null || libraries.length == 0) return null;
        Set<String> abis = new HashSet<>();
        for (File library : libraries) {
            byte[] header = readHeader(library);
            String abi = abiFromElfHeader(header);
            if (abi != null) abis.add(abi);
        }
        return abis.size() == 1 ? abis.iterator().next() : null;
    }

    private static byte[] readHeader(File library) {
        byte[] header = new byte[ELF_HEADER_BYTES];
        try (FileInputStream input = new FileInputStream(library)) {
            int offset = 0;
            while (offset < header.length) {
                int count = input.read(header, offset, header.length - offset);
                if (count < 0) return null;
                offset += count;
            }
            return header;
        } catch (IOException e) {
            return null;
        }
    }

    private static ApkAbiScan collectApkAbis(List<File> apks) {
        ApkAbiScan scan = new ApkAbiScan();
        scan.complete = apks != null && !apks.isEmpty();
        if (apks == null) return scan;
        for (File apk : apks) {
            if (apk == null || !apk.isFile()) {
                scan.complete = false;
                continue;
            }
            try (ZipFile zip = new ZipFile(apk)) {
                Enumeration<? extends ZipEntry> entries = zip.entries();
                while (entries.hasMoreElements()) {
                    String name = entries.nextElement().getName();
                    if (!name.startsWith("lib/") || !name.endsWith(".so")) continue;
                    scan.hasNativeLibraries = true;
                    String[] parts = name.split("/", 4);
                    if (parts.length < 3) continue;
                    if (isKnownAbi(parts[1])) scan.abis.add(parts[1]);
                }
            } catch (IOException ignored) {
                scan.complete = false;
                // An APK can disappear during refresh; other installed metadata may still decide.
            }
        }
        return scan;
    }

    private static final class ApkAbiScan {
        final Set<String> abis = new HashSet<>();
        boolean complete;
        boolean hasNativeLibraries;
    }
}
