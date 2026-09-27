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
import java.util.Collection;
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
    private static final Pattern ACTIVITY_CLASS_RE =
            Pattern.compile("^\\.?[A-Za-z_$][A-Za-z0-9_$]*(?:\\.[A-Za-z_$][A-Za-z0-9_$]*)*$");

    public static final String MODS_ROOT = "/data/local/tmp/mods/";

    // Pacote Android: comeca com letra, ponto separa segmentos, sem espaco.
    public static void requirePkg(String pkg) {
        if (pkg == null || !PKG_RE.matcher(pkg).matches()) {
            throw new IllegalArgumentException("pacote invalido: " + pkg);
        }
    }

    public static String requireActivityComponent(String pkg, String component) {
        requirePkg(pkg);
        if (component == null || component.indexOf('\n') >= 0 || component.indexOf('\r') >= 0) {
            throw new IllegalArgumentException("atividade de início inválida");
        }
        int separator = component.indexOf('/');
        if (separator <= 0 || separator != component.lastIndexOf('/')
                || !pkg.equals(component.substring(0, separator))) {
            throw new IllegalArgumentException("atividade de início não pertence ao pacote");
        }
        String className = component.substring(separator + 1);
        if (!ACTIVITY_CLASS_RE.matcher(className).matches()
                || className.equals(".")
                || (!className.startsWith(".") && !className.startsWith(pkg + "."))) {
            throw new IllegalArgumentException("nome da atividade de início inválido");
        }
        return component;
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

    /**
     * Comando que DEVOLVE o dono de um arquivo para o dono do diretório pai
     * (o app dono de /data/data/<pkg>/files). O Manager roda como root, e
     * qualquer arquivo que ele crie ali fica root:root — aí o processo do
     * jogo (outro uid) não consegue mais abrir pra appendar e TODOS os mods
     * ficam mudos, sem erro nenhum visível. Achado da rodada de device
     * 2026-09-26: log.txt apareceu root:root e ninguém soube por quê.
     *
     * toybox não tem chown --reference, então o uid:gid do pai vem de
     * stat -c %u:%g. Função pura (sem exec) para o teste_host poder conferir
     * o comando sem root.
     */
    public static String ownerFixCommand(String filePath) {
        int slash = filePath.lastIndexOf('/');
        String parent = slash > 0 ? filePath.substring(0, slash) : "/";
        return "chown \"$(stat -c %u:%g '" + parent + "')\" '" + filePath + "' && chmod 644 '" + filePath + "'";
    }

    /** Aplica ownerFixCommand. false = nem tentou (caminho hostil). */
    public static boolean ensureOwner(String filePath) {
        try {
            requirePath(filePath, "file");
        } catch (IllegalArgumentException e) {
            return false;
        }
        return exec(ownerFixCommand(filePath)).success;
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
        return exec(ensureModDirCommand(dir)).success;
    }

    /**
     * Cadeia que cria a pasta de mods com contexto SELinux. Builder PURO
     * (sem exec) para o teste de host rodar em sh de verdade: `chcon` não
     * existe no host, então o teste põe um stub à frente do PATH. A string é
     * byte a byte a que ia para o device — mesma ordem, mesmo &&, mesmo
     * contexto.
     */
    public static String ensureModDirCommand(String dir) {
        return "mkdir -p '" + dir + "' && chmod 755 '" + dir + "' && chcon " + SELINUX_MOD_CONTEXT + " '" + dir + "'";
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

    /**
     * Inventário de mods de TODOS os apps: UMA chamada root (o laço por app
     * acontecia dentro do shell do device). Existe por causa do travamento de
     * 2026-09-27 — ver ModInventory.
     */
    public static String listAllModsInventory() {
        return exec(ModInventory.command()).stdout;
    }

    /**
     * Conteúdo de vários arquivos do device em UMA chamada, com separador
     * (mesma economia: um `su` por item de lista é o que estourou memória).
     * Devolve null em falha.
     */
    public static String readTextFiles(String dir, Collection<String> names) {
        if (names == null || names.isEmpty()) return "";
        // O diretório entra num comando root: passa pela validação central como
        // qualquer outro caminho. A guarda só de null não impedia nada.
        requirePath(dir, "dir");
        for (String n : names) requireFileName(n);
        return exec(bundleCommand(dir, names)).stdout;
    }

    /**
     * Conteúdo de vários arquivos numa chamada só, com separador em linha
     * inteira. O separador vai para a SAÍDA de propósito: mandá-lo para
     * /dev/null (a primeira versão) fazia o parse nunca achar o início do
     * arquivo e a tela de jogo nunca mostrava o manifest. Cada arquivo tem o
     * nome validado, então o separador não colide com nome de arquivo.
     */
    public static String bundleCommand(String dir, Collection<String> names) {
        StringBuilder cmd = new StringBuilder();
        for (String n : names) {
            cmd.append("echo '").append(BUNDLE_SEP).append(n).append("'; ")
               .append("cat '").append(dir).append("/").append(n).append("' 2>/dev/null; ")
               // Newline DEPOIS de cada arquivo: sem isso, um .json que não
               // existe (cat falha e não imprime nada) deixa o separador
               // seguinte grudado na linha do conteúdo anterior e o parse
               // perde o arquivo. Achado rodando o comando em sh de verdade.
               .append("echo; ");
        }
        // Sai com 0 mesmo com o último cat falhando: um .json ausente não pode
        // fazer a tela de jogo inteiro ler "sem log" (o exit do su é o que o
        // SuHelper chama de sucesso).
        cmd.append("true");
        return cmd.toString();
    }

    /** Separador da resposta de readTextFiles (linha inteira, nome do arquivo). */
    public static final String BUNDLE_SEP = "@@@FILE:";

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
            Result r = exec(writeTextFileCommand(tmp.getAbsolutePath(), filePath));
            tmp.delete();
            // cp como root cria o arquivo root:root. Se o destino for dentro
            // de /data/data/<pkg>/files (o state dir do jogo), isso tranca o
            // app fora do próprio log — devolve o dono do diretório pai.
            if (r.success && filePath.contains("/files/")) ensureOwner(filePath);
            return r.success;
        } catch (IOException | IllegalArgumentException e) {
            return false;
        }
    }

    /** Cadeia de escrita por cópia (conteúdo já validado, entra por arquivo). */
    public static String writeTextFileCommand(String tmpPath, String destPath) {
        return "cp '" + tmpPath + "' '" + destPath + "' && chmod 644 '" + destPath + "' && chcon "
                + SELINUX_MOD_CONTEXT + " '" + destPath + "'";
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
        return exec(copyFileCommand(srcPath, destPath, mode)).success;
    }

    public static DownloadFilePicker.Listing listDownloadFiles() {
        return DownloadFilePicker.list(command -> {
            Result result = exec(command);
            if (!result.success) {
                throw new IllegalStateException(result.friendlyError != null
                        ? result.friendlyError : "Falha ao listar Download/Documents.");
            }
            return result.stdout;
        });
    }

    public static boolean copyDownloadFileToCache(String sourcePath, String destinationPath) {
        try {
            String command = DownloadFilePicker.copyToCacheCommand(sourcePath, destinationPath);
            return exec(command).success;
        } catch (IllegalArgumentException e) {
            return false;
        }
    }

    /** Cópia forçada com modo e contexto SELinux (o `mode` já foi validado). */
    public static String copyFileCommand(String srcPath, String destPath, String mode) {
        return "cp -f '" + srcPath + "' '" + destPath + "' && chmod " + mode + " '" + destPath
                + "' && chcon " + SELINUX_MOD_CONTEXT + " '" + destPath + "'";
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
        return exec(toggleModCommand(currentPath, newPath)).success;
    }

    /** Ligar/desligar mod: renomeia e reaplica o contexto no nome novo. */
    public static String toggleModCommand(String currentPath, String newPath) {
        return "mv '" + currentPath + "' '" + newPath + "' && chcon " + SELINUX_MOD_CONTEXT + " '" + newPath + "'";
    }

    public static boolean restartGame(String pkg, String activityComponent) {
        try {
            requirePkg(pkg);
            requireActivityComponent(pkg, activityComponent);
        } catch (IllegalArgumentException e) {
            return false;
        }
        Result r = exec(restartGameCommand(pkg, activityComponent));
        return r.exitCode == 0;
    }

    public static String restartGameCommand(String pkg, String activityComponent) {
        requirePkg(pkg);
        requireActivityComponent(pkg, activityComponent);
        return "am force-stop '" + pkg + "' && am start -n '" + activityComponent + "' 2>/dev/null";
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

    /**
     * Contador + marcador do crashguard em UMA chamada root (eram duas: um cat
     * e um test -f). Mesmo orçamento do resto: chamada por tela, nunca por item.
     */
    public static CrashGuardState.State readCrashGuard(String pkg) {
        try {
            requirePkg(pkg);
        } catch (IllegalArgumentException e) {
            return CrashGuardState.parse(null, false);
        }
        return parseCrashGuard(exec(crashGuardCommand(pkg, CrashGuardState.counterPath(pkg),
                CrashGuardState.markerPath(pkg), CrashGuardState.modsMarkerPath(pkg))).stdout);
    }

    /**
     * Contador + marcador do crashguard em UMA chamada root (eram duas: um
     * cat e um test -f). Saída com CHAVES, não com posições: o stdout volta
     * trim() e a versão anterior caçava a última quebra de linha para
     * separar o contador do marcador — sem marcador, o contador perdia o
     * "\n" e virava string vazia, e o contador de mortes sumia.
     */
    public static String crashGuardCommand(String pkg) {
        return crashGuardCommand(pkg, CrashGuardState.counterPath(pkg),
                CrashGuardState.markerPath(pkg), CrashGuardState.modsMarkerPath(pkg));
    }

    public static String crashGuardCommand(String pkg, String counterPath,
                                           String markerPath, String modsMarkerPath) {
        return "echo deaths=$(cat '" + counterPath + "' 2>/dev/null | cut -d' ' -f1);"
                + " echo ts=$(cat '" + counterPath + "' 2>/dev/null | cut -d' ' -f2);"
                + " if [ -f '" + markerPath + "' ] || [ -f '" + modsMarkerPath + "' ]; "
                + "then echo marker=yes; else echo marker=no; fi";
    }

    /** Parse puro das chaves do crashguard. */
    public static CrashGuardState.State parseCrashGuard(String stdout) {
        boolean marker = false;
        String deaths = "", ts = "";
        for (String line : (stdout == null ? "" : stdout).split("\\r?\\n")) {
            String t = line.trim();
            if (t.startsWith("deaths=")) deaths = t.substring(7).trim();
            else if (t.startsWith("ts=")) ts = t.substring(3).trim();
            else if (t.startsWith("marker=")) marker = "yes".equals(t.substring(7).trim());
        }
        if (deaths.isEmpty() && ts.isEmpty() && !marker) {
            return CrashGuardState.parse(null, false);
        }
        return CrashGuardState.parse(deaths + " " + ts, marker);
    }

    // "Reativar": apaga o marcador e zera o contador. Sem zerar, o aviso
    // continuaria na tela mesmo com o jogo ja abrindo normal (o bloqueio
    // do loader dura so a janela de 20s, mas o arquivo do marcador nao
    // some sozinho).
    /**
     * Plano do "Reativar", como string de comando — puro, para o teste de host
     * garantir o invariante que quebrou o device: <b>o Manager NUNCA cria o
     * state dir do jogo</b> (é do app) e nunca escreve nele sem devolver o
     * dono. Devolve null quando não há o que reativar.
     */
    public static String reactivateCommand(String pkg, boolean stateDirExists) {
        if (!stateDirExists) return null;
        return "rm -f '" + CrashGuardState.markerPath(pkg) + "' '"
                + CrashGuardState.modsMarkerPath(pkg) + "' && "
                + "echo '0 '$(date +%s) > '" + CrashGuardState.counterPath(pkg) + "'";
    }

    public static boolean reactivateMods(String pkg) {
        try {
            requirePkg(pkg);
        } catch (IllegalArgumentException e) {
            return false;
        }
        // NÃO cria o state dir: essa pasta é do APP (o loader e os mods
        // criam na primeira linha de log, com o uid do app). Um
        // `mkdir -p` aqui roda como root e, quando a pasta ainda não existe
        // (instalação nova, ou logo depois do `rm -rf` do kit de teste),
        // deixa /data/data/<pkg>/files/bepinex em root:root 0755 — o app
        // ganha só r-x, o open(O_CREAT|O_APPEND) do log dá EACCES e todos
        // os mods ficam mudos. Era a causa raiz do incidente de 2026-09-26.
        //
        // Se a pasta não existe, não há o que reativar: o jogo nunca rodou
        // com mods, e o app vai criá-la com o dono certo.
        String stateDir = CrashGuardState.stateDir(pkg);
        Result has = exec("[ -d '" + stateDir + "' ] && echo yes || echo no");
        boolean stateDirExists = has.success && has.stdout.contains("yes");
        String cmd = reactivateCommand(pkg, stateDirExists);
        if (cmd == null) return true;   // nada criado ainda: o app faz quando rodar
        Result r = exec(cmd);
        // O contador também é do app: o loader reescreve a cada 2s de vida.
        if (r.success) ensureOwner(CrashGuardState.counterPath(pkg));
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
