package io.github.rianprei.bepinex.manager.core;

/**
 * Decisão pura de ciclo de vida: o callback assíncrono pode tocar na tela?
 * isFinishing() sozinho é false quando a Activity morre por rotação ou outra
 * mudança de configuração — e o dialog aberto numa Activity destruída dá
 * BadTokenException. Por isso o isDestroyed() entra junto (API 17+; o
 * minSdk do app é 26). Interface mínima para o teste de host provar as
 * combinações sem Android.
 */
public final class UiLiveness {
    private UiLiveness() {}

    /** O subconjunto de Activity que a decisão precisa. */
    public interface ActivityLike {
        boolean isFinishing();
        boolean isDestroyed();
    }

    /** true = a tela ainda pode receber dialog/toast. */
    public static boolean alive(ActivityLike activity) {
        return activity != null && !activity.isFinishing() && !activity.isDestroyed();
    }
}
