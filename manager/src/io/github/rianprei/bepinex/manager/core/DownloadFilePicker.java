package io.github.rianprei.bepinex.manager.core;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Base64;
import java.util.Collections;
import java.util.List;

public final class DownloadFilePicker {
    public static final String DOWNLOAD_DIR = "/sdcard/Download";
    public static final String DOCUMENTS_DIR = "/sdcard/Documents";

    private DownloadFilePicker() {}

    public interface RootCall {
        String exec(String command);
    }

    public static final class Listing {
        public final List<String> paths;
        public final String error;

        Listing(List<String> paths, String error) {
            this.paths = paths;
            this.error = error;
        }

        public boolean success() {
            return error == null;
        }
    }

    public static Listing list(RootCall root) {
        if (root == null) throw new IllegalArgumentException("root call ausente");
        try {
            return new Listing(parseListing(root.exec(listCommand())), null);
        } catch (RuntimeException e) {
            return new Listing(Collections.emptyList(),
                    e.getMessage() != null ? e.getMessage() : "Falha ao listar Download/Documents.");
        }
    }

    public static List<String> parseListing(String output) {
        return parseListing(output, DOWNLOAD_DIR, DOCUMENTS_DIR);
    }

    public static List<String> parseListing(String output, String downloadDir, String documentsDir) {
        if (output == null || output.isEmpty()) return Collections.emptyList();
        List<String> paths = new ArrayList<>();
        for (String line : output.split("\\r?\\n")) {
            if (line.isEmpty()) continue;
            try {
                String path = new String(Base64.getDecoder().decode(line), StandardCharsets.UTF_8);
                if (isSharedFile(path, downloadDir, documentsDir)) paths.add(path);
            } catch (IllegalArgumentException ignored) {
                // Invalid records are ignored; the root command emits base64 only.
            }
        }
        return paths;
    }

    public static String listCommand() {
        return listCommand(DOWNLOAD_DIR, DOCUMENTS_DIR);
    }

    public static String listCommand(String downloadDir, String documentsDir) {
        StringBuilder cmd = new StringBuilder("for d in ")
                .append(shellQuote(downloadDir)).append(' ')
                .append(shellQuote(documentsDir)).append("; do\n")
                .append("  [ -d \"$d\" ] || continue\n")
                .append("  for f in \"$d\"/* \"$d\"/.[!.]* \"$d\"/..?*; do\n")
                .append("    [ -f \"$f\" ] && [ ! -L \"$f\" ] || continue\n")
                .append("    printf '%s' \"$f\" | base64 | tr -d '\\n'\n")
                .append("    printf '\\n'\n")
                .append("  done\n")
                .append("done");
        return cmd.toString();
    }

    public static String copyToCacheCommand(String sourcePath, String destinationPath) {
        return copyToCacheCommand(sourcePath, destinationPath, DOWNLOAD_DIR, DOCUMENTS_DIR);
    }

    public static String copyToCacheCommand(String sourcePath, String destinationPath,
                                             String downloadDir, String documentsDir) {
        if (!isSharedFile(sourcePath, downloadDir, documentsDir)) {
            throw new IllegalArgumentException("arquivo fora de Download/Documents");
        }
        SuHelper.requirePath(destinationPath, "cache destination");
        String source = shellQuote(sourcePath);
        String destination = shellQuote(destinationPath);
        return "[ -f " + source + " ] && [ ! -L " + source + " ] && [ -f " + destination
                + " ] && [ ! -L " + destination + " ] && cat " + source
                + " > " + destination + " && chmod 600 " + destination;
    }

    public static boolean isSharedFile(String path) {
        return isSharedFile(path, DOWNLOAD_DIR, DOCUMENTS_DIR);
    }

    public static boolean isSharedFile(String path, String downloadDir, String documentsDir) {
        if (path == null || path.isEmpty()) return false;
        File file = new File(path);
        String name = file.getName();
        if (name.isEmpty() || ".".equals(name) || "..".equals(name)) return false;
        String parent = file.getParent();
        return isDirectChild(parent, name, downloadDir) || isDirectChild(parent, name, documentsDir);
    }

    private static boolean isDirectChild(String parent, String name, String root) {
        return root.equals(parent) && name.indexOf('/') < 0;
    }

    private static String shellQuote(String value) {
        if (value == null || value.isEmpty()) throw new IllegalArgumentException("caminho vazio");
        return "'" + value.replace("'", "'\\''") + "'";
    }
}
