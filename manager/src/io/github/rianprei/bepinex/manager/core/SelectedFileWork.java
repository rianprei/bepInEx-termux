package io.github.rianprei.bepinex.manager.core;

import io.github.rianprei.bepinex.manager.model.ModManifest;

import java.io.File;
import java.util.concurrent.Executor;

/** Executes selected-file inspection and root installation on a caller-provided worker. */
public final class SelectedFileWork {
    public record Inspection(ModManifest manifest, SelectedFileRouter.Decision decision) {}

    @FunctionalInterface
    public interface Callback<T> {
        void complete(T value, Exception error);
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

    public static void install(Executor executor, File file, String packageName, String engine,
                               Callback<LooseModInstaller.Result> callback) {
        submit(executor, () -> LooseModInstaller.installFromFile(file, packageName, engine), callback);
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
