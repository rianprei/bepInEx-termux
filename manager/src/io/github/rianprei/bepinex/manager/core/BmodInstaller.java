package io.github.rianprei.bepinex.manager.core;

import io.github.rianprei.bepinex.manager.model.ModManifest;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.Map;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;
import java.util.zip.ZipOutputStream;

// Instalador e empacotador de arquivos .bmod (Contratos C1 e C2).
public final class BmodInstaller {
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

    // Inspeciona um arquivo .bmod e le seu manifest.json
    public static ModManifest inspect(File bmodFile) throws IOException {
        if (bmodFile == null || !bmodFile.exists()) {
            throw new IOException("Arquivo .bmod nao encontrado");
        }

        try (ZipFile zip = new ZipFile(bmodFile)) {
            ZipEntry manifestEntry = zip.getEntry("manifest.json");
            if (manifestEntry == null) {
                throw new IOException("Arquivo .bmod invalido: manifest.json ausente");
            }

            try (InputStream is = zip.getInputStream(manifestEntry)) {
                String json = readStreamToString(is);
                return ManifestParser.parse(json);
            }
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
                    "Incompatibilidade: Este mod foi feito para '" + manifest.game + "', mas o jogo selecionado e '" + targetPkg + "'.",
                    manifest);
        }

        if (detectedEngine != null && !manifest.matchesEngine(detectedEngine)) {
            return new InstallResult(false,
                    "Incompatibilidade de Engine: Mod requer '" + manifest.engine + "', mas o jogo usa '" + detectedEngine + "'.",
                    manifest);
        }

        if (!SuHelper.isRootAvailable()) {
            return new InstallResult(false, "Permissao root nao disponivel. Impossivel instalar o mod.", manifest);
        }

        File tmpDir = new File(bmodFile.getParentFile(), "tmp_bmod_" + manifest.id);
        tmpDir.mkdirs();

        try (ZipFile zip = new ZipFile(bmodFile)) {
            // 1. Extrair manifest.json para <id>.json
            File jsonFile = new File(tmpDir, manifest.id + ".json");
            ZipEntry manifestEntry = zip.getEntry("manifest.json");
            try (InputStream is = zip.getInputStream(manifestEntry);
                 FileOutputStream fos = new FileOutputStream(jsonFile)) {
                copyStream(is, fos);
            }

            // 2. Extrair payload (.so ou .patch)
            File payloadFile;
            if ("native".equals(manifest.type)) {
                ZipEntry soEntry = zip.getEntry("mod.so");
                if (soEntry == null) {
                    return new InstallResult(false, "Arquivo mod.so ausente no pacote .bmod", manifest);
                }
                payloadFile = new File(tmpDir, manifest.id + ".so");
                try (InputStream is = zip.getInputStream(soEntry);
                     FileOutputStream fos = new FileOutputStream(payloadFile)) {
                    copyStream(is, fos);
                }
            } else {
                ZipEntry patchEntry = zip.getEntry("mod.patch");
                if (patchEntry == null) {
                    return new InstallResult(false, "Arquivo mod.patch ausente no pacote .bmod", manifest);
                }
                payloadFile = new File(tmpDir, manifest.id + ".patch");
                try (InputStream is = zip.getInputStream(patchEntry);
                     FileOutputStream fos = new FileOutputStream(payloadFile)) {
                    copyStream(is, fos);
                }
            }

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
            String destDir = "/data/local/tmp/mods/" + targetPkg + "/";
            SuHelper.ensureModDir(targetPkg);

            SuHelper.copyFile(jsonFile.getAbsolutePath(), destDir + jsonFile.getName(), "644");
            SuHelper.copyFile(payloadFile.getAbsolutePath(), destDir + payloadFile.getName(), "644");
            if (confFile != null && confFile.exists()) {
                SuHelper.copyFile(confFile.getAbsolutePath(), destDir + confFile.getName(), "644");
            }

            return new InstallResult(true, "Mod '" + manifest.name + "' instalado com sucesso!", manifest);
        } catch (Exception e) {
            return new InstallResult(false, "Falha na instalacao: " + e.getMessage(), manifest);
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

            // mod.patch ou mod.so
            String entryName = "native".equals(manifest.type) ? "mod.so" : "mod.patch";
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

    private static String readStreamToString(InputStream is) throws IOException {
        ByteArrayOutputStream baos = new ByteArrayOutputStream();
        copyStream(is, baos);
        return new String(baos.toByteArray(), StandardCharsets.UTF_8);
    }

    private static void copyStream(InputStream in, java.io.OutputStream out) throws IOException {
        byte[] buf = new byte[4096];
        int n;
        while ((n = in.read(buf)) != -1) {
            out.write(buf, 0, n);
        }
    }
}
