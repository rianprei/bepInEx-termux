package io.github.rianprei.bepinex.manager.test;

import io.github.rianprei.bepinex.manager.core.CrashGuardState;
import io.github.rianprei.bepinex.manager.core.CrashGuardState.State;

public class CrashGuardStateTest {
    public static void run() {
        testPaths();
        testParseCounter();
        testBlocksWindow();
        testDescribe();
        System.out.println("  [OK] CrashGuardStateTest (F1d/G1)");
    }

    private static void check(String what, boolean cond) {
        TestRunner.check(what, cond);
    }

    private static void testPaths() {
        // O loader grava no state dir do JOGO (o jogo nao escreve na arvore de
        // mods); mods/<pkg>/ e conferido por compatibilidade — e essa arvore
        // foi de /data/local/tmp para /data/adb/bepinex (raiz root-only, revisao
        // de seguranca do freebuff).
        check("contador no state dir",
                CrashGuardState.counterPath("com.foo").equals("/data/data/com.foo/files/bepinex/crashguard"));
        check("marcador no state dir",
                CrashGuardState.markerPath("com.foo").equals("/data/data/com.foo/files/bepinex/disabled_by_crashguard"));
        check("marcador alternativo em mods/",
                CrashGuardState.modsMarkerPath("com.foo")
                        .equals("/data/adb/bepinex/mods/com.foo/disabled_by_crashguard"));
    }

    private static void testParseCounter() {
        State ok = CrashGuardState.parse("2 1758000000\n", true);
        check("contador valido", ok.counterValid && ok.count == 2 && ok.ts == 1758000000L);
        check("marcador vem do filesystem", ok.marker);

        State semMarcador = CrashGuardState.parse("1 1758000000", false);
        check("sem marcador", !semMarcador.marker && semMarcador.counterValid);

        // Ausente/corrompido = sem informacao, e sem informacao nao bloqueia
        // (o loader tambem e fail-safe assim).
        for (String ruim : new String[]{null, "", "   ", "abc", "2", "2 x", "-1 1758000000", "0 0"}) {
            State s = CrashGuardState.parse(ruim, false);
            check("contador ruim nao bloqueia: '" + ruim + "'", !s.counterValid && !s.blocks(1758000000L));
        }
    }

    private static void testBlocksWindow() {
        long t = 1758000000L;
        check("0 mortes nao bloqueia", !CrashGuardState.parse("0 " + t, false).blocks(t + 1));
        check("1 morte nao bloqueia", !CrashGuardState.parse("1 " + t, false).blocks(t + 1));
        check("2 mortes dentro da janela bloqueia", CrashGuardState.parse("2 " + t, false).blocks(t + 19));
        check("2 mortes exatamente na janela nao bloqueia",
                !CrashGuardState.parse("2 " + t, false).blocks(t + CrashGuardState.WINDOW_S));
        check("2 mortes fora da janela nao bloqueia",
                !CrashGuardState.parse("2 " + t, false).blocks(t + 600));
        check("3 mortes dentro da janela bloqueia", CrashGuardState.parse("3 " + t, false).blocks(t + 5));
        check("limite e janela batem com o loader",
                CrashGuardState.LIMIT == 2 && CrashGuardState.WINDOW_S == 20);
    }

    private static void testDescribe() {
        long t = 1758000000L;
        State bloqueado = CrashGuardState.parse("2 " + t, true);
        String txtBloqueado = CrashGuardState.describe(bloqueado, t + 5);
        check("diz que segurou agora", txtBloqueado.contains("segurou os mods"));
        check("diz quantas vezes", txtBloqueado.contains("2x"));
        check("nao promete diagnosticar o mod", txtBloqueado.contains("nao diagnostica o mod"));
        check("manda desativar mod por mod", txtBloqueado.contains("um por um"));

        State expirado = CrashGuardState.parse("2 " + t, true);
        String txtExpirado = CrashGuardState.describe(expirado, t + 600);
        check("diz que so pulou a ultima vez", txtExpirado.contains("ultima vez"));
        check("explica que a janela expirou", txtExpirado.contains("20 segundos"));

        // O aviso so existe com marcador: sem ele, o texto nem faz sentido.
        check("sem marcador nao ha o que descrever",
                CrashGuardState.parse("2 " + t, false).marker == false);
    }
}
