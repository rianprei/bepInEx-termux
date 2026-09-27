package io.github.rianprei.bepinex.manager.core;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.zip.ZipFile;

// Instala mod de qualquer origem (C7): le o arquivo escolhido pelo usuario,
// pede a deteccao POR CONTEUDO ao ModContentDetector e instala so o que
// realmente roda neste celular.
//
// Nada aqui abre o jogo, nada aqui escreve no APK/OBB/arquivos do jogo: o
// arquivo vai para /data/local/tmp/mods/<pkg>/ (C1), via su, com 644 e o
// contexto bepinex_mod_file (F1c) — quem escreve no device e o SuHelper.
public final class LooseModInstaller {

    public static final class Result {
        public final boolean success;
        public final String message;
        public final ModContentDetector.Kind kind;
        public final String installedAs;   // nome final em mods/<pkg>/, ou null

        Result(boolean success, String message, ModContentDetector.Kind kind, String installedAs) {
            this.success = success;
            this.message = message;
            this.kind = kind;
            this.installedAs = installedAs;
        }
    }

    // Limite de leitura para deteccao de texto (regra .patch e script .js sao
    // pequenos) e do arquivo inteiro na busca da assinatura do gadget (o
    // frida-gadget tem ~25MB; acima disso ja gastamos memoria a toa).
    private static final int TEXT_LIMIT = 512 * 1024;
    private static final int MARKER_SCAN_LIMIT = 32 * 1024 * 1024;
    private static final int HEAD_SIZE = 64 * 1024;

    // Mesmos nomes que o u_frida exige (mods/u_frida/jni/u_frida_config.h):
    // <stem>.config ao lado do binario, e o binario SEM .so para o loader
    // nao da dlopen nele sem config.
    public static final String GADGET_BIN = "frida-gadget.bin";
    public static final String GADGET_CONFIG = "frida-gadget.config";

    private LooseModInstaller() {}

    public static Result installFromFile(File src, String pkg, String engine) {
        if (src == null || !src.exists()) {
            return new Result(false, "Arquivo não encontrado.", null, null);
        }
        if (pkg == null || pkg.isEmpty()) {
            return new Result(false, "Jogo não selecionado.", null, null);
        }

        ModContentDetector.Sample sample;
        try {
            sample = probe(src);
        } catch (IOException e) {
            return new Result(false, "Não deu para ler o arquivo: " + e.getMessage(), null, null);
        }

        boolean engineIl2cpp = (engine == null) || engine.contains("il2cpp");
        ModContentDetector.Detection det = ModContentDetector.detect(sample, engineIl2cpp);

        // .bmod tem o proprio caminho de instalacao (C2): manifest + payload.
        if (det.kind == ModContentDetector.Kind.BMOD) {
            BmodInstaller.InstallResult r = BmodInstaller.install(src, pkg, engine);
            return new Result(r.success, r.message, det.kind, null);
        }

        // Garantia (c): o frida-gadget NUNCA vira <id>.so. O loader da dlopen
        // em qualquer .so da pasta, sem config, e o gadget sem config cai no
        // modo padrao (listen) — abre porta e segura o jogo. Ele entra pelo
        // caminho certo: frida-gadget.bin (SEM .so) + frida-gadget.config em
        // modo script-directory apontando para a propria pasta de mods.
        if (det.kind == ModContentDetector.Kind.FRIDA_GADGET) {
            return installGadget(src, pkg);
        }

        if (!det.installable) {
            return new Result(false, det.reason, det.kind, null);
        }
        if (!SuHelper.isRootAvailable()) {
            return new Result(false, "Permissão root não disponível. Impossível instalar o mod.",
                    det.kind, null);
        }

        String destName = det.targetId + det.targetExt;
        String destPath = "/data/local/tmp/mods/" + pkg + "/" + destName;
        if (!SuHelper.ensureModDir(pkg)) {
            return new Result(false,
                    "Não deu para preparar a pasta de mods do jogo (sem root?).",
                    det.kind, null);
        }
        if (!SuHelper.installFile(src.getAbsolutePath(), destPath, "644")) {
            return new Result(false,
                    "A cópia para a pasta de mods falhou. Sem a liberação de segurança "
                            + "do Android (SELinux), o jogo não consegue ler o arquivo.",
                    det.kind, null);
        }

        return new Result(true, "Instalado: " + destName + "\n" + posInstallHint(det), det.kind, destName);
    }

