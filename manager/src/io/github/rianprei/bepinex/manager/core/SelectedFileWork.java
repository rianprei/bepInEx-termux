package io.github.rianprei.bepinex.manager.core;

import io.github.rianprei.bepinex.manager.model.ModManifest;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.concurrent.Executor;

/** Executes selected-file inspection and root installation on a caller-provided worker. */
public final class SelectedFileWork {
    public record Inspection(ModManifest manifest, SelectedFileRouter.Decision decision) {}

    @FunctionalInterface
    public interface Callback<T> {
        void complete(T value, Exception error);
    }

    @FunctionalInterface
    public interface DisplayName {
        String get() throws Exception;
    }

    @FunctionalInterface
    public interface InputStreamProvider {
        InputStream open() throws IOException;
    }

    @FunctionalInterface
    private interface Operation<T> {
        T run() throws Exception;
    }

    private SelectedFileWork() {}

    public static void inspect(Executor executor, File file, Callback<Inspection> callback) {
        submit(executor, () -> {
            ModContentDetector.Detection detection = ModContentDetector.detect(
                    LooseModInstaller.probe(file), true);
            ModManifest manifest = detection.kind == ModContentDetector.Kind.BMOD
                    ? BmodInstaller.inspect(file) : null;
            return new Inspection(manifest, SelectedFileRouter.decide(detection, manifest));
        }, callback);
    }

    public static void stage(Executor executor, File cacheDirectory, DisplayName displayName,
                             InputStreamProvider inputProvider, Callback<File> callback) {
        submit(executor, () -> {
            File staged = SelectedFileStager.create(cacheDirectory, displayName.get());
            try (InputStream input = inputProvider.open();
                 FileOutputStream output = new FileOutputStream(staged)) {
                if (input == null) throw new IOException("O provedor não abriu o arquivo.");
                byte[] buffer = new byte[16 * 1024];
                int count;
                while ((count = input.read(buffer)) != -1) output.write(buffer, 0, count);
                return staged;
            } catch (Exception e) {
                SelectedFileStager.delete(staged);
                throw e;
            }
        }, callback);
    }

    public static void install(Executor executor, File file, String packageName, String engine,
                               Callback<LooseModInstaller.Result> callback) {
        install(executor, file, packageName, engine, null, callback);
    }

    public static void install(Executor executor, File file, String packageName, String engine,
                               String gameAbi, Callback<LooseModInstaller.Result> callback) {
        submit(executor, () -> LooseModInstaller.installFromFile(file, packageName, engine, gameAbi), callback);
    }

    private static <T> void submit(Executor executor, Operation<T> operation, Callback<T> callback) {
        if (executor == null || operation == null || callback == null) {
            throw new IllegalArgumentException("executor, operação e callback são obrigatórios");
        }
        executor.execute(() -> {
            try {
                callback.complete(operation.run(), null);
            } catch (Exception e) {
                callback.complete(null, e);
            }
        });
    }
}
