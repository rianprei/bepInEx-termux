package io.github.rianprei.bepinex.manager.model;

import java.util.ArrayList;
import java.util.List;

// Manifest do mod (Contrato C2).
public class ModManifest {
    public int format = 1;
    public String id;
    public String name;
    public String version;
    public String author;
    public String description;
    public String game;   // Pacote ou "*"
    public String engine; // unity-il2cpp | unity-mono | cocos2dx | native
    public String type;   // patch | native
    public List<ModOption> options = new ArrayList<>();

    public boolean isUniversalGame() {
        return "*".equals(game);
    }

    public boolean matchesGame(String targetPkg) {
        if (targetPkg == null) return false;
        return isUniversalGame() || targetPkg.equals(game);
    }

    public boolean matchesEngine(String detectedEngine) {
        if (engine == null || detectedEngine == null) return false;
        if ("*".equals(engine)) return true;
        return engine.equalsIgnoreCase(detectedEngine);
    }
}
