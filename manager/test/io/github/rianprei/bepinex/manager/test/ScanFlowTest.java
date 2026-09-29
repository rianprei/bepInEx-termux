package io.github.rianprei.bepinex.manager.test;

import io.github.rianprei.bepinex.manager.core.ScanFlow;

import java.io.ByteArrayInputStream;
import java.io.File;
import java.nio.charset.StandardCharsets;

public class ScanFlowTest {
    public static void run() throws Exception {
        testExtraiEConclui();
        testFalhaLimpaScanner();
        System.out.println("  [OK] ScanFlowTest (asset e limpeza finally)");
    }

    private static void testExtraiEConclui() throws Exception {
        File target = File.createTempFile("u_dump", ".so");
        FakeDevice device = new FakeDevice(false);
        ScanFlow.run(name -> new ByteArrayInputStream("ELF-test".getBytes(StandardCharsets.US_ASCII)),
                device, target);
        check("asset extraido", device.installed && device.dumpDeleted && device.restarted);
        check("scanner removido no sucesso", device.removed && !target.exists());
    }

    private static void testFalhaLimpaScanner() throws Exception {
        File target = File.createTempFile("u_dump", ".so");
        FakeDevice device = new FakeDevice(true);
        try {
            ScanFlow.run(name -> new ByteArrayInputStream(new byte[]{1, 2, 3}), device, target);
            throw new AssertionError("falha de install deveria propagar");
        } catch (Exception expected) {
            check("scanner removido no erro", device.removed);
            check("temporario removido no erro", !target.exists());
        }
    }

    private static void check(String what, boolean value) {
        TestRunner.check(what, value);
    }

    private static final class FakeDevice implements ScanFlow.Device {
        final boolean failInstall;
        boolean installed;
        boolean dumpDeleted;
        boolean restarted;
        boolean removed;

        FakeDevice(boolean failInstall) { this.failInstall = failInstall; }
        public boolean ensureModDir() { return true; }
        public boolean install(String local, String remote) {
            installed = true;
            return !failInstall;
        }
        public boolean deleteDump() { dumpDeleted = true; return true; }
        public boolean restartGame() { restarted = true; return true; }
        public boolean dumpReady() { return true; }
        public boolean removeScanner() { removed = true; return true; }
        public void sleep(long millis) {}
    }
}
