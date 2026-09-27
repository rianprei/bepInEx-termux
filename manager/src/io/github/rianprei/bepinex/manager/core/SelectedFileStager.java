package io.github.rianprei.bepinex.manager.core;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;

public final class SelectedFileStager {
    private static final String DIRECTORY_PREFIX = "bepinex-import-";

    /** Fonte do conteúdo (Uri do provedor, aberto pela Activity). */
    public interface InputStreamProvider {
        InputStream open() throws IOException;
    }

    /** Cria o staged e copia o stream; apaga o staged se a cópia falhar. */
    public static File copyIntoStaging(File cacheDirectory, String originalName,
                                       InputStreamProvider provider) throws IOException {
        File staged = create(cacheDirectory, originalName);
        try (InputStream input = provider.open()) {
            if (input == null) throw new IOException("O provedor não abriu o arquivo.");
            try (FileOutputStream output = new FileOutputStream(staged)) {
                byte[] buffer = new byte[16 * 1024];
                int count;
                while ((count = input.read(buffer)) != -1) output.write(buffer, 0, count);
            }
            return staged;
        } catch (Exception e) {
            delete(staged);
            throw e;
        }
    }

    private SelectedFileStager() {}

    public static File create(File cacheDirectory, String originalName) throws IOException {
        if (cacheDirectory == null || !cacheDirectory.isDirectory()) {
            throw new IOException("Diretório de cache indisponível.");
        }
        String name = originalName == null ? "" : originalName.replace('\\', '/');
        int separator = name.lastIndexOf('/');
        if (separator >= 0) name = name.substring(separator + 1);
        name = name.replaceAll("[^A-Za-z0-9._-]", "_");
        if (name.isEmpty() || ".".equals(name) || "..".equals(name)) name = "mod.bin";
        if (name.length() > 100) name = name.substring(name.length() - 100);

        File directory = Files.createTempDirectory(cacheDirectory.toPath(), DIRECTORY_PREFIX).toFile();
        File staged = new File(directory, name);
        if (!staged.createNewFile()) {
            directory.delete();
            throw new IOException("Não foi possível criar o arquivo temporário.");
        }
        return staged;
    }

    public static void delete(File staged) {
        if (staged == null) return;
        File directory = staged.getParentFile();
        staged.delete();
        if (directory != null && directory.isDirectory()
                && directory.getName().startsWith(DIRECTORY_PREFIX)) {
            directory.delete();
        }
    }

    /**
     * Retaguarda contra órfão: apaga TODO staging (bepinex-import-*) do
     * cache exceto os diretórios dos arquivos pendentes informados. Roda na
     * abertura do Manager e cobre o caso que nenhum holder em memória cobre:
     * processo morto pelo sistema com staged abandonado no cache.
     * Devolve quantos diretórios órfãos apagou.
     */
    public static int sweep(File cacheDirectory, File... keepFiles) {
        if (cacheDirectory == null || !cacheDirectory.isDirectory()) return 0;
        File[] entries = cacheDirectory.listFiles();
        if (entries == null) return 0;
        int removed = 0;
        for (File entry : entries) {
            if (!entry.isDirectory() || !entry.getName().startsWith(DIRECTORY_PREFIX)) continue;
            if (isKeepDirectory(entry, keepFiles)) continue;
            File[] inner = entry.listFiles();
            if (inner != null) {
                for (File file : inner) file.delete();
            }
            if (entry.delete()) removed++;
        }
        return removed;
    }

    private static boolean isKeepDirectory(File directory, File... keepFiles) {
        if (keepFiles == null) return false;
        for (File keep : keepFiles) {
            if (keep == null) continue;
            File parent = keep.getParentFile();
            if (parent != null && parent.equals(directory)) return true;
        }
        return false;
    }
}
