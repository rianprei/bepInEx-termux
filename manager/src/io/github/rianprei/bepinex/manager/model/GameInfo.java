package io.github.rianprei.bepinex.manager.model;

import io.github.rianprei.bepinex.manager.core.EngineDetector;

// Informacoes de um app/jogo instalado.
public class GameInfo {
    public String packageName;
    public String appName;
    public String engine = EngineDetector.ENGINE_JAVA;
    public int installedModsCount = 0;
    public int activeModsCount = 0;
    public boolean isGame = false;

    public GameInfo() {}

    public GameInfo(String packageName, String appName, String engine) {
        this.packageName = packageName;
        this.appName = appName;
        this.engine = engine;
    }
}
