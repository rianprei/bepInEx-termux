package io.github.rianprei.bepinex.manager.core;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;

// Sequência pura do scanner: instala u_dump por uma execução, espera o dump e
// sempre remove o componente temporário, inclusive em falha/timeout.
public final class ScanFlow {
    public interface AssetSource {
        InputStream open(String name) throws IOException;
    }

    public interface Device {
        boolean ensureModDir();
        boolean install(String localPath, String remotePath);
        boolean deleteDump();
        boolean restartGame();
        boolean dumpReady();
        boolean removeScanner();
        void sleep(long millis) throws InterruptedException;
    }

    public static final long TIMEOUT_MS = 240_000L;
    public static final long POLL_MS = 1_000L;

    private ScanFlow() {}

    public static void run(AssetSource assets, Device device, File localFile) throws Exception {
        boolean attemptedInstall = false;
        try {
            extract(assets, "u_dump.so", localFile);
            attemptedInstall = true;
            if (!device.ensureModDir() || !device.install(localFile.getAbsolutePath(), "u_dump.so")) {
                throw new IOException("falha ao instalar u_dump.so");
            }
            if (!device.deleteDump()) throw new IOException("falha ao remover dump.tsv antigo");
            if (!device.restartGame()) throw new IOException("falha ao reiniciar o jogo");
            long deadline = System.currentTimeMillis() + TIMEOUT_MS;
            while (!device.dumpReady()) {
                if (System.currentTimeMillis() >= deadline) {
                    throw new IOException("timeout de 240s aguardando dump.tsv");
                }
                device.sleep(POLL_MS);
            }
        } finally {
            if (attemptedInstall) device.removeScanner();
            if (localFile.exists() && !localFile.delete()) localFile.deleteOnExit();
        }
    }

    static void extract(AssetSource assets, String name, File target) throws IOException {
        try (InputStream input = assets.open(name);
             FileOutputStream output = new FileOutputStream(target)) {
            byte[] buffer = new byte[8192];
            int count;
            while ((count = input.read(buffer)) != -1) output.write(buffer, 0, count);
        }
    }
}