    // Instala o gadget no lugar certo: frida-gadget.bin + frida-gadget.config
    // na pasta de mods. NUNCA como .so (veja o comentario do chamador).
    private static Result installGadget(File src, String pkg) {
        if (!SuHelper.isRootAvailable()) {
            return new Result(false, "Permissão root não disponível. Impossível instalar o mod.",
                    ModContentDetector.Kind.FRIDA_GADGET, null);
        }
        String dir = "/data/local/tmp/mods/" + pkg + "/";
        if (!SuHelper.ensureModDir(pkg)) {
            return new Result(false, "Não deu para preparar a pasta de mods do jogo (sem root?).",
                    ModContentDetector.Kind.FRIDA_GADGET, null);
        }

        // 1. Remove um .so do gadget que alguém tenha copiado na mão: o
        // loader abriria ele sozinho, sem config, e o jogo travaria.
        SuHelper.deleteFile(dir + "frida-gadget.so");
        SuHelper.deleteFile(dir + "libfrida-gadget.so");

        // 2. Binário como .bin (sem .so) e config no modo script.
        if (!SuHelper.installFile(src.getAbsolutePath(), dir + GADGET_BIN, "644")) {
            return new Result(false, "A cópia do programa do Frida falhou. Sem a liberação de "
                            + "segurança do Android (SELinux), o jogo não lê o arquivo.",
                    ModContentDetector.Kind.FRIDA_GADGET, null);
        }
        String config = "{\"interaction\":{\"type\":\"script-directory\",\"path\":\"" + dir
                + "\",\"on_change\":\"ignore\"}}";
        if (!SuHelper.writeTextFile(dir + GADGET_CONFIG, config)) {
            SuHelper.deleteFile(dir + GADGET_BIN);
            return new Result(false, "Não deu para escrever a configuração do Frida "
                    + "(frida-gadget.config). Sem ela o programa do Frida trava o jogo esperando "
                    + "um computador conectar; por isso o arquivo também foi removido.",
                    ModContentDetector.Kind.FRIDA_GADGET, null);
        }

        return new Result(true, "Frida instalado na pasta do jogo (frida-gadget.bin + "
                + "frida-gadget.config).\n"
                + "Agora é só largar o script .js nessa mesma pasta e abrir o jogo: o script roda "
                + "sozinho, sem computador e sem porta aberta. Se a pasta não tiver nenhum .js, o "
                + "Frida nem carrega.", ModContentDetector.Kind.FRIDA_GADGET, GADGET_BIN);
    }

    // O que o usuario precisa saber DEPOIS de instalar (honesto: o que roda,
    // o que depende de outra coisa).
    private static String posInstallHint(ModContentDetector.Detection det) {
        switch (det.kind) {
            case ELF_ARM64:
                return "Abra o jogo para o mod carregar. Se o jogo fechar 2 vezes seguidas na hora "
                        + "de abrir, a proteção desliga os mods: volte aqui e toque em 'Reativar'.";
            case PATCH:
                return "Regras .patch instaladas. Elas só valem se o motor de patches estiver "
                        + "instalado (o criador de mods instala); sem ele o arquivo fica guardado, "
                        + "sem efeito.";
            case FRIDA_JS:
                return "Script .js instalado. Para ele rodar de verdade, o Frida precisa estar na "
                        + "MESMA pasta (como frida-gadget.bin, junto do frida-gadget.config). Sem "
                        + "isso, o script fica guardado e sem efeito.";
            default:
                return "Abra o jogo para carregar.";
        }
    }

    // Le o arquivo e monta a Sample: cabecalho (magic), texto (se for texto),
    // manifest.json e nomes de entradas (se zip) e assinatura do frida-gadget.
    public static ModContentDetector.Sample probe(File src) throws IOException {
        byte[] head = readHead(src, HEAD_SIZE);
        boolean isZip = ModContentDetector.isZip(head);
        String text = null;
        if (!isZip && !looksBinary(head)) {
            text = readText(src, TEXT_LIMIT);
        }
        boolean zipHasManifest = false;
        byte[] zipManifestBytes = null;
        java.util.List<String> zipEntryNames = java.util.Collections.emptyList();
        if (isZip) {
            try (ZipFile zip = new ZipFile(src)) {
                zipEntryNames = new java.util.ArrayList<>();
                java.util.Enumeration<? extends java.util.zip.ZipEntry> entries = zip.entries();
                while (entries.hasMoreElements()) {
                    zipEntryNames.add(entries.nextElement().getName());
                }
                java.util.zip.ZipEntry manifestEntry = zip.getEntry("manifest.json");
                if (manifestEntry != null) {
                    zipHasManifest = true;
                    // Mesmo teto do BmodInstaller (64KB): so o comeco basta
                    // para dizer se e o manifest do projeto (formato 1 + id).
                    zipManifestBytes = readLimited(zip.getInputStream(manifestEntry), 64 * 1024);
                }
            } catch (IOException ignored) {
                zipHasManifest = false;   // zip corrompido: cai no texto de "compactado"
                zipManifestBytes = null;
                zipEntryNames = java.util.Collections.emptyList();
            }
        }
        boolean fridaMarker = (text != null && ModContentDetector.hasFridaMarker(text))
                || scanFridaMarker(src);
        // length = tamanho REAL do arquivo: a checagem de PT_LOAD compara
        // offset+tamanho com ele. Um .so pela metade (corte de download)
        // passa no magic e morre aqui.
        return new ModContentDetector.Sample(src.getName(), head, text, zipHasManifest,
                fridaMarker, src.length(), zipManifestBytes, zipEntryNames);
    }

