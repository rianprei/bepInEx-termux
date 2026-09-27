package io.github.rianprei.bepinex.manager.core;

import java.util.ArrayList;
import java.util.Collection;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * Inventário de mods de TODOS os apps instalados, com UMA chamada root.
 *
 * Por que isso existe (achado de device, 2026-09-27): a tela de jogos fazia
 * {@code SuHelper.listFiles("/data/local/tmp/mods/" + pkg)} dentro do laço por
 * app instalado. Cada listFiles abre um processo {@code su} novo, e um celular
 * com centenas de apps esgotava a memória — o Manager travou o aparelho do
 * usuário. A contagem de chamadas root na tela inicial não pode depender do
 * número de apps: é 1, sempre.
 *
 * O trabalho acontece DENTRO do shell do device (um {@code for} sobre
 * /data/local/tmp/mods/*), e o resultado volta como texto delimitado. O
 * parse é puro, então o teste de host conta as chamadas com um fake.
 */
public final class ModInventory {

    /** Uma chamada root. Devolve a saída crua (ou null em falha). */
    public interface RootCall {
        String exec();
    }

    private static final String BEGIN = "bepinex-mods-begin";
    private static final String END = "bepinex-mods-end";

    public static final class Counts {
        public final int total;    // .so/.patch, ligadas ou desligadas
        public final int active;   // ligadas (sem o sufixo .off)

        Counts(int total, int active) {
            this.total = total;
            this.active = active;
        }
    }

    /**
     * Comando único: um laço dentro do shell do device. A raiz é parâmetro
     * para o teste de host rodar o comando DE VERDADE (sh) contra uma árvore
     * temporária; o Manager usa o default.
     *
     * Sintaxe POSIX sh, e o teste de shell real roda `sh -n` e executa: a
     * primeira versão tinha um `;` solto depois do `echo` e virava `;;done`,
     * que o sh rejeita — no device isso significa inventário SEMPRE vazio e
     * todo jogo com 0 mods, sem erro nenhum na tela.
     */
    public static String command() {
        return command("/data/local/tmp/mods");
    }

    public static String command(String modsRoot) {
        StringBuilder sb = new StringBuilder();
        sb.append("echo '").append(BEGIN).append("'; ");
        sb.append("for d in ").append(modsRoot).append("/*/; do ");
        sb.append("[ -d \"$d\" ] || continue; ");
        sb.append("p=${d%/}; p=${p##*/}; t=0; a=0; ");
        // Quatro globs explícitos em vez de "$d"* : nome com espaço continua
        // inteiro (o diretório está entre aspas e o globo é o sufixo), e
        // pasta vazia não vira o literal do padrão.
        sb.append("for f in \"$d\"*.so \"$d\"*.patch \"$d\"*.so.off \"$d\"*.patch.off; do ");
        sb.append("[ -f \"$f\" ] || continue; ");
        sb.append("case \"$f\" in *.off) t=$((t+1)) ;; *) t=$((t+1)); a=$((a+1)) ;; esac; ");
        sb.append("done; ");
        sb.append("echo \"$p $t $a\"; ");
        sb.append("done; ");
        sb.append("echo '").append(END).append("'");
        return sb.toString();
    }

    /**
     * Uma chamada root, o mapa de todos os apps que têm pasta de mods.
     * Devolve mapa vazio em falha (a tela mostra 0 mod, que é o que acontece
     * sem root mesmo).
     */
    public static Map<String, Counts> inventory(Collection<String> packageNames, RootCall call) {
        Map<String, Counts> out = new LinkedHashMap<>();
        if (call == null) return out;
        String raw = call.exec();
        Map<String, Counts> all = parse(raw);
        if (packageNames == null) return all;
        for (String pkg : packageNames) {
            if (pkg == null) continue;
            Counts c = all.get(pkg);
            if (c != null) out.put(pkg, c);
        }
        return out;
    }

    /**
     * "bepinex-mods-begin" / "<pkg> <total> <ativo>" por linha /
     * "bepinex-mods-end". Texto fora dos delimitadores é ignorado, então
     * banner do su, aviso do shell e utf-8 não viram pacote.
     */
    public static Map<String, Counts> parse(String raw) {
        Map<String, Counts> out = new LinkedHashMap<>();
        if (raw == null) return out;
        boolean inside = false;
        for (String line : raw.split("\\r?\\n")) {
            String t = line.trim();
            if (t.startsWith(BEGIN)) { inside = true; continue; }
            if (t.startsWith(END)) { inside = false; continue; }
            if (!inside || t.isEmpty()) continue;
            String[] parts = t.split("\\s+");
            if (parts.length != 3) continue;
            if (!pkgName(parts[0])) continue;
            try {
                int total = Integer.parseInt(parts[1]);
                int active = Integer.parseInt(parts[2]);
                // Contagem negativa ou absurda é linha corrompida/injetada, não
                // é o que o device conta (o teste pegou isso: "com..ruim -1 0"
                // entrava no mapa com total=-1).
                if (total < 0 || active < 0 || total > 10000 || active > total) continue;
                out.put(parts[0], new Counts(total, active));
            } catch (NumberFormatException ignored) {}
        }
        return out;
    }

    // Mesmo critério do SuHelper.requirePkg, sem lançar: o parse ignora o que
    // não for nome de pacote em vez de contaminar o mapa inteiro.
    private static boolean pkgName(String s) {
        if (s == null || s.isEmpty() || s.length() > 128) return false;
        if (!Character.isLetter(s.charAt(0))) return false;
        boolean dot = false;
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            if (c == '.') { dot = true; continue; }
            if (!(Character.isLetterOrDigit(c) || c == '_')) return false;
        }
        return dot;
    }

    /**
     * Conteúdo de vários arquivos numa chamada só: separador em linha inteira
     * ("@@@FILE:<nome>") seguido do conteúdo até o próximo separador. É o
     * mesmo truque do inventário, para a tela de jogo não abrir um `su` por
     * manifest.
     */
    public static Map<String, String> parseBundle(String raw, String sep) {
        Map<String, String> out = new LinkedHashMap<>();
        if (raw == null || sep == null) return out;
        String name = null;
        StringBuilder body = new StringBuilder();
        for (String line : raw.split("\\r?\\n")) {
            if (line.startsWith(sep)) {
                if (name != null) out.put(name, body.toString());
                name = line.substring(sep.length()).trim();
                body.setLength(0);
            } else if (name != null) {
                if (body.length() > 0) body.append('\n');
                body.append(line);
            }
        }
        if (name != null) out.put(name, body.toString());
        return out;
    }

    public static List<String> lines(String raw) {
        List<String> out = new ArrayList<>();
        if (raw == null) return out;
        for (String line : raw.split("\\r?\\n")) if (!line.trim().isEmpty()) out.add(line.trim());
        return out;
    }
}
