package io.github.rianprei.bepinex.manager.core;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

/** Writes UTF-8 to an app-private temp file and always removes it after the callback. */
public final class TemporaryTextFile {
    @FunctionalInterface
    public interface Consumer {
        boolean use(String tempPath) throws IOException;
    }

    private TemporaryTextFile() {}

    public static boolean write(File directory, String content, Consumer consumer) throws IOException {
        File temp = null;
        try {
            temp = File.createTempFile("bep_su_write_", ".tmp", directory);
            try (FileOutputStream output = new FileOutputStream(temp)) {
                output.write(content.getBytes(StandardCharsets.UTF_8));
            }
            return consumer.use(temp.getAbsolutePath());
        } finally {
            if (temp != null) Files.deleteIfExists(temp.toPath());
        }
    }
}
