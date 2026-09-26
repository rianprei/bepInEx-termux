package io.github.rianprei.bepinex.manager.core;

import java.util.Arrays;
import java.util.HashSet;
import java.util.Locale;
import java.util.Set;

// Contrato C7 — o Manager aceita mod de QUALQUER origem: o tipo vem do
// CONTEUDO (magic/cabecalho), nunca da extensao, e a resposta e em
// portugues, honesta sobre rodar ou nao rodar.
//
// Por que conteudo e nao extensao: o usuario baixa o mod no WhatsApp, o
// arquivo chega como "mod (1).so", "sem nome" ou "download.bin", e o
// Manager precisa descobrir o que e. E o inverso tambem importa: uma
// extensao .so nao garante um .so (dll de Windows, .lua do GameGuardian).
//
// O que o jogo faz com cada tipo:
//   - ELF arm64  -> vira <id>.so em mods/<pkg>/ e o loader da dlopen (C1)
//   - texto C4   -> vira <id>.patch, lido pelo u_patch
//   - texto JS   -> vira <id>.js, lido pelo frida-gadget em modo script (F11)
//   - .dll       -> NAO roda agora: .NET IL2CPP e F12, .NET Mono e F13, e
//                   .dll Mono em jogo IL2CPP nao roda de jeito nenhum
//
// Nenhum caminho escreve no APK/OBB/arquivos do jogo: tudo em runtime.
public final class ModContentDetector {

    public enum Kind {
        BMOD,             // zip com manifest.json (C2)
        ZIP_PLAIN,        // zip sem manifest.json
        ELF_ARM64,        // ELF E_AARCH64 (183): mod nativo Android arm64
        ELF_OTHER_ARCH,   // ELF de outra arquitetura
        FRIDA_GADGET,     // frida-gadget: nunca entra como .so de mod
        PATCH,            // texto com regra do contrato C4
        FRIDA_JS,         // script Frida
        LUA_GG,           // script GameGuardian (F10, nao implementado)
        DOTNET_IL2CPP,    // .dll BepInEx 6 / MelonLoader IL2CPP (F12)
        DOTNET_MONO,      // .dll BepInEx 5 / MelonLoader Mono (F13)
        PE_NATIVE,        // .exe/.dll nativo do Windows (sem runtime .NET)
        MACHO,            // .dylib de iOS/macOS
        CHEAT_ENGINE,     // tabela do Cheat Engine
        BINARY_UNKNOWN,   // binario que nao e nada disso
        TEXT_OTHER        // texto que nao e .patch nem script
    }

    // Resultado da deteccao: o que e, se instala, e por que (sempre em PT-BR).
    public static final class Detection {
        public final Kind kind;
        public final String label;
        public final boolean installable;
        public final String reason;      // por que roda, ou por que nao roda
        public final String targetExt;   // ".so" / ".patch" / ".js", ou null
        public final String targetId;    // id do arquivo destino, ou null

        Detection(Kind kind, String label, boolean installable, String reason,
                  String targetExt, String targetId) {
            this.kind = kind;
            this.label = label;
            this.installable = installable;
            this.reason = reason;
            this.targetExt = targetExt;
            this.targetId = targetId;
        }
    }

    // Tudo que a detecco precisa saber sobre o arquivo. Quem le o arquivo
    // preenche (LooseModInstaller); a deteccao aqui e PURA e testavel no host.
    public static final class Sample {
        public final String fileName;
        public final byte[] head;          // primeiros bytes (20+ para ler o ELF)
        public final String text;          // conteudo como texto, null se binario
        public final boolean zipHasManifest;
        public final boolean fridaMarker;  // assinatura do gadget no conteudo

        public Sample(String fileName, byte[] head, String text,
                      boolean zipHasManifest, boolean fridaMarker) {
            this.fileName = (fileName != null) ? fileName : "";
            this.head = (head != null) ? head : new byte[0];
            this.text = text;
            this.zipHasManifest = zipHasManifest;
            this.fridaMarker = fridaMarker;
        }
    }

