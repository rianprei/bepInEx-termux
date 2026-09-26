package io.github.rianprei.bepinex.manager.core;

import java.util.Locale;

// Leitura honesta do crashguard (F1d, garantia G1).
//
// O que o loader faz (jni/main.cpp::crashguard_gate na base):
//   - antes de qualquer dlopen, le /data/data/<pkg>/files/bepinex/crashguard
//     no formato "<contador> <unix_ts>";
//   - se contador >= 2 E (agora - ts) < 20s: NAO carrega nenhum mod deste
//     jogo, escreve o marcador disabled_by_crashguard e avisa no log;
//   - senao: incrementa o contador e, 20s depois (jogo sobreviveu a
//     janela), zera o contador em uma thread.
//
// Detalhe que importa para o botao "Reativar": o bloqueio dura so a
// janela de 20s — a proxima tentativa depois dela passa. O arquivo de
// marcador, esse sim, fica no device ate alguem apagar. Por isso o botao
// apaga o marcador E zera o contador: sem isso o aviso voltaria a
// aparecer mesmo com o jogo ja funcionando.
//
// O marcador nasce em /data/data/<pkg>/files/bepinex/ (o jogo nao escreve
// em /data/local/tmp). mods/<pkg>/disabled_by_crashguard tambem e
// verificado, para pegar marcador escrito por outra versao do loader.
public final class CrashGuardState {

    public static final int LIMIT = 2;      // bc_crashguard.h::BC_CRASHGUARD_LIMIT
    public static final int WINDOW_S = 20;  // bc_crashguard.h::BC_CRASHGUARD_WINDOW_S

    public static final class State {
        public int count = 0;
        public long ts = 0;
        public boolean counterValid = false;   // arquivo existia e estava legivel
        public boolean marker = false;         // disabled_by_crashguard existe

        public boolean blocks(long nowSeconds) {
            if (!counterValid) return false;   // fail-safe do loader tambem
            return count >= LIMIT && (nowSeconds - ts) < WINDOW_S;
        }
    }

    private CrashGuardState() {}

    public static String stateDir(String pkg) {
        return String.format(Locale.ROOT, "/data/data/%s/files/bepinex", pkg);
    }

    public static String counterPath(String pkg) {
        return stateDir(pkg) + "/crashguard";
    }

    public static String markerPath(String pkg) {
        return stateDir(pkg) + "/disabled_by_crashguard";
    }

    public static String modsMarkerPath(String pkg) {
        return String.format(Locale.ROOT, "/data/local/tmp/mods/%s/disabled_by_crashguard", pkg);
    }

    // "<contador> <ts>" -> State. Ausente, vazio ou corrompido = contador
    // invalido (igual ao loader: nao bloqueia por falta de informacao).
    public static State parse(String counterText, boolean markerPresent) {
        State s = new State();
        s.marker = markerPresent;
        if (counterText == null) return s;
        String[] parts = counterText.trim().split("\\s+");
        if (parts.length < 2) return s;
        try {
            long c = Long.parseLong(parts[0]);
            long t = Long.parseLong(parts[1]);
            if (c >= 0 && t > 0) {
                s.count = (int) c;
                s.ts = t;
                s.counterValid = true;
            }
        } catch (NumberFormatException ignored) {}
        return s;
    }

    // Texto do aviso. So aparece quando o marcador existe; o botao_exists
    // limpa marcador e contador.
    public static String describe(State s, long nowSeconds) {
        StringBuilder sb = new StringBuilder();
        if (s.blocks(nowSeconds)) {
            sb.append("O crashguard segurou os mods DESTE JOGO agora: ele fechou ")
              .append(s.count).append("x seguidas dentro de ").append(WINDOW_S)
              .append(" segundos logo depois de carregar os mods, e nao vale a pena insistir.\n\n");
        } else {
            sb.append("O crashguard pulou o carregamento de mods deste jogo na ultima vez: ele "
                      + "fechou ").append(s.count)
              .append("x seguidas logo depois de carregar os mods. Como o bloqueio dura so ")
              .append(WINDOW_S).append(" segundos, a proxima vez que voce abrir o jogo ja tenta de novo.\n\n");
        }
        sb.append("Se o jogo continuar fechando, o problema e um mod especifico: desative os mods "
                + "um por um (o interruptor da lista abaixo) e veja qual deles derruba o jogo. O "
                + "crashguard nao sabe qual mod foi — ele protege o jogo, nao diagnostica o mod.");
        return sb.toString();
    }
}
