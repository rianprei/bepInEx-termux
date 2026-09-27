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
    private static final int ELF_HEADER_BYTES = 20;

    private NativeAbiDetector() {}

    public static String detect(File nativeLibraryDir, List<File> apks) {
        String fromPath = abiInPath(nativeLibraryDir == null ? null : nativeLibraryDir.getPath());
        if (fromPath != null) return fromPath;

        String fromLibraries = abiFromExtractedLibraries(nativeLibraryDir);
        if (fromLibraries != null) return fromLibraries;

        Set<String> apkAbis = new HashSet<>();
        if (apks != null) {
            for (File apk : apks) collectApkAbis(apk, apkAbis);
        }
        return apkAbis.size() == 1 ? apkAbis.iterator().next() : null;
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
        return null;
    }

    public static String abiForKind(ModContentDetector.Kind kind) {
        if (kind == ModContentDetector.Kind.ELF_ARM64) return ARM64;
        if (kind == ModContentDetector.Kind.ELF_ARM32) return ARM32;
        return null;
    }

    public static String incompatibilityMessage(String payloadAbi, String gameAbi) {
        if (payloadAbi == null) return "O arquivo nativo não é uma biblioteca ARM Android válida.";
        if (gameAbi == null) {
            return "Não foi possível descobrir a arquitetura instalada deste jogo. O Manager "
                    + "não vai arriscar instalar uma biblioteca incompatível.";
        }
        return "Este mod é para " + payloadAbi + ", mas o jogo instalado usa " + gameAbi
                + ". Escolha a versão do mod para a arquitetura do jogo.";
    }

    private static String abiInPath(String path) {
        if (path == null) return null;
        String[] segments = path.replace('\\', '/').split("/");
        for (String segment : segments) {
            if (ARM64.equals(segment) || "arm64".equals(segment)) return ARM64;
            if (ARM32.equals(segment) || "arm".equals(segment)) return ARM32;
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

    private static void collectApkAbis(File apk, Set<String> abis) {
        if (apk == null || !apk.isFile()) return;
        try (ZipFile zip = new ZipFile(apk)) {
            Enumeration<? extends ZipEntry> entries = zip.entries();
            while (entries.hasMoreElements()) {
                String name = entries.nextElement().getName();
                if (!name.startsWith("lib/") || !name.endsWith(".so")) continue;
                String[] parts = name.split("/", 4);
                if (parts.length < 3) continue;
                if (ARM64.equals(parts[1])) abis.add(ARM64);
                else if (ARM32.equals(parts[1])) abis.add(ARM32);
            }
        } catch (IOException ignored) {
            // An APK can disappear during refresh; other installed metadata may still decide.
        }
    }
}
