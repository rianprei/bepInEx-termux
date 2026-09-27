package io.github.rianprei.bepinex.manager.core;

import io.github.rianprei.bepinex.manager.model.ModManifest;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.Map;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;
import java.util.zip.ZipOutputStream;

// Instalador e empacotador de arquivos .bmod (Contratos C1 e C2).
//
// O .bmod vem de fora (WhatsApp, navegador, outro device): e um zip
// HOSTIL ate prova em contrario. ZipEntry.getSize() nao serve para nada —
// e um numero que quem montou o arquivo escolheu, e um .bomb pode declarar
// 16 bytes e entregar 1 GB. Por isso todo limite aqui conta BYTES REAIS
// lidos do stream, e estourar o limite apaga o parcial em vez de deixar
// metade de um .so no disco.
public final class BmodInstaller {

    // Tetos. Manifest e .bpatch sao texto: alguns KB bastam e sobra. O .so e
    // binario: 64MB e o teto de um mod legitimo (o maior do repo, o
    // libdobby.a empacotado, tem 1,3MB).
    public static final long MAX_MANIFEST_BYTES = 64L * 1024;
    public static final long MAX_PAYLOAD_BYTES = 64L * 1024 * 1024;
    // .bpatch e texto de regra (~70 bytes por linha): 256KB ja sao ~3000
    // regras. Um .bpatch de 1MB nao e mod, e zip bomb.
    public static final long MAX_PATCH_BYTES = 256L * 1024;
    public static final int MAX_ENTRIES = 32;

    public static final class LimitExceededException extends IOException {
        public final String what;
        public final long limit;
        public final long real;

        LimitExceededException(String what, long limit, long real) {
            super(what + " passa do limite de " + human(limit) + " (o .bmod declara "
                    + "tamanho menor do que entrega: " + human(real) + " lidos). Instalação abortada, "
                    + "nada foi instalado.");
            this.what = what;
            this.limit = limit;
            this.real = real;
        }
    }

    public static final class InstallResult {
        public final boolean success;
        public final String message;
        public final ModManifest manifest;

        public InstallResult(boolean success, String message, ModManifest manifest) {
            this.success = success;
            this.message = message;
            this.manifest = manifest;
        }
    }

    private BmodInstaller() {}

    private static String human(long bytes) {
        if (bytes >= 1024L * 1024 * 1024) return (bytes / (1024L * 1024 * 1024)) + " GB";
        if (bytes >= 1024L * 1024) return (bytes / (1024L * 1024)) + " MB";
        if (bytes >= 1024L) return (bytes / 1024) + " KB";
        return bytes + " bytes";
    }

    // Inspeciona um arquivo .bmod e le seu manifest.json (com teto de 64KB)
    public static ModManifest inspect(File bmodFile) throws IOException {
        if (bmodFile == null || !bmodFile.exists()) {
            throw new IOException("Arquivo .bmod não encontrado");
        }

        try (ZipFile zip = new ZipFile(bmodFile)) {
            requireEntryCount(zip);
            requireSafeEntryNames(zip);
            ZipEntry manifestEntry = zip.getEntry("manifest.json");
            if (manifestEntry == null) {
                throw new IOException("Arquivo .bmod inválido: manifest.json ausente");
            }

            try (InputStream is = zip.getInputStream(manifestEntry)) {
                String json = readStreamLimited(is, MAX_MANIFEST_BYTES, "manifest.json");
                return ManifestParser.parse(json);
            }
        }
    }

