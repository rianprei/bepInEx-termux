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

    /**
     * Nome do arquivo principal deste mod no aparelho. Vem do MESMO par que a
     * tela de jogo usa para ler o nome de volta (ModInventory.modFileName), e
     * não de literais de extensão aqui: as duas metades do round-trip não podem
     * divergir, e é exatamente isso que quebrou antes (a tela montava
     * "t1..bpatch" e não achava o arquivo).
     */
    public String getMainFilename() {
        return io.github.rianprei.bepinex.manager.core.ModInventory
                .modFileName(id, type, isEnabled);
    }
}
