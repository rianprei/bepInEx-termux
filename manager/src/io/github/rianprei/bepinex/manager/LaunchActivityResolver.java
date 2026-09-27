package io.github.rianprei.bepinex.manager;

import android.content.ComponentName;
import android.content.Intent;
import android.content.pm.PackageManager;

import io.github.rianprei.bepinex.manager.core.SuHelper;

final class LaunchActivityResolver {
    static final String NOT_FOUND_MESSAGE =
            "O Android não encontrou uma tela de início válida para este aplicativo.";

    private LaunchActivityResolver() {}

    static String resolve(PackageManager packageManager, String packageName) {
        if (packageManager == null) return null;
        Intent launchIntent = packageManager.getLaunchIntentForPackage(packageName);
        if (launchIntent == null) return null;
        ComponentName component = launchIntent.getComponent();
        if (component == null) return null;
        try {
            return SuHelper.requireActivityComponent(packageName, component.flattenToShortString());
        } catch (IllegalArgumentException e) {
            return null;
        }
    }
}