    // Le no maximo max bytes de um stream (para o manifesto, que nao
    // precisa passar do teto para a deteccao decidir).
    private static byte[] readLimited(InputStream in, int max) throws IOException {
        try (InputStream lim = new LimitInputStream(in, max)) {
            ByteArrayOutputStream baos = new ByteArrayOutputStream();
            byte[] buf = new byte[8192];
            int n;
            while ((n = lim.read(buf)) > 0) {
                baos.write(buf, 0, n);
            }
            return baos.toByteArray();
        }
    }

    // Corta o stream em max bytes, sem ler o resto da entrada.
    private static final class LimitInputStream extends java.io.FilterInputStream {
        private long remaining;
        LimitInputStream(InputStream in, long max) {
            super(in);
            this.remaining = max;
        }
        @Override
        public int read() throws IOException {
            if (remaining <= 0) return -1;
            int b = super.read();
            if (b >= 0) remaining--;
            return b;
        }
        @Override
        public int read(byte[] b, int off, int len) throws IOException {
            if (remaining <= 0) return -1;
            int n = super.read(b, off, (int) Math.min(len, remaining));
            if (n > 0) remaining -= n;
            return n;
        }
    }

    private static byte[] readHead(File src, int max) throws IOException {
        try (InputStream in = new FileInputStream(src)) {
            ByteArrayOutputStream baos = new ByteArrayOutputStream();
            byte[] buf = new byte[8192];
            int total = 0, n;
            while (total < max && (n = in.read(buf, 0, Math.min(buf.length, max - total))) > 0) {
                baos.write(buf, 0, n);
                total += n;
            }
            return baos.toByteArray();
        }
    }

    private static String readText(File src, int max) throws IOException {
        try (InputStream in = new FileInputStream(src)) {
            ByteArrayOutputStream baos = new ByteArrayOutputStream();
            byte[] buf = new byte[8192];
            int total = 0, n;
            while (total < max && (n = in.read(buf, 0, Math.min(buf.length, max - total))) > 0) {
                baos.write(buf, 0, n);
                total += n;
            }
            return new String(baos.toByteArray(), StandardCharsets.UTF_8);
        }
    }

    // Byte 0 em arquivo e sinal forte de binario. Sem isso, um .so lido como
    // UTF-8 vira lixo com NUL e a deteccao de texto mentiria.
    private static boolean looksBinary(byte[] head) {
        int limit = Math.min(head.length, 4096);
        for (int i = 0; i < limit; i++) {
            if (head[i] == 0) return true;
        }
        return false;
    }

    // Uma passada so pelo arquivo, procurando qualquer marca do gadget. O
    // buffer mantem a sobreposicao do maior marcador para nao cortar no meio.
    private static boolean scanFridaMarker(File src) throws IOException {
        int overlap = ModContentDetector.maxFridaMarkerLength() - 1;
        try (InputStream in = new FileInputStream(src)) {
            byte[] buf = new byte[64 * 1024];
            String carry = "";
            long total = 0;
            int n;
            while ((n = in.read(buf)) > 0) {
                String window = carry + new String(buf, 0, n, StandardCharsets.ISO_8859_1);
                if (ModContentDetector.hasFridaMarker(window)) return true;
                if (window.length() > overlap) {
                    carry = window.substring(window.length() - overlap);
                } else {
                    carry = window;
                }
                total += n;
                if (total >= MARKER_SCAN_LIMIT) return false;
            }
        }
        return false;
    }
}