    // Extrai manifest e payload para tmpDir, com teto, e devolve o arquivo do
    // payload. Publica para o teste de zip bomb poder chamar direto, sem root.
    // Se estourar qualquer limite, apaga o parcial e lanca.
    public static File extractBmod(File bmodFile, ModManifest manifest, File tmpDir) throws IOException {
        if (!tmpDir.exists() && !tmpDir.mkdirs()) {
            throw new IOException("Não deu para criar " + tmpDir);
        }

        try (ZipFile zip = new ZipFile(bmodFile)) {
            requireEntryCount(zip);
            requireSafeEntryNames(zip);

            // 1. manifest.json -> <id>.json
            File jsonFile = new File(tmpDir, manifest.id + ".json");
            ZipEntry manifestEntry = zip.getEntry("manifest.json");
            if (manifestEntry == null) {
                throw new IOException("Arquivo .bmod inválido: manifest.json ausente");
            }
            try (InputStream is = zip.getInputStream(manifestEntry);
                 FileOutputStream fos = new FileOutputStream(jsonFile)) {
                copyLimited(is, fos, MAX_MANIFEST_BYTES, "manifest.json");
            } catch (IOException e) {
                jsonFile.delete();
                throw e;
            }

            // 2. payload (mod.so ou mod.bpatch), teto por tipo
            boolean nativePayload = "native".equals(manifest.type);
            String entryName = nativePayload ? "mod.so" : "mod.bpatch";
            long limit = nativePayload ? MAX_PAYLOAD_BYTES : MAX_PATCH_BYTES;
            String destName = manifest.id + (nativePayload ? ".so" : ".bpatch");

            ZipEntry payloadEntry = zip.getEntry(entryName);
            if (payloadEntry == null) {
                jsonFile.delete();
                throw new IOException("Arquivo " + entryName + " ausente no pacote .bmod");
            }
            File payloadFile = new File(tmpDir, destName);
            try (InputStream is = zip.getInputStream(payloadEntry);
                 FileOutputStream fos = new FileOutputStream(payloadFile)) {
                copyLimited(is, fos, limit, entryName);
            } catch (IOException e) {
                jsonFile.delete();
                payloadFile.delete();   // parcial some com a recusa
                throw e;
            }
            return payloadFile;
        }
    }

    // Instala o .bmod no diretorio /data/local/tmp/mods/<targetPkg>/
    public static InstallResult install(File bmodFile, String targetPkg, String detectedEngine) {
        ModManifest manifest;
        try {
            manifest = inspect(bmodFile);
        } catch (Exception e) {
            return new InstallResult(false, "Erro ao ler .bmod: " + e.getMessage(), null);
        }

        if (!manifest.matchesGame(targetPkg)) {
            return new InstallResult(false,
                    "Incompatibilidade: este mod foi feito para '" + manifest.game + "', mas o jogo selecionado é '" + targetPkg + "'.",
                    manifest);
        }

        if (detectedEngine != null && !manifest.matchesEngine(detectedEngine)) {
            return new InstallResult(false,
                    "Incompatibilidade: o mod pede a versão '" + manifest.engine + "' do jogo, mas a instalada é '" + detectedEngine + "'.",
                    manifest);
        }

        if (!SuHelper.isRootAvailable()) {
            return new InstallResult(false, "Permissão root não disponível. Impossível instalar o mod.", manifest);
        }

        File tmpDir = new File(bmodFile.getParentFile(), "tmp_bmod_" + manifest.id);
        tmpDir.mkdirs();

        try {
            // Extrai com teto: um .bmod que estoura limite nao chega perto do
            // device (e o parcial ja foi apagado dentro do extractBmod).
            File payloadFile = extractBmod(bmodFile, manifest, tmpDir);
            File jsonFile = new File(tmpDir, manifest.id + ".json");

            // 3. Gerar arquivo .conf com os valores padrao se houver opcoes
            File confFile = null;
            if (manifest.options != null && !manifest.options.isEmpty()) {
                confFile = new File(tmpDir, manifest.id + ".conf");
                Map<String, String> defaults = ConfManager.getDefaults(manifest);
                String confContent = "# bepInEx mod config (Contrato C3)\n" + ConfManager.format(defaults);
                try (FileOutputStream fos = new FileOutputStream(confFile)) {
                    fos.write(confContent.getBytes(StandardCharsets.UTF_8));
                }
            }

            // 4. Copiar para /data/local/tmp/mods/<targetPkg>/
            String destDir = SuHelper.modsDir(targetPkg);
            SuHelper.ensureModDir(targetPkg);

            SuHelper.installFile(jsonFile.getAbsolutePath(), destDir + jsonFile.getName(), "644");
            SuHelper.installFile(payloadFile.getAbsolutePath(), destDir + payloadFile.getName(), "644");
            if (confFile != null && confFile.exists()) {
                SuHelper.installFile(confFile.getAbsolutePath(), destDir + confFile.getName(), "644");
            }

            return new InstallResult(true, "Mod '" + manifest.name + "' instalado com sucesso!", manifest);
        } catch (Exception e) {
            return new InstallResult(false, "Falha na instalação: " + e.getMessage(), manifest);
        } finally {
            // Limpa arquivos temporarios
            File[] files = tmpDir.listFiles();
            if (files != null) {
                for (File f : files) f.delete();
            }
            tmpDir.delete();
        }
    }

