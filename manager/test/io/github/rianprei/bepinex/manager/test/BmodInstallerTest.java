package io.github.rianprei.bepinex.manager.test;

import io.github.rianprei.bepinex.manager.core.BmodInstaller;
import io.github.rianprei.bepinex.manager.core.ManifestParser;
import io.github.rianprei.bepinex.manager.model.ModManifest;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.file.Files;
import java.util.zip.CRC32;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;
import java.util.zip.ZipOutputStream;

// .bmod e entrada HOSTIL: e um zip que veio de fora. O teste monta o pior
// caso (zip bomb com tamanho declarado falso) e exige que o codigo barra
// contando BYTES REAIS, e nao getSize().
public class BmodInstallerTest {
    public static void run() {
        try {
            testManifestBombRecusado();
            testPayloadBombRecusado();
            testZipBombEhBombReal();
            testExcessoDeEntradasRecusado();
            testBmodLegitimoExtrai();
            System.out.println("  [OK] BmodInstallerTest (zip bomb)");
        } catch (IOException e) {
            throw new AssertionError("falha de I/O montando o .bmod de teste: " + e);
        }
    }

    private static void check(String what, boolean cond) {
        if (!cond) throw new AssertionError("falhou: " + what);
    }

    private static File tmpDir() throws IOException {
        File dir = Files.createTempDirectory("bmod_test_").toFile();
        dir.deleteOnExit();
        return dir;
    }

    private static String manifestJson(String type) {
        return "{\"format\":1,\"id\":\"meu\",\"name\":\"Meu Mod\",\"engine\":\"unity-il2cpp\","
                + "\"type\":\"" + type + "\",\"game\":\"com.foo\"}";
    }

    /** Zip de mod .so legitimo: manifest + 2KB de "codigo". */
    private static File legitBmod(String type, String entryName, int payloadBytes) throws IOException {
        File f = new File(tmpDir(), "legit.bmod");
        try (ZipOutputStream z = new ZipOutputStream(new FileOutputStream(f))) {
            z.putNextEntry(new ZipEntry("manifest.json"));
            z.write(manifestJson(type).getBytes("UTF-8"));
            z.closeEntry();
            z.putNextEntry(new ZipEntry(entryName));
            z.write(new byte[payloadBytes]);
            z.closeEntry();
        }
        return f;
    }

    /** Zip bomb: 1MB de zeros numa entrada que DECLARA 16 bytes. */
    private static File bombBmod(int declaredSize, int realBytes) throws IOException {
        File f = new File(tmpDir(), "bomb.bmod");
        try (ZipOutputStream z = new ZipOutputStream(new FileOutputStream(f))) {
            z.putNextEntry(new ZipEntry("manifest.json"));
            z.write(manifestJson("native").getBytes("UTF-8"));
            z.closeEntry();
            z.putNextEntry(new ZipEntry("mod.so"));
            z.write(new byte[realBytes]);
            z.closeEntry();
        }
        rewriteDeclaredSize(f, "mod.so", declaredSize);
        return f;
    }

    // Reescreve o campo "tamanho descomprimido" dos cabecalhos local e
    // central, sem mexer no tamanho comprimido nem no CRC. E o que o
    // atacante controla: o ZipEntry.getSize() passa a mentir, mas o stream
    // continua entregando tudo.
    private static void rewriteDeclaredSize(File zip, String entryName, int declared) throws IOException {
        byte[] raw = Files.readAllBytes(zip.toPath());
        byte[] payload = readEntry(zip, entryName);
        CRC32 crc = new CRC32();
        crc.update(payload);
        int crcVal = (int) crc.getValue();
        boolean touchedLocal = false, touchedCen = false;
        for (int i = 0; i + 4 <= raw.length; i++) {
            long sig = (raw[i] & 0xFFL) | ((raw[i + 1] & 0xFFL) << 8)
                    | ((raw[i + 2] & 0xFFL) << 16) | ((raw[i + 3] & 0xFFL) << 24);
            if (sig == 0x04034b50L) {           // local file header: crc +14, usize +22
                put32(raw, i + 14, crcVal);
                put32(raw, i + 22, declared);
                touchedLocal = true;
            } else if (sig == 0x02014b50L) {    // central directory: crc +16, usize +24
                put32(raw, i + 16, crcVal);
                put32(raw, i + 24, declared);
                touchedCen = true;
            }
        }
        check("o zipbomb tem cabecalho local", touchedLocal);
        check("o zipbomb tem diretorio central", touchedCen);
        Files.write(zip.toPath(), raw);
    }

