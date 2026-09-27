package io.github.rianprei.bepinex.manager.core;

/**
 * Guarda de toque duplo para ação com efeito que não deve repetir
 * (instalar o mesmo arquivo duas vezes). Pura, sem Android: a tela cria
 * a flag por trabalho e o teste de host prova o estado sozinho.
 *
 * Escopo: uma flag por diálogo/trabalho, vivendo na Activity. Se a
 * Activity morre e recria (rotação), a flag nova permite repetir — o
 * arquivo staged da instância antiga é tratado pelo cleanup de ciclo de
 * vida, e o install repetido é cp -f determinístico, não corruptor.
 */
public final class InFlightFlag {
    private boolean inFlight;

    /** Tenta iniciar: true = este chamador começa; false = outro já está em voo. */
    public synchronized boolean begin() {
        if (inFlight) return false;
        inFlight = true;
        return true;
    }

    /** Marca o fim do trabalho (no callback final, inclusive em erro). */
    public synchronized void end() {
        inFlight = false;
    }

    public synchronized boolean isInFlight() {
        return inFlight;
    }
}
