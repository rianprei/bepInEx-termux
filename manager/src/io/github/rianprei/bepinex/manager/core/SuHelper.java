package io.github.rianprei.bepinex.manager.core;

import java.io.BufferedReader;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

// Helper unico para execucao de comandos root via 'su -c' com erros amigaveis (Contrato C1).
public final class SuHelper {
    public static final class Result {
        public final int exitCode;
        public final String stdout;
        public final String stderr;
        public final boolean success;
        public final String friendlyError;

        public Result(int exitCode, String stdout, String stderr, String friendlyError) {
            this.exitCode = exitCode;
            this.stdout = stdout != null ? stdout.trim() : "";
            this.stderr = stderr != null ? stderr.trim() : "";
            this.friendlyError = friendlyError;
            this.success = (exitCode == 0 && friendlyError == null);
        }
    }

    private static Boolean sRootAvailableCache = null;

    private SuHelper() {}

    public static Result exec(String cmd) {
        if (cmd == null || cmd.trim().isEmpty()) {
            return new Result(-1, "", "", "Comando vazio");
        }

        Process process = null;
        try {
            process = Runtime.getRuntime().exec(new String[]{"su", "-c", cmd});

            final Process proc = process;
            final ByteArrayOutputStream outStream = new ByteArrayOutputStream();
            final ByteArrayOutputStream errStream = new ByteArrayOutputStream();

            Thread tOut = new Thread(() -> copyStream(proc.getInputStream(), outStream));
            Thread tErr = new Thread(() -> copyStream(proc.getErrorStream(), errStream));
            tOut.start();
            tErr.start();

            int exitCode = process.waitFor();
            tOut.join(3000);
            tErr.join(3000);

            String stdout = new String(outStream.toByteArray(), StandardCharsets.UTF_8);
            String stderr = new String(errStream.toByteArray(), StandardCharsets.UTF_8);

            String friendlyError = null;
            if (exitCode != 0) {
                if (stderr.contains("Permission denied") || stderr.contains("not allowed")) {
                    friendlyError = "Acesso root negado pelo Magisk/KernelSU. Permita o bepInEx Manager no seu gerenciador de root.";
                } else {
                    friendlyError = "Erro ao executar comando root (codigo " + exitCode + "): " + (stderr.isEmpty() ? stdout : stderr);
                }
            }

            return new Result(exitCode, stdout, stderr, friendlyError);
        } catch (IOException e) {
            return new Result(-1, "", e.getMessage(),
                    "Binario 'su' nao encontrado ou inacessivel. O bepInEx Manager requer Magisk ou KernelSU com root ativo.");
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            return new Result(-1, "", "Interrompido", "Comando interrompido antes de terminar.");
        } finally {
            if (process != null) {
                process.destroy();
            }
        }
    }

    public static boolean isRootAvailable() {
        if (sRootAvailableCache != null && sRootAvailableCache) {
            return true;
        }
        Result r = exec("id");
        boolean ok = r.success && (r.stdout.contains("uid=0") || r.stdout.contains("root"));
        if (ok) {
            sRootAvailableCache = true;
        }
        return ok;
    }

    public static final String SELINUX_MOD_CONTEXT = "u:object_r:bepinex_mod_file:s0";

    public static boolean ensureModDir(String pkg) {
        String dir = "/data/local/tmp/mods/" + pkg;
        Result r = exec("mkdir -p '" + dir + "' && chmod 755 '" + dir + "' && chcon " + SELINUX_MOD_CONTEXT + " '" + dir + "'");
        return r.success;
    }

    public static List<String> listFiles(String dirPath) {
        List<String> list = new ArrayList<>();
        Result r = exec("ls -1 '" + dirPath + "' 2>/dev/null");
        if (r.success && !r.stdout.isEmpty()) {
            String[] lines = r.stdout.split("\\r?\\n");
            for (String l : lines) {
                String trimmed = l.trim();
                if (!trimmed.isEmpty()) list.add(trimmed);
            }
        }
        return list;
    }