    private static byte[] readEntry(File zip, String entryName) throws IOException {
        try (ZipFile zf = new ZipFile(zip)) {
            ZipEntry e = zf.getEntry(entryName);
            if (e == null) return new byte[0];
            try (java.io.InputStream is = zf.getInputStream(e)) {
                ByteArrayOutputStream baos = new ByteArrayOutputStream();
                byte[] buf = new byte[8192];
                int n;
                while ((n = is.read(buf)) != -1) baos.write(buf, 0, n);
                return baos.toByteArray();
            }
        }
    }

    private static void put32(byte[] b, int off, int v) {
        b[off] = (byte) v;
        b[off + 1] = (byte) (v >> 8);
        b[off + 2] = (byte) (v >> 16);
        b[off + 3] = (byte) (v >> 24);
    }

    private static void testManifestBombRecusado() throws IOException {
        // manifest.json de 200KB (o teto e 64KB): declared size bate, mas o
        // limite tem de existir porque oManifestParser nao le 200KB de JSON.
        File f = new File(tmpDir(), "manifest_bomb.bmod");
        try (ZipOutputStream z = new ZipOutputStream(new FileOutputStream(f))) {
            z.putNextEntry(new ZipEntry("manifest.json"));
            z.write(new byte[200 * 1024]);
            z.closeEntry();
            z.putNextEntry(new ZipEntry("mod.so"));
            z.write(new byte[16]);
            z.closeEntry();
        }
        try {
            BmodInstaller.inspect(f);
            throw new AssertionError("manifest de 200KB deveria ter sido recusado");
        } catch (BmodInstaller.LimitExceededException e) {
            check("o limite nomeia o manifest", "manifest.json".equals(e.what));
            check("o limite e 64KB", e.limit == 64L * 1024);
            check("a mensagem diz que abortou", e.getMessage().contains("abortada"));
        }
    }

    private static void testPayloadBombRecusado() throws IOException {
        // Nenhuma checagem de constante aqui de proposito: se o teste so
        // repetisse "MAX_PAYLOAD_BYTES == 64MB", a sabotagem de mexer no
        // numero passaria. O que tem de provar e COMPORTAMENTO: o bomb entra
        // e o codigo recusa, com o teto no motivo da recusa.

        // (1) O caso do briefing: 1MB de zeros numa entrada que DECLARA 16
        // bytes, num .bpatch (teto 256KB). Barato e e o ataque classico.
        File patchBomb = new File(tmpDir(), "patch_bomb.bmod");
        try (ZipOutputStream z = new ZipOutputStream(new FileOutputStream(patchBomb))) {
            z.putNextEntry(new ZipEntry("manifest.json"));
            z.write(manifestJson("patch").getBytes("UTF-8"));
            z.closeEntry();
            z.putNextEntry(new ZipEntry("mod.bpatch"));
            z.write(new byte[1024 * 1024]);
            z.closeEntry();
        }
        rewriteDeclaredSize(patchBomb, "mod.bpatch", 16);
        checkZipBombPremise(patchBomb, "mod.bpatch", 16, 1024 * 1024);

        ModManifest mp = ManifestParser.parse(manifestJson("patch"));
        File outPatch = new File(tmpDir(), "extract_patch_bomb");
        try {
            BmodInstaller.extractBmod(patchBomb, mp, outPatch);
            throw new AssertionError("payload .bpatch de 1MB (declarando 16) deveria ter sido recusado");
        } catch (BmodInstaller.LimitExceededException e) {
            check("o limite nomeia o payload", "mod.bpatch".equals(e.what));
            check("o teto do patch e 256KB", e.limit == 256L * 1024);
            check("a mensagem acusa a mentira do tamanho",
                    e.getMessage().contains("declara") && e.getMessage().contains("abortada"));
        }
        check("parcial .bpatch apagado", !new File(outPatch, "meu.bpatch").exists());
        check("parcial .json apagado", !new File(outPatch, "meu.json").exists());

        // (2) O teto real do .so (64MB) tambem barra: 65MB de zeros
        // declarando 16. Sai de um zip de ~64KB porque zeros deflatam para
        // quase nada — que e justamente o formato do zip bomb.
        long big = 65L * 1024 * 1024;
        File soBomb = new File(tmpDir(), "so_bomb.bmod");
        try (ZipOutputStream z = new ZipOutputStream(new FileOutputStream(soBomb))) {
            z.putNextEntry(new ZipEntry("manifest.json"));
            z.write(manifestJson("native").getBytes("UTF-8"));
            z.closeEntry();
            z.putNextEntry(new ZipEntry("mod.so"));
            byte[] chunk = new byte[1024 * 1024];
            for (long written = 0; written < big; written += chunk.length) z.write(chunk);
            z.closeEntry();
        }
        rewriteDeclaredSize(soBomb, "mod.so", 16);
        checkZipBombPremise(soBomb, "mod.so", 16, (int) big);

        ModManifest mn = ManifestParser.parse(manifestJson("native"));
        File outSo = new File(tmpDir(), "extract_so_bomb");
        try {
            BmodInstaller.extractBmod(soBomb, mn, outSo);
            throw new AssertionError("payload .so de 65MB (declarando 16) deveria ter sido recusado");
        } catch (BmodInstaller.LimitExceededException e) {
            check("o limite nomeia o mod.so", "mod.so".equals(e.what));
            check("o teto do .so e 64MB", e.limit == 64L * 1024 * 1024);
            check("conta bytes reais, nao getSize()", e.real > 64L * 1024 * 1024);
        }
        check("parcial .so apagado", !new File(outSo, "meu.so").exists());

        // (3) E o contrario tambem vale: .so de 1MB e mod pequeno LEGITIMO.
        // Se o limite fosse apertado demais, o Manager recusaria mod bom.
        File small = legitBmod("native", "mod.so", 1024 * 1024);
        File outSmall = new File(tmpDir(), "extract_small_ok");
        File payload = BmodInstaller.extractBmod(small, mn, outSmall);
        check(".so de 1MB instala", payload.exists() && payload.length() == 1024 * 1024);
    }

