package io.github.rianprei.bepinex.manager.test;

import io.github.rianprei.bepinex.manager.core.UiLiveness;

import java.io.File;

public final class UiLivenessTest {
    private UiLivenessTest() {}

    public static void run() {
        check("tela viva recebe dialog",
                UiLiveness.alive(new Fake(false, false)));
        check("isFinishing recusa (usuário fechou)",
                !UiLiveness.alive(new Fake(true, false)));
        // O caso que o código antigo deixava passar: Activity destruída por
        // rotação — isFinishing() é false, mas o token da janela morreu.
        check("isDestroyed recusa (rotação, tema, idioma)",
                !UiLiveness.alive(new Fake(false, true)));
        check("ambos recusa",
                !UiLiveness.alive(new Fake(true, true)));
        check("referência nula recusa",
                !UiLiveness.alive(null));
        System.out.println("  [OK] UiLivenessTest (callback pós-rotação não abre dialog)");
    }

    private static void check(String what, boolean cond) {
        TestRunner.check(what, cond);
    }

    private record Fake(boolean finishing, boolean destroyed) implements UiLiveness.ActivityLike {
        @Override public boolean isFinishing() { return finishing; }
        @Override public boolean isDestroyed() { return destroyed; }
    }
}
