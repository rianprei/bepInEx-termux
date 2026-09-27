package io.github.rianprei.bepinex.manager.core;

import java.io.File;
import java.io.IOException;
import java.util.Collection;
import java.util.Enumeration;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

// Detecta engines pelo conteudo do APK, sem abrir ou alterar o aplicativo.
public final class EngineDetector {
    public static final String ENGINE_UNITY_IL2CPP = "unity-il2cpp";
    public static final String ENGINE_UNITY_MONO = "unity-mono";
    public static final String ENGINE_COCOS2DX = "cocos2dx";
    public static final String ENGINE_UNREAL = "unreal";
    public static final String ENGINE_GODOT = "godot";
    public static final String ENGINE_DEFOLD = "defold";
    public static final String ENGINE_FLUTTER = "flutter";
    public static final String ENGINE_REACT_NATIVE = "react-native";
    public static final String ENGINE_SOLAR2D = "solar2d";
    public static final String ENGINE_LOVE = "love";
    public static final String ENGINE_LIBGDX = "libgdx";
    public static final String ENGINE_XAMARIN = "xamarin";
    public static final String ENGINE_RENPY = "renpy";
    public static final String ENGINE_NATIVE = "native";
    public static final String ENGINE_JAVA = "java";
    public static final String ENGINE_UNKNOWN = "unknown";

    private EngineDetector() {}

    public static String getDisplayName(String engine) {
        if (engine == null || ENGINE_UNKNOWN.equals(engine)) return "Desconhecido";
        switch (engine) {
            case ENGINE_UNITY_IL2CPP: return "Unity (IL2CPP)";
            case ENGINE_UNITY_MONO: return "Unity (Mono)";
            case ENGINE_COCOS2DX: return "Cocos2d-x";
            case ENGINE_UNREAL: return "Unreal Engine";
            case ENGINE_GODOT: return "Godot";
            case ENGINE_DEFOLD: return "Defold";
            case ENGINE_FLUTTER: return "Flutter";
            case ENGINE_REACT_NATIVE: return "React Native";
            case ENGINE_SOLAR2D: return "Solar2D";
            case ENGINE_LOVE: return "LÖVE";
            case ENGINE_LIBGDX: return "libGDX";
            case ENGINE_XAMARIN: return ".NET / Xamarin";
            case ENGINE_RENPY: return "Ren'Py";
            case ENGINE_NATIVE: return "Nativo (C/C++)";
            case ENGINE_JAVA: return "Java / Android";
            default: return engine;
        }
    }

    public static String getModSupport(String engine) {
        if (ENGINE_UNITY_IL2CPP.equals(engine)) {
            return "u_patch funciona apenas em Unity IL2CPP. Mods .so precisam ser compativeis com o aparelho; scripts Frida JS dependem do u_frida.";
        }
        if (ENGINE_UNITY_MONO.equals(engine)) {
            return "u_patch deste projeto nao funciona em Unity Mono. Mods .so compativeis e scripts Frida JS dependem do aparelho e do u_frida.";
        }
        if (ENGINE_JAVA.equals(engine) || ENGINE_UNKNOWN.equals(engine) || engine == null) {
            return "Nao foi identificado um engine suportado. O Manager nao cria suporte automaticamente; .so compativeis e Frida JS dependem do aparelho e do u_frida.";
        }
        if (ENGINE_NATIVE.equals(engine)) {
            return "Ha bibliotecas nativas, mas o engine nao foi identificado. u_patch nao se aplica; .so compativeis e Frida JS dependem do aparelho e do u_frida.";
        }
        return "Engine identificado pelo conteudo, mas nao ha mod especifico para ele neste projeto. Mods .so exigem compatibilidade; scripts Frida JS dependem do aparelho e do u_frida.";
    }

    // Indica apenas os artefatos nativos. Sem os assets, backends Unity nao podem
    // ser distinguidos com a certeza exigida pelo detector de APK.
    public static String detectFromLibNames(Collection<String> libNames) {
        return detectFromContents(libNames, java.util.Collections.emptySet());
    }

