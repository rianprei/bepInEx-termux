package io.github.rianprei.bepinex.manager.test;

import io.github.rianprei.bepinex.manager.core.EngineDetector;

import java.io.File;
import java.io.FileOutputStream;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

public class EngineDetectorTest {
    public static void run() {
        testDetectFromLibNames();
        testDetectFromZip();
        System.out.println("  [OK] EngineDetectorTest (C6)");
    }

    private static void testDetectFromLibNames() {
        if (!EngineDetector.ENGINE_UNITY_IL2CPP.equals(
                EngineDetector.detectFromLibNames(Arrays.asList("libmain.so", "libil2cpp.so", "libunity.so")))) {
            throw new AssertionError("Falha ao detectar unity-il2cpp");
        }

        if (!EngineDetector.ENGINE_UNITY_MONO.equals(
                EngineDetector.detectFromLibNames(Arrays.asList("libmonobdwgc-2.0.so", "libunity.so")))) {
            throw new AssertionError("Falha ao detectar unity-mono");
        }

        if (!EngineDetector.ENGINE_COCOS2DX.equals(
                EngineDetector.detectFromLibNames(Collections.singletonList("libcocos2djs.so")))) {
            throw new AssertionError("Falha ao detectar cocos2dx");
        }

        if (!EngineDetector.ENGINE_UNREAL.equals(
                EngineDetector.detectFromLibNames(Collections.singletonList("libUE4.so")))) {
            throw new AssertionError("Falha ao detectar unreal");
        }

        if (!EngineDetector.ENGINE_GODOT.equals(
                EngineDetector.detectFromLibNames(Collections.singletonList("libgodot_android.so")))) {
            throw new AssertionError("Falha ao detectar godot");
        }

        if (!EngineDetector.ENGINE_NATIVE.equals(
                EngineDetector.detectFromLibNames(Collections.singletonList("libgameengine.so")))) {
            throw new AssertionError("Falha ao detectar native");
        }

        if (!EngineDetector.ENGINE_JAVA.equals(
                EngineDetector.detectFromLibNames(Collections.emptyList()))) {
            throw new AssertionError("Falha ao detectar java para app sem so");
        }
    }

    private static void testDetectFromZip() {
        try {
            File tmpZip = File.createTempFile("test_apk_", ".apk");
            tmpZip.deleteOnExit();

            try (ZipOutputStream zos = new ZipOutputStream(new FileOutputStream(tmpZip))) {
                zos.putNextEntry(new ZipEntry("lib/arm64-v8a/libil2cpp.so"));
                zos.write(new byte[]{0, 1, 2});
                zos.closeEntry();
            }

            String engine = EngineDetector.detectFromApks(Collections.singletonList(tmpZip), null);
            if (!EngineDetector.ENGINE_UNITY_IL2CPP.equals(engine)) {
                throw new AssertionError("Falha na deteccao atraves de ZipFile: " + engine);
            }
        } catch (Exception e) {
            throw new RuntimeException(e);
        }
    }
}