    public static String readTextFile(String filePath) {
        Result r = exec("cat '" + filePath + "' 2>/dev/null");
        return r.success ? r.stdout : null;
    }

    public static boolean writeTextFile(String filePath, String content) {
        try {
            File tmp = File.createTempFile("bep_su_write_", ".tmp");
            try (FileOutputStream fos = new FileOutputStream(tmp)) {
                fos.write(content.getBytes(StandardCharsets.UTF_8));
            }
            Result r = exec("cp '" + tmp.getAbsolutePath() + "' '" + filePath + "' && chmod 644 '" + filePath + "' && chcon " + SELINUX_MOD_CONTEXT + " '" + filePath + "'");
            tmp.delete();
            return r.success;
        } catch (IOException e) {
            return false;
        }
    }

    public static boolean copyFile(String srcPath, String destPath, String chmodMode) {
        String mode = (chmodMode != null) ? chmodMode : "644";
        Result r = exec("cp -f '" + srcPath + "' '" + destPath + "' && chmod " + mode + " '" + destPath + "' && chcon " + SELINUX_MOD_CONTEXT + " '" + destPath + "'");
        return r.success;
    }

    // Helper de instalacao de arquivos (adendo F1c SELinux: cp + chmod 644 + chcon)
    public static boolean installFile(String srcPath, String destPath, String chmodMode) {
        return copyFile(srcPath, destPath, chmodMode);
    }

    public static boolean installFile(String srcPath, String destPath) {
        return copyFile(srcPath, destPath, "644");
    }

    public static boolean deleteFile(String filePath) {
        Result r = exec("rm -f '" + filePath + "'");
        return r.success;
    }

    public static boolean deleteMod(String pkg, String modId) {
        String base = "/data/local/tmp/mods/" + pkg + "/" + modId;
        Result r = exec("rm -f '" + base + ".'*");
        return r.success;
    }

    public static boolean toggleMod(String pkg, String filename, boolean enable) {
        String dir = "/data/local/tmp/mods/" + pkg + "/";
        String currentPath = dir + filename;
        String newPath;
        if (enable) {
            if (filename.endsWith(".off")) {
                newPath = dir + filename.substring(0, filename.length() - 4);
            } else {
                return true;
            }
        } else {
            if (!filename.endsWith(".off")) {
                newPath = dir + filename + ".off";
            } else {
                return true;
            }
        }
        Result r = exec("mv '" + currentPath + "' '" + newPath + "' && chcon " + SELINUX_MOD_CONTEXT + " '" + newPath + "'");
        return r.success;
    }

    public static boolean restartGame(String pkg) {
        // Encerra forcadamente e inicia o aplicativo principal
        String cmd = "am force-stop '" + pkg + "' && " +
                "(monkey -p '" + pkg + "' -c android.intent.category.LAUNCHER 1 2>/dev/null || " +
                "am start -n $(cmd package resolve-activity --brief '" + pkg + "' | tail -n 1) 2>/dev/null)";
        Result r = exec(cmd);
        return r.exitCode == 0;
    }

    public static String readLog(String pkg) {
        return readTextFile("/data/data/" + pkg + "/files/bepinex/log.txt");
    }

    public static boolean clearLog(String pkg) {
        String path = "/data/data/" + pkg + "/files/bepinex/log.txt";
        Result r = exec(": > '" + path + "'");
        return r.success;
    }

    public static String readDump(String pkg) {
        return readTextFile("/data/data/" + pkg + "/files/bepinex/dump.tsv");
    }

    public static boolean deleteDump(String pkg) {
        return deleteFile("/data/data/" + pkg + "/files/bepinex/dump.tsv");
    }

    private static void copyStream(InputStream in, OutputStream out) {
        byte[] buf = new byte[4096];
        int n;
        try {
            while ((n = in.read(buf)) != -1) {
                out.write(buf, 0, n);
            }
        } catch (IOException ignored) {}
    }
}