    // Componentes internos do bepInEx: esses nomes nao podem ser usados por
    // mod de terceiros (o Manager tambem esconde u_dump.so/u_patch.so na lista).
    private static final Set<String> RESERVED_IDS = new HashSet<>(Arrays.asList(
            "u_patch", "u_dump", "u_frida", "u_manager", "bepinex"));

    // Assinaturas de script Frida (C7): o texto tem que falar de Frida.
    private static final String[] JS_MARKERS = {
            "Interceptor", "Il2Cpp.perform", "Java.perform", "Module.findExportByName",
            "Process.getModuleByName", "NativeFunction", "Memory.read", "Memory.write"
    };

    // Marcas do frida-gadget dentro do proprio binario. A deteccao e por
    // nome OU por marca no conteudo: um .so do gadget renomeado para nao ter
    // "frida" no nome so e barrado se a marca estiver no binario.
    private static final String[] FRIDA_MARKERS = {
            "frida-gadget", "frida-agent", "frida_agent", "gum-js-loop", "GumScript"
    };

    private ModContentDetector() {}

    public static Detection detect(Sample s, boolean engineIl2cpp) {
        byte[] h = s.head;

        if (isZip(h)) {
            if (s.zipHasManifest) {
                return new Detection(Kind.BMOD, "pacote .bmod", true,
                        "Pacote .bmod (zip com manifest.json): instalar pelo fluxo C2.",
                        null, null);
            }
            return new Detection(Kind.ZIP_PLAIN, "arquivo compactado (.zip)", false,
                    "Isto e um .zip comum, sem manifest.json dentro. O Manager so instala "
                            + "pacote .bmod (o zip que tem manifest.json) ou arquivo solto "
                            + "com codigo de mod. Descompacte e instale o arquivo de dentro.", null, null);
        }

        if (isElf(h)) {
            if (isFridaGadgetName(s.fileName) || s.fridaMarker) {
                return new Detection(Kind.FRIDA_GADGET, "frida-gadget (runtime do Frida)", false,
                        "Isto e o frida-gadget, e NAO e um mod. Ele nao pode virar <id>.so: o "
                                + "loader abre qualquer .so da pasta direto, sem o frida-gadget.config, "
                                + "e o gadget sem config cai no modo padrao (listen), que abre uma "
                                + "porta e segura o jogo esperando um PC conectar. O jeito certo e "
                                + "instalar o binario como frida-gadget.bin (SEM .so) e escrever o "
                                + "frida-gadget.config com interaction script-directory apontando "
                                + "para esta mesma pasta. Assim o .js da pasta roda sem porta e sem PC.",
                        null, null);
            }
            int machine = elfMachine(h);
            if (machine == 183) { // E_AARCH64: o unico que o loader da dlopen no Android
                return installAs(s, ".so", Kind.ELF_ARM64, "mod nativo .so (arm64)");
            }
            return new Detection(Kind.ELF_OTHER_ARCH, "binario ELF de outra arquitetura", false,
                    "E um binario ELF, mas nao arm64 (" + archName(machine) + "). Celular Android "
                            + "de 64 bits roda mod compilado para arm64-v8a; este arquivo nao serve "
                            + "aqui.", null, null);
        }

        if (h.length >= 2 && h[0] == 'M' && h[1] == 'Z') {
            String t = (s.text != null) ? s.text : "";
            if (t.contains("mscorlib") || t.contains("_CorDllMain") || t.contains("mscoree")) {
                boolean il2cpp = t.contains("Il2CppInterop") || t.contains("UnhollowerBaseLib")
                        || t.contains("Il2CppDomain") || t.contains("BepInEx.Unity.IL2CPP");
                if (il2cpp) {
                    return new Detection(Kind.DOTNET_IL2CPP, "assembly .NET para IL2CPP", false,
                            "Este .dll e um mod .NET de IL2CPP (BepInEx 6 / MelonLoader IL2CPP). "
                                    + "Ele precisa do runtime .NET dentro do processo do jogo, que o "
                                    + "bepInEx-termux ainda nao tem (F12, nao implementado). O "
                                    + "Manager instala, mas o mod nao roda: use o Mod Maker (regras "
                                    + ".patch) ou espere a F12.", null, null);
                }
                if (engineIl2cpp) {
                    return new Detection(Kind.DOTNET_MONO, "assembly .NET de PC (Mono)", false,
                            "Este .dll e mod de PC (BepInEx 5 / MelonLoader Mono, o formato dos "
                                    + "mods de PC) e o jogo deste celular e IL2CPP. NAO RODA: jogo "
                                    + "IL2CPP nao tem runtime Mono, entao o .dll nem carrega. Para "
                                    + "mudar esse jogo Use o Mod Maker (regras .patch) ou procure a "
                                    + "versao IL2CPP do mod (F12).", null, null);
                }
                return new Detection(Kind.DOTNET_MONO, "assembly .NET (Mono)", false,
                        "Este .dll e mod .NET de Mono (BepInEx 5 / MelonLoader Mono). Para rodar "
                                + "precisa do runtime Mono carregado no processo do jogo (F13, nao "
                                + "implementado). Em jogo Unity Mono o caminho existe; em IL2CPP nao.",
                        null, null);
            }
            return new Detection(Kind.PE_NATIVE, "executavel/binario de Windows", false,
                    "Isto e um binario de Windows (PE), nao Android. Nao roda no celular de "
                            + "nenhum jeito, mesmo com root.", null, null);
        }

        if (isMachO(h)) {
            return new Detection(Kind.MACHO, "binario de iOS/macOS (Mach-O)", false,
                    "Isto e um binario Mach-O (iOS/macOS). Nao roda em Android.", null, null);
        }

        if (s.text != null) {
            String t = s.text;
            if (t.contains("Auto Assembler") || t.contains("<CheatEngine") || t.contains("Cheat Engine")) {
                return new Detection(Kind.CHEAT_ENGINE, "tabela do Cheat Engine", false,
                        "Isto e uma tabela do Cheat Engine (formato de PC). Para usar no celular "
                                + "precisa de GameGuardian ou do engine emulado; o Manager nao "
                                + "instala esse formato.", null, null);
            }
            if (!PatchGenerator.parse(t).isEmpty()) {
                return installAs(s, ".patch", Kind.PATCH, "regras declarativas (.patch)");
            }
            if (hasJsMarker(t)) {
                return installAs(s, ".js", Kind.FRIDA_JS, "script Frida (.js)");
            }
            if (t.contains("gg.") && t.contains("function") ) {
                return new Detection(Kind.LUA_GG, "script GameGuardian (lua)", false,
                        "Isto e script do GameGuardian (gg.). O caminho de script GameGuardian e "
                                + "F10 e ainda nao esta implementado. Para Frida, use script .js "
                                + "(com frida-gadget na pasta).", null, null);
            }
            return new Detection(Kind.TEXT_OTHER, "texto", false,
                    "Isto e um arquivo de texto, mas nao tem regra C4 (.patch) nem script Frida "
                            + "(.js) reconhecivel. O Manager so instala .so arm64, .patch e .js.",
                    null, null);
        }

        return new Detection(Kind.BINARY_UNKNOWN, "binario desconhecido", false,
                "Nao da para dizer o que e este arquivo: nao e .bmod, nem .so arm64, nem .dll, nem "
                        + "texto de .patch/.js.", null, null);
    }

