package io.github.rianprei.bepinex.manager.core;

import io.github.rianprei.bepinex.manager.model.ModManifest;

public final class SelectedFileRouter {
    public enum Action {
        REJECT,
        SELECT_GAME,
        INSTALL_DECLARED_GAME
    }

    public enum Installer {
        NONE,
        LOOSE_MOD_INSTALLER,
        BMOD_INSTALLER
    }

    public static final class Decision {
        public final Action action;
        public final Installer installer;
        public final ModContentDetector.Kind kind;
        public final String message;
        public final String packageName;

        private Decision(Action action, Installer installer, ModContentDetector.Kind kind, String message,
                         String packageName) {
            this.action = action;
            this.installer = installer;
            this.kind = kind;
            this.message = message;
            this.packageName = packageName;
        }
    }

    private SelectedFileRouter() {}

    public static Decision decide(ModContentDetector.Detection detection, ModManifest manifest) {
        if (detection == null) {
            return new Decision(Action.REJECT, Installer.NONE, null,
                    "Não foi possível identificar o arquivo.", null);
        }
        if (!detection.installable) {
            return new Decision(Action.REJECT, Installer.NONE, detection.kind, detection.reason, null);
        }
        if (detection.kind == ModContentDetector.Kind.BMOD) {
            if (manifest == null || manifest.game == null || manifest.game.isEmpty()) {
                return new Decision(Action.REJECT, Installer.NONE, detection.kind,
                        "O pacote .bmod não informa para qual jogo foi criado.", null);
            }
            if (!manifest.isUniversalGame()) {
                return new Decision(Action.INSTALL_DECLARED_GAME, Installer.LOOSE_MOD_INSTALLER,
                        detection.kind, "",
                        manifest.game);
            }
        }
        return new Decision(Action.SELECT_GAME, Installer.LOOSE_MOD_INSTALLER, detection.kind, "", null);
    }
}
