package io.github.rianprei.bepinex.manager.core;

// Verificador de integridade do ambiente (Root, Modulo Magisk/KSU, Zygisk).
public final class StatusChecker {
    public static final class SystemStatus {
        public boolean rootOk = false;
        public String rootInfo = "Não concedido";
        public boolean moduleInstalled = false;
        public boolean moduleActive = false;
        public String moduleInfo = "Não instalado";
        public boolean zygiskActive = false;
        public String zygiskInfo = "Desconhecido";
        // Versao do APK: vem do VERSION da raiz do repo (BuildVersion e
        // gerado pelo build.sh). Numero solto aqui mentia sobre a versao.
        public String appVersion = BuildVersion.NAME + " (" + BuildVersion.CODE + ")";
    }

    private StatusChecker() {}

    public static SystemStatus check() {
        SystemStatus status = new SystemStatus();

        // 1. Root
        SuHelper.Result rootRes = SuHelper.exec("id");
        if (rootRes.success && (rootRes.stdout.contains("uid=0") || rootRes.stdout.contains("root"))) {
            status.rootOk = true;
            status.rootInfo = "Ativo (uid=0)";
        } else {
            status.rootOk = false;
            status.rootInfo = (rootRes.friendlyError != null) ? rootRes.friendlyError : "Sem permissão root";
            return status;
        }

        // 2. Modulo Magisk / KernelSU
        SuHelper.Result modRes = SuHelper.exec("ls -d /data/adb/modules/*bepinex* 2>/dev/null || ls -d /data/adb/modules/* 2>/dev/null");
        if (modRes.success && !modRes.stdout.isEmpty()) {
            String[] lines = modRes.stdout.split("\\r?\\n");
            String bepinexModuleDir = null;
            for (String l : lines) {
                if (l.toLowerCase().contains("bepinex")) {
                    bepinexModuleDir = l.trim();
                    break;
                }
            }

            if (bepinexModuleDir != null) {
                status.moduleInstalled = true;
                // O caminho veio de um `ls` do device: entra no comando abaixo
                // so depois da validacao central (o device e root, mas a lista
                // pode ter sido adulterada por outro app).
                SuHelper.Result disRes;
                try {
                    SuHelper.requirePath(bepinexModuleDir, "module dir");
                    disRes = SuHelper.exec("[ -f '" + bepinexModuleDir + "/disable' ] && echo 'disabled' || echo 'enabled'");
                } catch (IllegalArgumentException e) {
                    disRes = SuHelper.exec("echo invalid");
                }
                if (disRes.success && "disabled".equals(disRes.stdout.trim())) {
                    status.moduleActive = false;
                    status.moduleInfo = "Instalado (Desativado no Magisk)";
                } else {
                    status.moduleActive = true;
                    status.moduleInfo = "Ativo (" + bepinexModuleDir.substring(bepinexModuleDir.lastIndexOf('/') + 1) + ")";
                }
            } else {
                status.moduleInstalled = false;
                status.moduleInfo = "Módulo bepInEx ausente em /data/adb/modules/";
            }
        } else {
            status.moduleInfo = "Nenhum módulo encontrado em /data/adb/modules/";
        }

        // 3. Zygisk
        SuHelper.Result zygiskCheck = SuHelper.exec(
                "if [ -d /data/adb/zygisk ] || [ -d /data/adb/modules/*zygisk* ] || getprop ro.zygisk 2>/dev/null | grep -q 1; then echo 'zygisk_ok'; else echo 'zygisk_maybe'; fi"
        );
        if (zygiskCheck.success && zygiskCheck.stdout.contains("zygisk_ok")) {
            status.zygiskActive = true;
            status.zygiskInfo = "Ativo (Zygisk / ZygiskNext)";
        } else {
            SuHelper.Result magiskVer = SuHelper.exec("magisk -v 2>/dev/null");
            if (magiskVer.success && !magiskVer.stdout.isEmpty()) {
                status.zygiskInfo = "Magisk instalado (" + magiskVer.stdout + ")";
            } else {
                status.zygiskInfo = "Zygisk não confirmado";
            }
        }

        return status;
    }
}
