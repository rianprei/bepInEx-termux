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
        testConfirmedEngineMarkers();
        testPrecedenceAndExactNames();
        testUnknownAndJavaFallback();
        testDetectFromZip();
        System.out.println("  [OK] EngineDetectorTest (C6)");
    }

    private static void testConfirmedEngineMarkers() {
        // Unity Android player artifacts: libil2cpp + metadata, or Mono runtime + managed assembly.
        assertEngine(EngineDetector.ENGINE_UNITY_IL2CPP,
                libs("libil2cpp.so"), assets("assets/bin/Data/Managed/Metadata/global-metadata.dat"));
        assertEngine(EngineDetector.ENGINE_UNITY_MONO,
                libs("libmonobdwgc-2.0.so"), assets("assets/bin/Data/Managed/Assembly-CSharp.dll"));

        // Existing supported engine markers retained from the previous detector.
        assertEngine(EngineDetector.ENGINE_COCOS2DX, libs("libcocos2djs.so"), assets());
        assertEngine(EngineDetector.ENGINE_UNREAL, libs("libUE4.so"), assets());
        assertEngine(EngineDetector.ENGINE_GODOT, libs("libgodot_android.so"), assets());

        // Defold official debugging guide uses libdmengine.so in an Android APK library path.
        // https://github.com/defold/defold/blob/dev/README_DEBUGGING.md#L71
        assertEngine(EngineDetector.ENGINE_DEFOLD, libs("libdmengine.so"), assets());

        // Flutter official AOT docs/devicelab APK check: both libraries live under lib/<abi>.
        // https://github.com/flutter/flutter/blob/8db55268667c738b90677d49857ff42938e9c9fa/docs/engine/Flutter-engine-operation-in-AOT-Mode.md#L59
        // https://github.com/flutter/flutter/blob/8db55268667c738b90677d49857ff42938e9c9fa/dev/devicelab/bin/tasks/build_android_host_app_with_module_aar.dart#L368-L372
        assertEngine(EngineDetector.ENGINE_FLUTTER, libs("libflutter.so", "libapp.so"), assets());

        // React Native Gradle plugin's documented default APK bundle asset name.
        // https://github.com/facebook/react-native/blob/main/packages/gradle-plugin/react-native-gradle-plugin/src/main/kotlin/com/facebook/react/ReactExtension.kt#L74-L77
        assertEngine(EngineDetector.ENGINE_REACT_NATIVE, libs(), assets("assets/index.android.bundle"));

        // Solar2D's official Android build copies libcorona.so into its native library output.
        // https://github.com/coronalabs/corona/blob/master/platform/android/sdk/build.xml#L157-L161
        assertEngine(EngineDetector.ENGINE_SOLAR2D, libs("libcorona.so"), assets());

        // Official Android port build documentation references the liblove.so dependency.
        // https://github.com/love2d/love-android/blob/4c65fff4f8b38693aca5d91bc06f254f86a97adf/README.md#L16
        assertEngine(EngineDetector.ENGINE_LOVE, libs("liblove.so"), assets());

        // Official libGDX Android natives artifact contains this library (gdx-platform).
        // https://repo1.maven.org/maven2/com/badlogicgames/gdx/gdx-platform/1.13.1/gdx-platform-1.13.1-natives-arm64-v8a.jar
        assertEngine(EngineDetector.ENGINE_LIBGDX, libs("libgdx.so"), assets());

        // .NET for Android documents both the runtime library and managed assembly packaging.
        // https://github.com/dotnet/android/blob/main/Documentation/project-docs/ApkSharedLibraries.md#L35-L62
        assertEngine(EngineDetector.ENGINE_XAMARIN, libs("libmonodroid.so"), assets());
        assertEngine(EngineDetector.ENGINE_XAMARIN, libs(), assets("assets/assemblies/Game.dll"));
        assertEngine(EngineDetector.ENGINE_XAMARIN, libs(), assets("assets/assemblies.blob"));

        // Ren'Py's official Android build task links librenpython.so.
        // https://github.com/renpy/renpy-build/blob/16129650549f6ccef43d9eab9292701691844ca6/tasks/renpython.py#L27-L53
        assertEngine(EngineDetector.ENGINE_RENPY, libs("librenpython.so"), assets());
    }

    private static void testPrecedenceAndExactNames() {
        // A Unity player takes precedence over an embedded Flutter plugin/runtime.
        assertEngine(EngineDetector.ENGINE_UNITY_IL2CPP,
                libs("libil2cpp.so", "libflutter.so", "libapp.so"),
                assets("assets/bin/Data/Managed/Metadata/global-metadata.dat"));

        // The same precedence applies when an app also embeds a React Native bundle.
        assertEngine(EngineDetector.ENGINE_UNITY_MONO,
                libs("libmonobdwgc-2.0.so"),
                assets("assets/bin/Data/Managed/Assembly-CSharp.dll", "assets/index.android.bundle"));

        // Engine library matching is exact; a similarly named third-party library is not GameMaker.
        assertEngine(EngineDetector.ENGINE_NATIVE, libs("libyoyo_fake.so"), assets());
        assertEngine(EngineDetector.ENGINE_NATIVE, libs("libdmengine_fake.so"), assets());

        // Single Unity backend markers are insufficient to distinguish a player confidently.
        assertEngine(EngineDetector.ENGINE_NATIVE, libs("libil2cpp.so"), assets());
        assertEngine(EngineDetector.ENGINE_NATIVE, libs("libmonobdwgc-2.0.so"), assets());
    }

    private static void testUnknownAndJavaFallback() {
        assertEngine(EngineDetector.ENGINE_UNKNOWN, libs(), assets());
        assertEngine(EngineDetector.ENGINE_UNKNOWN, null, null);
        assertEngine(EngineDetector.ENGINE_JAVA, libs(), assets("classes.dex"));
        if (!"Desconhecido".equals(EngineDetector.getDisplayName(EngineDetector.ENGINE_UNKNOWN))) {
            throw new AssertionError("Engine desconhecido deve ter nome honesto");
        }
        if (EngineDetector.getModSupport(EngineDetector.ENGINE_UNITY_IL2CPP).contains("Unity Mono")) {
            throw new AssertionError("Texto de suporte IL2CPP contradiz o engine detectado");
        }
    }

    private static void testDetectFromZip() {
        try {
            File apk = File.createTempFile("engine_detector_", ".apk");
            apk.deleteOnExit();
            try (ZipOutputStream zip = new ZipOutputStream(new FileOutputStream(apk))) {
                addEntry(zip, "lib/arm64-v8a/libil2cpp.so");
                addEntry(zip, "assets/bin/Data/Managed/Metadata/global-metadata.dat");
            }
            String engine = EngineDetector.detectFromApks(Collections.singletonList(apk), null);
            if (!EngineDetector.ENGINE_UNITY_IL2CPP.equals(engine)) {
                throw new AssertionError("Falha lendo libs e assets do APK: " + engine);
            }
        } catch (Exception e) {
            throw new RuntimeException(e);
        }
    }

    private static void addEntry(ZipOutputStream zip, String name) throws Exception {
        zip.putNextEntry(new ZipEntry(name));
        zip.write(0);
        zip.closeEntry();
    }

    private static List<String> libs(String... values) {
        return Arrays.asList(values);
    }

    private static List<String> assets(String... values) {
        return Arrays.asList(values);
    }

    private static void assertEngine(String expected, List<String> libs, List<String> assets) {
        String actual = EngineDetector.detectFromContents(libs, assets);
        if (!expected.equals(actual)) {
            throw new AssertionError("Esperado " + expected + ", recebido " + actual
                    + " para libs=" + libs + ", assets=" + assets);
        }
        String support = EngineDetector.getModSupport(actual);
        if (support == null || support.trim().isEmpty()) {
            throw new AssertionError("Texto de suporte ausente para " + actual);
        }
    }
}