    // Cria um arquivo .bmod (zip) para compartilhar / exportar
    public static File createBmod(ModManifest manifest, String payloadContentOrPath, boolean isContent, File destDir) throws IOException {
        if (!destDir.exists()) destDir.mkdirs();
        File bmodFile = new File(destDir, manifest.id + ".bmod");

        try (ZipOutputStream zos = new ZipOutputStream(new FileOutputStream(bmodFile))) {
            // manifest.json
            zos.putNextEntry(new ZipEntry("manifest.json"));
            zos.write(ManifestParser.toJson(manifest).getBytes(StandardCharsets.UTF_8));
            zos.closeEntry();

            // mod.bpatch ou mod.so
            String entryName = "native".equals(manifest.type) ? "mod.so" : "mod.bpatch";
            zos.putNextEntry(new ZipEntry(entryName));
            if (isContent) {
                zos.write(payloadContentOrPath.getBytes(StandardCharsets.UTF_8));
            } else {
                try (FileInputStream fis = new FileInputStream(payloadContentOrPath)) {
                    copyStream(fis, zos);
                }
            }
            zos.closeEntry();
        }

        return bmodFile;
    }

    // Um .bmod legitimo tem manifest.json + payload. Mais que isso e o
    // atacante tentando encher o disco de arquivos.
    private static void requireEntryCount(ZipFile zip) throws IOException {
        int entries = zip.size();
        if (entries > MAX_ENTRIES) {
            throw new IOException("O .bmod tem " + entries + " entradas; o limite é " + MAX_ENTRIES
                    + ". Instalação abortada.");
        }
    }

    // Zip-slip: entrada cujo NOME sai da pasta de extracao. Hoje o payload
    // sai por nome fixo (mod.so/mod.bpatch), mas nada garante que um futuro
    // fluxo use entry.getName() — e a defesa tem que morar onde o zip e
    // lido, nao na memoria de quem escreveu o extrator. Barra "../", ".."
    // isolado, caminho absoluto, disco de Windows e "./" disfarcado. O
    // id do manifest tem regex propria ([a-z0-9-]{3,48}), que ja impede o
    // MESMO ataque via manifest.id nos caminhos <id>.json/tmp_bmod_<id>.
    private static void requireSafeEntryNames(ZipFile zip) throws IOException {
        java.util.Enumeration<? extends ZipEntry> entries = zip.entries();
        while (entries.hasMoreElements()) {
            String name = entries.nextElement().getName();
            String normalized = name.replace('\\', '/');
            boolean unsafe = normalized.contains("../")
                    || normalized.equals("..")
                    || normalized.startsWith("/")
                    || normalized.matches("[A-Za-z]:.*");
            if (unsafe) {
                throw new IOException("O .bmod tem entrada com caminho perigoso ('" + name
                        + "', que sairia da pasta de instalação: ataque zip-slip). "
                        + "Instalação abortada, nada foi instalado.");
            }
        }
    }

    private static String readStreamLimited(InputStream is, long max, String what) throws IOException {
        ByteArrayOutputStream baos = new ByteArrayOutputStream();
        copyLimited(is, baos, max, what);
        return new String(baos.toByteArray(), StandardCharsets.UTF_8);
    }

    // Conta os BYTES REAIS que saem do stream (nunca getSize(), que o
    // atacante controla) e barra antes de escrever o bloco que estouraria.
    private static long copyLimited(InputStream in, OutputStream out, long max, String what) throws IOException {
        byte[] buf = new byte[64 * 1024];
        long total = 0;
        int n;
        while ((n = in.read(buf)) != -1) {
            total += n;
            if (total > max) {
                throw new LimitExceededException(what, max, total);
            }
            out.write(buf, 0, n);
        }
        return total;
    }

    private static void copyStream(InputStream in, OutputStream out) throws IOException {
        byte[] buf = new byte[4096];
        int n;
        while ((n = in.read(buf)) != -1) {
            out.write(buf, 0, n);
        }
    }
}
