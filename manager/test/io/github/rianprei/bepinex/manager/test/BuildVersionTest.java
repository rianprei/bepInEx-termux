package io.github.rianprei.bepinex.manager.test;

import io.github.rianprei.bepinex.manager.core.BuildVersion;
import io.github.rianprei.bepinex.manager.core.StatusChecker;

public class BuildVersionTest {
    public static void run() {
        testVersionVemDoVersionDaRaiz();
        System.out.println("  [OK] BuildVersionTest (VERSION da raiz)");
    }

    private static void check(String what, boolean cond) {
        if (!cond) throw new AssertionError("falhou: " + what);
    }

    // O ponto deste teste e um: nenhuma versao pode ficar solta no codigo.
    // BuildVersion.java e gerado do VERSION da raiz por run_tests.sh/build.sh,
    // entao se alguem reintroduzir "0.1.0" no manifest ou no aapt2, a versao
    // do APK para de bater com a do projeto — e este teste ve a versao que o
    // codigo realmente usa.
    private static void testVersionVemDoVersionDaRaiz() {
        check("versionName sem 'v' inicial", BuildVersion.NAME.matches("\\d+(\\.\\d+)+"));
        check("versionCode positivo", BuildVersion.CODE > 0);

        String shown = new StatusChecker.SystemStatus().appVersion;
        check("appVersion mostra nome e codigo",
                shown.contains(BuildVersion.NAME) && shown.contains(String.valueOf(BuildVersion.CODE)));
        check("appVersion nao e mais um 1.0.0 fixo", !shown.startsWith("1.0.0"));
    }
}
