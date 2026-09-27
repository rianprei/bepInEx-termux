package io.github.rianprei.bepinex.manager.core;

import java.io.File;

/**
 * Guarda do arquivo staged contra órfão no cache: o ponteiro vive fora da
 * Activity, então rotação/troca de tema não perde o caminho — a instância
 * nova reaproveita (take + volta a setar no mesmo runnable do main thread,
 * sem janela para o onDestroy interleavar); se ninguém consumir, o dono
 * apaga no onDestroy. Só um arquivo por vez: quem chega com o holder
 * ocupado recebe false e apaga o seu, em vez de vazar o anterior.
 */
public final class PendingStagedFile {
    private File pending;

    /** true = este arquivo ficou pendente; false = já havia outro (chame o dono apaga o seu). */
    public synchronized boolean set(File file) {
        if (pending != null) return false;
        pending = file;
        return true;
    }

    /** Tira o arquivo da pendência (quem instalou, apagou ou herdou). */
    public synchronized File take() {
        File file = pending;
        pending = null;
        return file;
    }
}
