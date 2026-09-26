package io.github.rianprei.bepinex.manager.core;

import java.io.File;
import java.io.IOException;
import java.util.Collection;
import java.util.Enumeration;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

// Detector de engine de jogo sem abrir o processo (Contrato C6).
public final class EngineDetector {
    public static final String ENGINE_UNITY_IL2CPP = "unity-il2cpp";
    public static final String ENGINE_UNITY_MONO   = "unity-mono";
    public static final String ENGINE_COCOS2DX     = "cocos2dx";
    public static final String ENGINE_UNREAL       = "unreal";
    public static final String ENGINE_GODOT        = "godot";
    public static final String ENGINE_NATIVE       = "native";
    public static final String ENGINE_JAVA         = "java";

    private EngineDetector() {}

    public static String getDisplayName(String engine) {
        if (engine == null) return "Desconhecido";
        switch (engine) {
            case ENGINE_UNITY_IL2CPP: return "Unity (IL2CPP)";
            case ENGINE_UNITY_MONO:   return "Unity (Mono)";
            case ENGINE_COCOS2DX:     return "Cocos2d-x";
            case ENGINE_UNREAL:       return "Unreal Engine";
            case ENGINE_GODOT:        return "Godot";
            case ENGINE_NATIVE:       return "Nativo (C/C++)";
            case ENGINE_JAVA:         return "Java / Android";
            default:                  return engine;
        }
    }

    // Detecta o engine a partir dos nomes de bibliotecas .so encontradas.
    public static String detectFromLibNames(Collection<String> libNames) {
        if (libNames == null || libNames.isEmpty()) {
            return ENGINE_JAVA;
        }

        boolean hasNative = false;

        for (String lib : libNames) {
            String name = lib.toLowerCase();
            if (name.contains("/")) {
                name = name.substring(name.lastIndexOf('/') + 1);
            }
            if (!name.endsWith(".so")) continue;

            hasNative = true;

            if ("libil2cpp.so".equals(name)) {
                return ENGINE_UNITY_IL2CPP;
            }
            if (name.startsWith("libmonobdwgc") || name.startsWith("libmono")) {
                return ENGINE_UNITY_MONO;
            }
            if (name.startsWith("libcocos2d") || name.startsWith("libcocos")) {
                return ENGINE_COCOS2DX;
            }
            if (name.startsWith("libue4") || name.startsWith("libunreal")) {
                return ENGINE_UNREAL;
            }
            if (name.startsWith("libgodot")) {
                return ENGINE_GODOT;
            }
        }

        return hasNative ? ENGINE_NATIVE : ENGINE_JAVA;
    }

    // Varre os APKs (base + splits) e a pasta de libs nativas extraidas.
    public static String detectFromApks(List<File> apkFiles, File nativeLibDir) {
        Set<String> libNames = new HashSet<>();

        // 1. Checa a pasta nativeLibraryDir se existir no sistema
        if (nativeLibDir != null && nativeLibDir.isDirectory()) {
            File[] files = nativeLibDir.listFiles();
            if (files != null) {
                for (File f : files) {
                    if (f.getName().endsWith(".so")) {
                        libNames.add(f.getName());
                    }
                }
            }
        }

        // 2. Checa dentro dos arquivos APK (base e splits)
        if (apkFiles != null) {
            for (File apk : apkFiles) {
                if (apk == null || !apk.isFile()) continue;
                try (ZipFile zip = new ZipFile(apk)) {
                    Enumeration<? extends ZipEntry> entries = zip.entries();
                    while (entries.hasMoreElements()) {
                        ZipEntry entry = entries.nextElement();
                        String name = entry.getName();
                        if (name.endsWith(".so") && name.startsWith("lib/")) {
                            libNames.add(name);
                        }
                    }
                } catch (IOException ignored) {}
            }
        }

        return detectFromLibNames(libNames);
    }
}
