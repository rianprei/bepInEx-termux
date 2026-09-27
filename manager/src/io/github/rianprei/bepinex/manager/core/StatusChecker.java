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
        public String magiskVersion = "";
        // Versao do APK: vem do VERSION da raiz do repo (BuildVersion e
        // gerado pelo build.sh). Numero solto aqui mentia sobre a versao.
        public String appVersion = BuildVersion.NAME + " (" + BuildVersion.CODE + ")";
    }

    private StatusChecker() {}

    /**
     * Uma chamada root para o status inteiro. Antes eram ~5 `su` (id, ls dos
     * módulos, disable, zygisk, magisk -v) por carregamento de tela; cada `su`
     * é um processo, e o padrão do projeto é "nada de chamada root repetida".
     * Saída: marcadores + uma chave por linha.
     *
     * Raiz parametrizada para o teste de host rodar o comando em sh de verdade.
     * Sem `su` aninhado dentro do comando root (seria um processo su extra
     * dentro do orçamento de 1 chamada) e com as MESMAS três condições de
     * zygisk do original (3884e54): /data/adb/zygisk, /data/adb/modules/*zygisk*
     * e a propriedade ro.zygisk.
     */
    public static String command() {
        return command("/data/adb");
    }

    public static String command(String adbRoot) {
        return "echo B=bepinex-probe-begin; "
                + "echo \"uid=$(id -u 2>/dev/null)\"; "
                + "m=''; "
                + "for c in '" + adbRoot + "/modules/bc-poc' '" + adbRoot + "'/modules/*bepinex*; do "
                + "  if [ -d \"$c\" ]; then m=\"$c\"; break; fi; "
                + "done; "
                + "echo module=$m; "
                // O arquivo `disable` do Magisk é o que DESLIGA o módulo: sem
                // ele, o módulo está ativo. (A primeira versão tinha isso
                // invertido — só apareceu rodando o comando em sh de verdade.)
                + "echo disable=enabled; "
                + "if [ -n \"$m\" ] && [ -f \"$m/disable\" ]; then echo disable=disabled; fi; "
                + "z=no; "
                + "if [ -d '" + adbRoot + "/zygisk' ]; then z=yes; "
                + "else for d in '" + adbRoot + "'/modules/*zygisk*; do "
                + "  [ -d \"$d\" ] && { z=yes; break; }; "
                + "done; fi; "
                + "if [ \"$z\" != yes ] && getprop ro.zygisk 2>/dev/null | grep -q 1; then z=yes; fi; "
                + "echo zygisk=$z; "
                + "echo \"magisk=$(magisk -v 2>/dev/null | head -n 1)\"; "
                + "echo B=bepinex-probe-end";
    }

    public static final String PROBE_COMMAND = "";   // removido: use command()

    public static SystemStatus check() {
        return probe(new RootCall() {
            @Override public String exec() { return SuHelper.exec(command()).stdout; }
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

    /**
     * Parse das chaves. Respeita os marcadores begin/end: antes, qualquer
     * linha "uid=" ou "module=" que aparecesse fora deles (banner do su, saída
     * do magisk) era aceita como se fosse nossa.
     */
    public static SystemStatus parse(String raw, SystemStatus status) {
        if (raw == null) return status;
        boolean inside = false;
        for (String line : raw.split("\\r?\\n")) {
            String t = line.trim();
            if (t.equals("B=bepinex-probe-begin")) { inside = true; continue; }
            if (t.equals("B=bepinex-probe-end")) { inside = false; continue; }
            if (!inside) continue;
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
                    status.moduleInfo = "Instalado (" + v.substring(v.lastIndexOf('/') + 1) + ")";
                } else {
                    status.moduleInfo = "Módulo bepInEx ausente em " + v + "/";
                }
            } else if ("disable".equals(k)) {
                if ("disabled".equals(v)) {
                    status.moduleActive = false;
                    status.moduleInfo = "Instalado (Desativado no Magisk)";
                }
            } else if ("zygisk".equals(k)) {
                status.zygiskActive = "yes".equals(v);
            } else if ("magisk".equals(k)) {
                status.magiskVersion = v;
            }
        }
        if (!status.zygiskActive) {
            // Fallback do original (3884e54): zygisk sem o diretório/propriedade
            // ainda é Magisk instalado, e isso é o que o usuário lê no card.
            status.zygiskInfo = (!status.magiskVersion.isEmpty())
                    ? "Magisk instalado (" + status.magiskVersion + ")"
                    : "Zygisk não confirmado";
        } else {
            status.zygiskInfo = "Ativo (Zygisk / ZygiskNext)";
        }
        return status;
    }
}