    // Monta a Detection de um tipo que instala como <id><ext>. Nome invalido
    // ou reservado volta como installable=false, com o motivo.
    private static Detection installAs(Sample s, String ext, Kind kind, String label) {
        String id = baseId(s.fileName);
        if (id == null) {
            return new Detection(kind, label, false,
                    "O nome do arquivo nao serve como id de mod: use so letras, numeros, ponto, "
                            + "hifen e underscore, ate 48 caracteres (ex: meu_mod.so).", null, null);
        }
        if (RESERVED_IDS.contains(id.toLowerCase(Locale.ROOT))) {
            return new Detection(kind, label, false,
                    "'" + id + "' e nome de componente interno do bepInEx (u_patch/u_dump/u_frida). "
                            + "Renomeie o arquivo para nao sobrescrever o motor do sistema.", null, null);
        }
        return new Detection(kind, label, true, "", ext, id);
    }

    // ".so" / ".patch" / ".js" viram id sem extensao. Nulo se nao sobrar
    // nada utilizavel (o resto da deteccao explica o motivo).
    public static String baseId(String fileName) {
        if (fileName == null) return null;
        String name = fileName;
        int slash = Math.max(name.lastIndexOf('/'), name.lastIndexOf('\\'));
        if (slash >= 0) name = name.substring(slash + 1);
        int dot = name.lastIndexOf('.');
        if (dot > 0) name = name.substring(0, dot);
        // ".so", ".patch" ou oculto: nao sobra nome de mod, so extensao.
        if (name.isEmpty() || name.startsWith(".")) return null;
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < name.length(); i++) {
            char c = name.charAt(i);
            boolean ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
                    || c == '.' || c == '_' || c == '-';
            sb.append(ok ? c : '-');
        }
        String id = sb.toString().replaceAll("-{2,}", "-");
        while (id.startsWith("-")) id = id.substring(1);
        while (id.endsWith("-")) id = id.substring(0, id.length() - 1);
        if (id.isEmpty() || id.length() > 48) return null;
        return id;
    }

    public static boolean isFridaGadgetName(String fileName) {
        if (fileName == null) return false;
        String lower = fileName.toLowerCase(Locale.ROOT);
        return lower.contains("frida") || lower.contains("gadget");
    }

    // Maior marca do gadget: quem le o arquivo em pedacos precisa de uma
    // sobreposicao desse tamanho para nao cortar um marcador ao meio.
    public static int maxFridaMarkerLength() {
        int max = 0;
        for (String m : FRIDA_MARKERS) {
            max = Math.max(max, m.length());
        }
        return max;
    }

    public static boolean hasFridaMarker(String text) {
        if (text == null) return false;
        for (String m : FRIDA_MARKERS) {
            if (text.contains(m)) return true;
        }
        return false;
    }

    public static boolean hasJsMarker(String text) {
        if (text == null) return false;
        for (String m : JS_MARKERS) {
            if (text.contains(m)) return true;
        }
        return false;
    }

    public static boolean isZip(byte[] h) {
        return h.length >= 4 && h[0] == 'P' && h[1] == 'K'
                && (h[2] == 3 || h[2] == 5 || h[2] == 7) && (h[3] == 4 || h[3] == 6 || h[3] == 8);
    }

    public static boolean isElf(byte[] h) {
        return h.length >= 4 && h[0] == 0x7F && h[1] == 'E' && h[2] == 'L' && h[3] == 'F';
    }

    // e_machine: 2 bytes little-endian no offset 18 do ELF64/ELF32.
    public static int elfMachine(byte[] h) {
        if (h.length < 20) return -1;
        return (h[18] & 0xFF) | ((h[19] & 0xFF) << 8);
    }

    public static boolean isMachO(byte[] h) {
        if (h.length < 4) return false;
        int b0 = h[0] & 0xFF, b1 = h[1] & 0xFF, b2 = h[2] & 0xFF, b3 = h[3] & 0xFF;
        // 32/64 bits, little e big endian, e o fat binary 0xcafebabe.
        return (b0 == 0xFE && b1 == 0xED && b2 == 0xFA && b3 == 0xCE)
                || (b0 == 0xCE && b1 == 0xFA && b2 == 0xED && b3 == 0xFE)
                || (b0 == 0xFE && b1 == 0xED && b2 == 0xFA && b3 == 0xCF)
                || (b0 == 0xCF && b1 == 0xFA && b2 == 0xED && b3 == 0xFE)
                || (b0 == 0xCA && b1 == 0xFE && b2 == 0xBA && b3 == 0xBE);
    }

    private static String archName(int machine) {
        switch (machine) {
            case 3: return "x86 (32 bits)";
            case 40: return "ARM (32 bits, armeabi-v7a)";
            case 62: return "x86-64";
            case 183: return "arm64";
            case 243: return "RISC-V";
            case 258: return "LoongArch";
            default: return "e_machine " + machine;
        }
    }
}
