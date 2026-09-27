package io.github.rianprei.bepinex.manager.core;

import java.io.File;

/**
 * Guarda do arquivo staged contra órfão no cache. Os holders vivem em
 * campos STATIC das Activities (escopo de PROCESSO): rotação cria uma
 * Activity nova com holder novo se o campo for de instância — o arquivo
 * ficava órfão exatamente como antes. Com static, a instância nova herda.
 *
 * São dois holders porque o pendente de cada tela tem dono diferente:
 * SHARED é da MainActivity (herda no refresh) e DETAIL da GameDetailActivity
 * (retoma o install no onResume). Um holder único faria uma tela instalar
 * o pendente da outra no jogo errado.
 *
 * Retaguarda: SelectedFileStager.sweep apaga todo staging órfão na abertura
 * do Manager — cobre também processo morto pelo sistema, que nenhum holder
 * em memória cobre. set() recusa segundo arquivo (o novo é apagado pelo
 * chamador, nunca sobrescreve o anterior); take() consome e libera.
 */
public final class PendingStagedFile {
    /** Pendente da MainActivity (escopo de processo). */
    public static final PendingStagedFile SHARED = new PendingStagedFile();
    /** Pendente da GameDetailActivity (escopo de processo). */
    public static final PendingStagedFile DETAIL = new PendingStagedFile();

    private File pending;

    /** true = este arquivo ficou pendente; false = já havia outro (o chamador apaga o seu). */
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

    /** Lê sem consumir — para o sweep saber o que preservar. */
    public synchronized File peek() {
        return pending;
    }
}
