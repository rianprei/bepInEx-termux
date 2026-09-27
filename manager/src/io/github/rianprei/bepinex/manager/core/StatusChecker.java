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

    /**
     * Uma chamada root para o status inteiro. Antes eram ~5 `su` (id, ls dos
     * módulos, disable, zygisk, magisk -v) por carregamento de tela: cada
     * `su` é um processo, e o padrão do projeto é "nada de chamada root
     * repetida" (o travamento de 2026-09-27 foi exatamente isso, em escala
     * maior). O formato é KEY=VALUE por linha.
     */
    public static final String PROBE_COMMAND =
            "echo B=bepinex-probe-begin;"
            + "echo uid=$(id -u 2>/dev/null);"
            + "m=$(ls -d /data/adb/modules/*bepinex* 2>/dev/null | head -n 1);"
            + "echo module=$m;"
            + "echo disable=$([ -f \"$m/disable\" ] && echo disabled || echo enabled);"
            + "echo zygisk=$(su -c 'test -d /data/adb/modules/zygisk' && echo yes || echo no);"
            + "echo magisk=$(magisk -v 2>/dev/null | head -n 1);"
            + "echo B=bepinex-probe-end";

    public static SystemStatus check() {
        return probe(new RootCall() {
            @Override public String exec() { return SuHelper.exec(PROBE_COMMAND).stdout; }
        });
    }

    /** Uma chamada root; o parse é puro e testável sem root. */
    public static SystemStatus probe(RootCall call) {
        SystemStatus status = new SystemStatus();
        String raw = (call != null) ? call.exec() : null;
        return parse(raw, status);
    }

    public interface RootCall {
        String exec();
    }

    static SystemStatus parse(String raw, SystemStatus status) {
        if (raw == null) return status;
        for (String line : raw.split("\\r?\\n")) {
            String t = line.trim();
            int eq = t.indexOf('=');
            if (eq <= 0) continue;
            String k = t.substring(0, eq).trim();
            String v = t.substring(eq + 1).trim();
            if ("uid".equals(k)) {
                status.rootOk = "0".equals(v);
                status.rootInfo = status.rootOk ? "Ativo (uid=0)" : "Sem permissão root";
            } else if ("module".equals(k)) {
                status.moduleInstalled = !v.isEmpty();
                status.moduleActive = status.moduleInstalled;
                if (status.moduleInstalled) {
                    String name = v.substring(v.lastIndexOf('/') + 1);
                    status.moduleInfo = "Instalado (" + name + ")";
                } else {
                    status.moduleInfo = "Módulo bepInEx ausente em /data/adb/modules/";
                }
            } else if ("disable".equals(k)) {
                if ("disabled".equals(v)) {
                    status.moduleActive = false;
                    status.moduleInfo = "Instalado (Desativado no Magisk)";
                }
            } else if ("zygisk".equals(k)) {
                status.zygiskActive = "yes".equals(v);
                status.zygiskInfo = status.zygiskActive ? "Ativo (Zygisk / ZygiskNext)" : "Desconhecido";
            } else if ("magisk".equals(k)) {
                // version informational; o card mostra root/zygisk
            }
        }
        if (status.moduleInstalled && status.moduleActive) {
            // moduleInfo já tem o nome do módulo; nada a acrescentar
        }
        return status;
    }
}
