package io.github.rianprei.bepinex.manager.test;

import io.github.rianprei.bepinex.manager.core.LooseModInstaller;
import io.github.rianprei.bepinex.manager.core.SelectedFileWork;
import io.github.rianprei.bepinex.manager.core.SelectedFileStager;

import java.io.ByteArrayInputStream;
import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.Executor;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;

public final class SelectedFileWorkTest {
    private SelectedFileWorkTest() {}

    public static void run() throws Exception {
        File file = Files.createTempFile("selected-file-work-", ".bpatch").toFile();
        try {
            Files.writeString(file.toPath(), "return Example GetValue 0 int 1\n",
                    StandardCharsets.UTF_8);
            Executor workerExecutor = command -> new Thread(command, "test-file-worker").start();
            long uiThread = Thread.currentThread().getId();

            CountDownLatch probeFinished = new CountDownLatch(1);
            AtomicReference<Thread> probeCallbackThread = new AtomicReference<>();
            AtomicReference<Exception> probeError = new AtomicReference<>();
            AtomicReference<SelectedFileWork.Inspection> inspection = new AtomicReference<>();
            SelectedFileWork.inspect(workerExecutor, file, (value, error) -> {
                inspection.set(value);
                probeError.set(error);
                probeCallbackThread.set(Thread.currentThread());
                probeFinished.countDown();
            });
            check("probe concluído fora da thread chamadora",
                    probeFinished.await(5, TimeUnit.SECONDS)
                            && probeCallbackThread.get() != null
                            && probeCallbackThread.get().getId() != uiThread);
            check("probe executou e classificou arquivo selecionado",
                    probeError.get() == null && inspection.get() != null
                            && inspection.get().decision() != null);

            File missing = new File(file.getParentFile(), file.getName() + ".missing");
            CountDownLatch installFinished = new CountDownLatch(1);
            AtomicReference<Thread> installCallbackThread = new AtomicReference<>();
            AtomicReference<LooseModInstaller.Result> installResult = new AtomicReference<>();
            AtomicReference<Exception> installError = new AtomicReference<>();
            SelectedFileWork.install(workerExecutor, missing, "com.example.game", "unity-il2cpp",
                    (value, error) -> {
                        installResult.set(value);
                        installError.set(error);
                        installCallbackThread.set(Thread.currentThread());
                        installFinished.countDown();
                    });
            check("install concluído fora da thread chamadora",
                    installFinished.await(5, TimeUnit.SECONDS)
                            && installCallbackThread.get() != null
                            && installCallbackThread.get().getId() != uiThread);
            check("install executou e devolveu erro controlado",
                    installError.get() == null && installResult.get() != null
                            && !installResult.get().success);

            CountDownLatch stageFinished = new CountDownLatch(1);
            AtomicReference<Thread> stageCallbackThread = new AtomicReference<>();
            AtomicReference<File> stagedFile = new AtomicReference<>();
            AtomicReference<Exception> stageError = new AtomicReference<>();
            byte[] stageBytes = "conteudo grande do arquivo".getBytes(StandardCharsets.UTF_8);
            SelectedFileWork.stage(workerExecutor, file.getParentFile(), () -> "selected file.bin",
                    () -> new ByteArrayInputStream(stageBytes), (value, error) -> {
                        stagedFile.set(value);
                        stageError.set(error);
                        stageCallbackThread.set(Thread.currentThread());
                        stageFinished.countDown();
                    });
            check("cópia do URI concluída fora da thread chamadora",
                    stageFinished.await(5, TimeUnit.SECONDS)
                            && stageCallbackThread.get() != null
                            && stageCallbackThread.get().getId() != uiThread);
            check("cópia do URI foi concluída em cache",
                    stageError.get() == null && stagedFile.get() != null
                            && java.util.Arrays.equals(Files.readAllBytes(stagedFile.get().toPath()), stageBytes));
            SelectedFileStager.delete(stagedFile.get());
            System.out.println("  [OK] SelectedFileWorkTest (stage/probe/install fora da thread UI)");
        } finally {
            Files.deleteIfExists(file.toPath());
        }
    }

    private static void check(String what, boolean condition) {
        if (!condition) throw new AssertionError("falhou: " + what);
    }
}