    // A premissa do ataque: getSize() mente, o stream entrega o tamanho real.
    private static void checkZipBombPremise(File zip, String entryName, int declared, int real) throws IOException {
        try (ZipFile zf = new ZipFile(zip)) {
            check(entryName + ": getSize() declara " + declared,
                    zf.getEntry(entryName).getSize() == declared);
            check(entryName + ": o stream entrega " + real,
                    readEntry(zip, entryName).length == real);
        }
    }

    private static void testZipBombEhBombReal() throws IOException {
        // Sem o limite, 1MB de zeros de um zip de ~1.3KB: a razao e o que
        // importa. Um .bmod de 1,3KB que entrega 1MB de RAM; com 1000
        // entradas iguais, 1 GB sem ocupar disco.
        File bomb = bombBmod(16, 1024 * 1024);
        check("zip de 1MB cabe em menos de 16KB de arquivo", bomb.length() < 16 * 1024);
    }

    private static void testExcessoDeEntradasRecusado() throws IOException {
        File f = new File(tmpDir(), "many.bmod");
        try (ZipOutputStream z = new ZipOutputStream(new FileOutputStream(f))) {
            z.putNextEntry(new ZipEntry("manifest.json"));
            z.write(manifestJson("patch").getBytes("UTF-8"));
            z.closeEntry();
            for (int i = 0; i < BmodInstaller.MAX_ENTRIES + 8; i++) {
                z.putNextEntry(new ZipEntry("lixo" + i + ".bin"));
                z.write(new byte[8]);
                z.closeEntry();
            }
        }
        try {
            BmodInstaller.inspect(f);
            throw new AssertionError("zip com muitas entradas deveria ter sido recusado");
        } catch (IOException e) {
            check("a recusa fala de entradas", e.getMessage().contains("entradas"));
        }
    }

    private static void testBmodLegitimoExtrai() throws IOException {
        File f = legitBmod("patch", "mod.bpatch", 2048);
        ModManifest m = ManifestParser.parse(manifestJson("patch"));
        File out = new File(tmpDir(), "extract_ok");
        File payload = BmodInstaller.extractBmod(f, m, out);
        check("payload extraido", payload.exists() && payload.length() == 2048);
        check("nome do payload", payload.getName().equals("meu.bpatch"));
        check("manifest extraido", new File(out, "meu.json").exists());
    }
}
