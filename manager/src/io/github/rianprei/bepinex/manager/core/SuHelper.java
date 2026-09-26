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
import java.util.regex.Pattern;

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

    // --- validacao central (tudo que vai para o shell root passa aqui) ------
    //
    // Nenhum dado que venha de fora (pacote do Intent, nome de arquivo de um
    // .bmod hostil, caminho devolvido por um `ls`) entra no comando sem
    // passar por estas funcoes. As tres regras sao ancoradas em ^...$ e o
    // conjunto de caracteres nao inclui espaco, aspas, $, ;, |, &, `, redirecionamento
    // nem quebra de linha: mesmo que uma regra escape, nao sobra metacaractere
    // para montar um segundo comando.
    private static final Pattern PKG_RE = Pattern.compile("^[A-Za-z][A-Za-z0-9_]*(\\.[A-Za-z0-9_]+)+$");
    private static final Pattern NAME_RE = Pattern.compile("^[A-Za-z0-9._-]+$");
    private static final Pattern MODE_RE = Pattern.compile("^[0-7]{3,4}$");
    private static final Pattern PATH_RE = Pattern.compile("^/[A-Za-z0-9_./-]+$");

    public static final String MODS_ROOT = "/data/local/tmp/mods/";

    // Pacote Android: comeca com letra, ponto separa segmentos, sem espaco.
    public static void requirePkg(String pkg) {
        if (pkg == null || !PKG_RE.matcher(pkg).matches()) {
            throw new IllegalArgumentException("pacote invalido: " + pkg);
        }
    }

    // Nome de arquivo dentro de mods/<pkg>/: sem barra, sem "..", sem espaco.
    public static void requireFileName(String name) {
        if (name == null || !NAME_RE.matcher(name).matches() || name.contains("..")) {
            throw new IllegalArgumentException("nome de arquivo invalido: " + name);
        }
    }

    // Modo de chmod: 3 ou 4 digitos octais ("777; id" nao passa).
    public static void requireChmodMode(String mode) {
        if (mode == null || !MODE_RE.matcher(mode).matches()) {
            throw new IllegalArgumentException("modo de chmod invalido: " + mode);
        }
    }

    // Caminho absoluto com caracteres de caminho e nada mais. Vale tambem
    // para caminho que o device devolveu (ls de /data/adb/modules).
    public static void requirePath(String path, String what) {
        if (path == null || path.length() > 512 || !PATH_RE.matcher(path).matches()
                || path.contains("..")) {
            throw new IllegalArgumentException("caminho invalido (" + what + "): " + path);
        }
    }

    public static String modsDir(String pkg) {
        requirePkg(pkg);
        String dir = MODS_ROOT + pkg + "/";
        requirePath(dir, "mods dir");
        return dir;
    }

    public static String modsFile(String pkg, String name) {
        requirePkg(pkg);
        requireFileName(name);
        return modsDir(pkg) + name;
    }

    public static String stateFile(String pkg, String name) {
        requirePkg(pkg);
        requireFileName(name);
        String path = CrashGuardState.stateDir(pkg) + "/" + name;
        requirePath(path, "state file");
        return path;
    }

    // Resultado de recusa: os helpers que devolvem boolean/String devolvem
    // falha em vez de estourar excecao na thread de UI; quem quiser o
    // detalhe usa os require* diretamente (e os testes usam).
    private static Result rejected(String what, String value) {
        return new Result(-1, "", "", "Entrada invalida (" + what + "): " + value);
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
        String dir;
        try {
            dir = modsDir(pkg);
        } catch (IllegalArgumentException e) {
            return false;
        }
        Result r = exec("mkdir -p '" + dir + "' && chmod 755 '" + dir + "' && chcon " + SELINUX_MOD_CONTEXT + " '" + dir + "'");
        return r.success;
    }

    public static List<String> listFiles(String dirPath) {
        List<String> list = new ArrayList<>();
        try {
            requirePath(dirPath, "dir");
        } catch (IllegalArgumentException e) {
            return list;   // caminho hostil: lista vazia, nenhum comando roda
        }
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
        try {
            requirePath(filePath, "file");
        } catch (IllegalArgumentException e) {
            return null;
        }
        Result r = exec("cat '" + filePath + "' 2>/dev/null");
        return r.success ? r.stdout : null;
    }

    public static boolean writeTextFile(String filePath, String content) {
        try {
            requirePath(filePath, "file");
        } catch (IllegalArgumentException e) {
            return false;
        }
        try {
            // O conteudo NAO entra no comando: vai num arquivo temporario do
            // proprio app e so o caminho (validado) e copiado. Escrever por
            // stdin seria mais curto, mas se o su do device nao repassar o
            // stdin o `cat > destino` receberia EOF e deixaria o arquivo
            // VAZIO sem dar erro — o .conf/.patch do mod sumiria em silencio.
            File tmp = File.createTempFile("bep_su_write_", ".tmp");
            try (FileOutputStream fos = new FileOutputStream(tmp)) {
                fos.write(content.getBytes(StandardCharsets.UTF_8));
            }
            requirePath(tmp.getAbsolutePath(), "tmp");
            Result r = exec("cp '" + tmp.getAbsolutePath() + "' '" + filePath + "' && chmod 644 '" + filePath + "' && chcon " + SELINUX_MOD_CONTEXT + " '" + filePath + "'");
            tmp.delete();
            return r.success;
        } catch (IOException | IllegalArgumentException e) {
            return false;
        }
    }

    public static boolean copyFile(String srcPath, String destPath, String chmodMode) {
        String mode = (chmodMode != null) ? chmodMode : "644";
        try {
            requirePath(srcPath, "src");
            requirePath(destPath, "dest");
            requireChmodMode(mode);
        } catch (IllegalArgumentException e) {
            return false;
        }
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
        try {
            requirePath(filePath, "file");
        } catch (IllegalArgumentException e) {
            return false;
        }
        Result r = exec("rm -f '" + filePath + "'");
        return r.success;
    }

    public static boolean deleteMod(String pkg, String modId) {
        String base;
        try {
            base = modsFile(pkg, modId);
        } catch (IllegalArgumentException e) {
            return false;
        }
        Result r = exec("rm -f '" + base + ".'*");
        return r.success;
    }

    public static boolean toggleMod(String pkg, String filename, boolean enable) {
        String dir;
        String currentPath;
        try {
            dir = modsDir(pkg);
            requireFileName(filename);
            currentPath = dir + filename;
        } catch (IllegalArgumentException e) {
            return false;
        }
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
        requirePath(newPath, "toggle");
        Result r = exec("mv '" + currentPath + "' '" + newPath + "' && chcon " + SELINUX_MOD_CONTEXT + " '" + newPath + "'");
        return r.success;
    }

    public static boolean restartGame(String pkg) {
        try {
            requirePkg(pkg);
        } catch (IllegalArgumentException e) {
            return false;
        }
        // Encerra forcadamente e inicia o aplicativo principal
        String cmd = "am force-stop '" + pkg + "' && " +
                "(monkey -p '" + pkg + "' -c android.intent.category.LAUNCHER 1 2>/dev/null || " +
                "am start -n $(cmd package resolve-activity --brief '" + pkg + "' | tail -n 1) 2>/dev/null)";
        Result r = exec(cmd);
        return r.exitCode == 0;
    }

    public static String readLog(String pkg) {
        try {
            return readTextFile(stateFile(pkg, "log.txt"));
        } catch (IllegalArgumentException e) {
            return null;
        }
    }

    public static boolean clearLog(String pkg) {
        String path;
        try {
            path = stateFile(pkg, "log.txt");
        } catch (IllegalArgumentException e) {
            return false;
        }
        Result r = exec(": > '" + path + "'");
        return r.success;
    }

    public static String readDump(String pkg) {
        try {
            return readTextFile(stateFile(pkg, "dump.tsv"));
        } catch (IllegalArgumentException e) {
            return null;
        }
    }

    public static boolean deleteDump(String pkg) {
        try {
            return deleteFile(stateFile(pkg, "dump.tsv"));
        } catch (IllegalArgumentException e) {
            return false;
        }
    }

    // --- crashguard (F1d) ---------------------------------------------------

    // O marcador existe em algum dos dois lugares? (state dir do jogo, que e
    // onde o loader grava, e mods/<pkg>/, que outra versao do loader podia
    // usar — o jogo nao escreve em /data/local/tmp.)
    public static boolean hasCrashGuardMarker(String pkg) {
        try {
            requirePkg(pkg);
        } catch (IllegalArgumentException e) {
            return false;
        }
        Result r = exec("[ -f '" + CrashGuardState.markerPath(pkg) + "' ] || "
                + "[ -f '" + CrashGuardState.modsMarkerPath(pkg) + "' ] && echo yes || echo no");
        return r.success && r.stdout.contains("yes");
    }

    public static CrashGuardState.State readCrashGuard(String pkg) {
        try {
            requirePkg(pkg);
        } catch (IllegalArgumentException e) {
            return CrashGuardState.parse(null, false);
        }
        String counter = readTextFile(CrashGuardState.counterPath(pkg));
        return CrashGuardState.parse(counter, hasCrashGuardMarker(pkg));
    }

    // "Reativar": apaga o marcador e zera o contador. Sem zerar, o aviso
    // continuaria na tela mesmo com o jogo ja abrindo normal (o bloqueio
    // do loader dura so a janela de 20s, mas o arquivo do marcador nao
    // some sozinho).
    public static boolean reactivateMods(String pkg) {
        try {
            requirePkg(pkg);
        } catch (IllegalArgumentException e) {
            return false;
        }
        Result r = exec("mkdir -p '" + CrashGuardState.stateDir(pkg) + "' && "
                + "rm -f '" + CrashGuardState.markerPath(pkg) + "' '"
                + CrashGuardState.modsMarkerPath(pkg) + "' && "
                + "echo '0 '$(date +%s) > '" + CrashGuardState.counterPath(pkg) + "'");
        return r.success;
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
