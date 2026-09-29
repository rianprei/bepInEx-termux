package io.github.rianprei.bepinex.manager.test;

import io.github.rianprei.bepinex.manager.core.SelectedFileStager;

import java.io.File;
import java.nio.file.Files;

public final class SelectedFileStagerTest {
    public static void run() {
        File cache;
        try {
            cache = Files.createTempDirectory("selected-file-stager-test-").toFile();
        } catch (Exception e) {
            throw new AssertionError("não foi possível criar cache temporário: " + e);
        }
        File staged = null;
        try {
            staged = SelectedFileStager.create(cache, "../../hello mod.so");
            check("nome original preservado sem caminho", "hello_mod.so".equals(staged.getName()));
            check("staging fica em diretório temporário dentro do cache",
                    staged.getParentFile().getParentFile().equals(cache));
            check("arquivo staged existe", staged.isFile());
            File directory = staged.getParentFile();
            SelectedFileStager.delete(staged);
            check("limpeza remove arquivo e diretório temporário",
                    !staged.exists() && !directory.exists());
            staged = null;
            System.out.println("  [OK] SelectedFileStagerTest (nome e limpeza)");
        } catch (Exception e) {
            throw new AssertionError("falha ao preparar arquivo de teste: " + e);
        } finally {
            SelectedFileStager.delete(staged);
            cache.delete();
        }
    }

    private static void check(String what, boolean condition) {
        TestRunner.check(what, condition);
    }
}
