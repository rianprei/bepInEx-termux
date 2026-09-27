package io.github.rianprei.bepinex.manager;

import android.app.Activity;
import android.app.AlertDialog;
import android.os.Handler;
import android.os.Looper;

import io.github.rianprei.bepinex.manager.core.DownloadFilePicker;
import io.github.rianprei.bepinex.manager.core.SelectedFileStager;
import io.github.rianprei.bepinex.manager.core.SuHelper;

import java.io.File;
import java.util.List;
import java.util.function.Consumer;

final class DownloadFileDialog {
    private static final Handler MAIN = new Handler(Looper.getMainLooper());

    private DownloadFileDialog() {}

    static void show(Activity activity, Consumer<File> onSelected) {
        new Thread(() -> {
            DownloadFilePicker.Listing listing = SuHelper.listDownloadFiles();
            MAIN.post(() -> {
                if (activity.isFinishing()) return;
                if (!listing.success()) {
                    showMessage(activity, "Não foi possível listar os arquivos", listing.error);
                    return;
                }
                if (listing.paths.isEmpty()) {
                    showMessage(activity, "Nenhum arquivo encontrado",
                            "Não há arquivos em Download ou Documents.");
                    return;
                }

                List<String> paths = listing.paths;
                String[] labels = new String[paths.size()];
                for (int i = 0; i < paths.size(); i++) {
                    labels[i] = new File(paths.get(i)).getParentFile().getName()
                            + "/" + new File(paths.get(i)).getName();
                }
                new AlertDialog.Builder(activity)
                        .setTitle("Escolher da pasta Download")
                        .setItems(labels, (dialog, which) -> stageFile(activity, paths.get(which), onSelected))
                        .setNegativeButton("Cancelar", null)
                        .show();
            });
        }).start();
    }

    private static void stageFile(Activity activity, String sourcePath, Consumer<File> onSelected) {
        new Thread(() -> {
            File staged = null;
            String error = null;
            try {
                staged = SelectedFileStager.create(activity.getCacheDir(),
                        new File(sourcePath).getName());
                if (!SuHelper.copyDownloadFileToCache(sourcePath, staged.getAbsolutePath())) {
                    error = "Não foi possível copiar o arquivo escolhido para importação.";
                }
            } catch (Exception e) {
                error = "Não foi possível preparar o arquivo: " + e.getMessage();
            }

            File resultFile = staged;
            String resultError = error;
            MAIN.post(() -> {
                if (activity.isFinishing()) {
                    SelectedFileStager.delete(resultFile);
                    return;
                }
                if (resultError != null) {
                    SelectedFileStager.delete(resultFile);
                    showMessage(activity, "Falha ao abrir arquivo", resultError);
                    return;
                }
                onSelected.accept(resultFile);
            });
        }).start();
    }

    private static void showMessage(Activity activity, String title, String message) {
        new AlertDialog.Builder(activity)
                .setTitle(title)
                .setMessage(message)
                .setPositiveButton("OK", null)
                .show();
    }
}
