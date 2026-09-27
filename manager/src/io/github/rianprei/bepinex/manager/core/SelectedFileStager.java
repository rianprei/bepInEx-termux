package io.github.rianprei.bepinex.manager.core;

import java.io.File;
import java.io.IOException;
import java.nio.file.Files;

public final class SelectedFileStager {
    private static final String DIRECTORY_PREFIX = "bepinex-import-";

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
}
