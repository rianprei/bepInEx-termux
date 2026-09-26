package io.github.rianprei.bepinex.manager.model;

// Informacoes de um mod instalado em /data/local/tmp/mods/<pkg>/ (Contrato C1).
public class ModInfo {
    public String id;
    public String name;
    public String version = "1.0";
    public String author = "";
    public String description = "";
    public String type = "patch"; // "patch" | "native"
    public boolean isEnabled = true;
    public boolean hasConf = false;
    public boolean hasOptions = false;
    public ModManifest manifest;

    public ModInfo(String id) {
        this.id = id;
        this.name = id;
    }

    public String getMainFilename() {
        String ext = "native".equals(type) ? ".so" : ".patch";
        return id + ext + (isEnabled ? "" : ".off");
    }
}