    public static String detectFromContents(Collection<String> libNames, Collection<String> assetNames) {
        Set<String> libs = new HashSet<>();
        if (libNames != null) {
            for (String lib : libNames) {
                if (lib != null) libs.add(baseName(lib).toLowerCase(Locale.ROOT));
            }
        }
        Set<String> assets = new HashSet<>();
        if (assetNames != null) {
            for (String asset : assetNames) {
                if (asset != null) assets.add(asset.toLowerCase(Locale.ROOT));
            }
        }
        if (libs.isEmpty() && assets.isEmpty()) return ENGINE_UNKNOWN;

        // Unity precedes other specific engines: Unity games can embed Flutter/RN
        // libraries as plugins. The backend requires both its library and payload.
        if (libs.contains("libil2cpp.so") && hasSuffix(assets, "/global-metadata.dat")) {
            return ENGINE_UNITY_IL2CPP;
        }
        if (hasAnyName(libs, "libmonobdwgc-2.0.so", "libmono.so")
                && hasSuffix(assets, "/assembly-csharp.dll")) {
            return ENGINE_UNITY_MONO;
        }
        if (libs.contains("libflutter.so") && libs.contains("libapp.so")) return ENGINE_FLUTTER;
        if (assets.contains("assets/index.android.bundle")) return ENGINE_REACT_NATIVE;
        if (libs.contains("libdmengine.so")) return ENGINE_DEFOLD;
        if (libs.contains("libcorona.so")) return ENGINE_SOLAR2D;
        if (libs.contains("liblove.so")) return ENGINE_LOVE;
        if (libs.contains("libgdx.so")) return ENGINE_LIBGDX;
        if (libs.contains("libmonodroid.so") || hasAssemblies(assets)) return ENGINE_XAMARIN;
        if (libs.contains("librenpython.so")) return ENGINE_RENPY;
        if (libs.contains("libcocos2djs.so") || libs.contains("libcocos2dcpp.so")) {
            return ENGINE_COCOS2DX;
        }
        if (libs.contains("libue4.so") || libs.contains("libunreal.so")) return ENGINE_UNREAL;
        if (libs.contains("libgodot_android.so") || libs.contains("libgodot.so")) return ENGINE_GODOT;
        if (libs.isEmpty()) {
            for (String asset : assets) {
                if (asset.matches("(?:.*/)?classes[0-9]*\\.dex")) return ENGINE_JAVA;
            }
            return ENGINE_UNKNOWN;
        }
        return ENGINE_NATIVE;
    }

    // Varre APK base/splits e nativeLibraryDir somente pelos nomes de entradas.
    public static String detectFromApks(List<File> apkFiles, File nativeLibDir) {
        Set<String> libs = new HashSet<>();
        Set<String> assets = new HashSet<>();
        if (nativeLibDir != null && nativeLibDir.isDirectory()) {
            File[] files = nativeLibDir.listFiles();
            if (files != null) {
                for (File file : files) {
                    if (file.getName().endsWith(".so")) libs.add(file.getName());
                }
            }
        }
        if (apkFiles != null) {
            for (File apk : apkFiles) {
                if (apk == null || !apk.isFile()) continue;
                try (ZipFile zip = new ZipFile(apk)) {
                    Enumeration<? extends ZipEntry> entries = zip.entries();
                    while (entries.hasMoreElements()) {
                        String name = entries.nextElement().getName();
                        if (name.startsWith("lib/") && name.endsWith(".so")) {
                            libs.add(name);
                        } else if (name.startsWith("assets/")) {
                            assets.add(name);
                        } else if (name.matches("classes[0-9]*\\.dex")) {
                            assets.add(name);
                        }
                    }
                } catch (IOException ignored) {
                    // APKs podem desaparecer durante a leitura; os demais continuam utilizaveis.
                }
            }
        }
        return detectFromContents(libs, assets);
    }

    private static String baseName(String path) {
        int slash = path.lastIndexOf('/');
        return slash >= 0 ? path.substring(slash + 1) : path;
    }

    private static boolean hasSuffix(Set<String> paths, String suffix) {
        for (String path : paths) {
            if (path.endsWith(suffix)) return true;
        }
        return false;
    }

    private static boolean hasAssemblies(Set<String> assets) {
        for (String path : assets) {
            if (path.matches(".*(?:^|/)assemblies/[^/]+\\.dll$")
                    || path.endsWith("/assemblies.blob")) return true;
        }
        return false;
    }

    private static boolean hasAnyName(Set<String> names, String... candidates) {
        for (String candidate : candidates) {
            if (names.contains(candidate)) return true;
        }
        return false;
    }
}
