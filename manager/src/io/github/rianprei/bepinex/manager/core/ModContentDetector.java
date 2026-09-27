package io.github.rianprei.bepinex.manager.core;

import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
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
//   - APK/OBB/XAPK, asset UnityFS, .pak de Unreal, save do jogador ->
//                   NUNCA modifica: e arquivo do jogo, nao mod
//
// O CONTEUDO manda sobre a extensao (o corpus de test/fixtures/modtypes
// prova os dois lados): ELF com nome .png instala, zip com nome .so e
// recusado como zip, .apk so e container do jogo se o nome diz isso — um
// .zip com classes.dex dentro cai no aviso comum de zip, e um .apk sem
// nada de Android la dentro seria so um zip. Nenhum caminho escreve no
// APK/OBB/arquivos do jogo: tudo em runtime.
public final class ModContentDetector {

    public enum Kind {
        BMOD,             // zip com manifest.json do formato C2 (pelo CONTEUDO)
        ZIP_PLAIN,        // zip sem manifest.json de pacote .bmod
        ZIP_GAME_CONTAINER, // APK/OBB/XAPK: pacote do jogo, nunca mod
        ELF_ARM64,        // ELF E_AARCH64 (183): mod nativo Android arm64
        ELF_MALFORMED,    // ELF arm64 com cabecalho/PT_LOAD incoerente
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
        GAME_DATA,        // UnityFS / .pak de Unreal: dado do jogo, nao mod
        SAVE_GAME,        // save do jogador (progresso em JSON)
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

    // Tudo que a deteccao precisa saber sobre o arquivo. Quem le o arquivo
    // preenche (LooseModInstaller); a deteccao aqui e PURA e testavel no host.
    public static final class Sample {
        public final String fileName;
        public final byte[] head;          // primeiros bytes (20+ para ler o ELF)
        public final String text;          // conteudo como texto, null se binario
        public final boolean zipHasManifest;
        public final boolean fridaMarker;  // assinatura do gadget no conteudo
        public final long length;          // tamanho REAL do arquivo em bytes
        public final byte[] zipManifestBytes; // conteudo de manifest.json, se zip
        public final List<String> zipEntryNames; // nomes das entradas, se zip

        public Sample(String fileName, byte[] head, String text,
                      boolean zipHasManifest, boolean fridaMarker) {
            this(fileName, head, text, zipHasManifest, fridaMarker,
                    (head != null) ? head.length : 0L);
        }

        public Sample(String fileName, byte[] head, String text,
                      boolean zipHasManifest, boolean fridaMarker, long length) {
            this(fileName, head, text, zipHasManifest, fridaMarker, length, null, null);
        }

        public Sample(String fileName, byte[] head, String text,
                      boolean zipHasManifest, boolean fridaMarker, long length,
                      byte[] zipManifestBytes, List<String> zipEntryNames) {
            this.fileName = (fileName != null) ? fileName : "";
            this.head = (head != null) ? head : new byte[0];
            this.text = text;
            this.zipHasManifest = zipHasManifest;
            this.fridaMarker = fridaMarker;
            this.length = length;
            this.zipManifestBytes = zipManifestBytes;
            this.zipEntryNames = (zipEntryNames != null) ? zipEntryNames : java.util.Collections.emptyList();
        }
    }

    // Componentes internos do bepInEx: esses nomes nao podem ser usados por
    // mod de terceiros (o Manager tambem esconde u_dump.so/u_patch.so na lista).
    // frida-gadget entra aqui pela garantia (c): ele NUNCA pode virar <id>.so.
    // Nomeado .so, o loader da dlopen sozinho, sem o frida-gadget.config, e o
    // gadget sem config cai no modo padrao (listen): abre porta e segura o
    // jogo esperando um PC conectar.
    private static final Set<String> RESERVED_IDS = new HashSet<>(Arrays.asList(
            "u_patch", "u_dump", "u_frida", "u_manager", "bepinex", "frida-gadget"));

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

        // Arquivo vazio (0 byte): nenhuma magica de extensao pode inventar
        // um tipo para ele. E o download que falhou inteiro.
        if (s.length <= 0) {
            return new Detection(Kind.BINARY_UNKNOWN, "arquivo vazio (0 byte)", false,
                    "Este arquivo esta VAZIO (0 byte). Nao e mod de tipo nenhum: foi um "
                            + "download que falhou ou uma copia que nao copiou nada. Baixe o "
                            + "mod de novo e confira que o arquivo tem conteudo.", null, null);
        }

        if (isZip(h)) {
            // manifest.json pelo CONTEUDO (formato 1 + id C2), nao pela
            // existencia: o .xapk tambem traz um manifest.json, mas e o do
            // instalador de APK (xapk_version), nao o de pacote .bmod. Sem
            // essa leitura, um APK renomeado .bmod seria "instalado" e o
            // erro so apareceria quebrado, depois, dentro do fluxo C2.
            if (isBmodManifest(s.zipManifestBytes)) {
                return new Detection(Kind.BMOD, "pacote .bmod", true,
                        "Pacote .bmod (zip com manifest.json do formato do projeto): "
                                + "instalar pelo fluxo C2.", null, null);
            }
            if (looksLikeGameContainer(s)) {
                String what = gameContainerName(s);
                return new Detection(Kind.ZIP_GAME_CONTAINER, what, false,
                        "Isto e o " + what + " — o pacote do PROPRIO JOGO, nao um mod. O "
                                + "Manager nunca modifica arquivo do jogo (nem APK, nem OBB, "
                                + "nem expansao): mod aqui e .so, .patch e .js em "
                                + "/data/local/tmp/mods/, em runtime. Se veio de um pacote "
                                + "de mod de PC, procure dentro dele a pasta BepInEx/ e o "
                                + "que for .dll .NET o Manager reconhece.", null, null);
            }
            return new Detection(Kind.ZIP_PLAIN, "arquivo compactado (.zip)", false,
                    "Isto e um .zip comum, sem manifest.json de pacote .bmod dentro. O "
                            + "Manager so instala pacote .bmod (o zip que tem manifest.json "
                            + "do projeto) ou arquivo solto com codigo de mod. Descompacte "
                            + "e instale o arquivo de dentro.", null, null);
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
                // Antes de virar <id>.so, o ELF64 tem que ser um .so de verdade:
                // ET_DYN, cabecalho e tabela de programas coerentes com o
                // tamanho REAL do arquivo, todo PT_LOAD dentro do arquivo.
                // Truncado (download cortado) ou adulterado e recusado com o
                // motivo, em vez de o jogo fechar ao tentar abrir.
                ElfCheck elf = validateElf64Arm64(h, s.length);
                if (!elf.ok) {
                    return new Detection(Kind.ELF_MALFORMED, "ELF arm64 invalido", false,
                            "O arquivo e arm64, mas esta corrompido ou adulterado: " + elf.reason
                                    + ". O Manager nao instala .so pela metade — o jogo tentaria abrir "
                                    + "isso e fecharia junto. Baixe o mod de novo.", null, null);
                }
                return installAs(s, ".so", Kind.ELF_ARM64, "mod nativo .so (arm64)");
            }
            return new Detection(Kind.ELF_OTHER_ARCH, "binario ELF de outra arquitetura", false,
                    "E um binario ELF, mas nao arm64 (" + archName(machine) + "). Celular Android "
                            + "de 64 bits roda mod compilado para arm64-v8a; este arquivo nao serve "
                            + "aqui.", null, null);
        }

        if (h.length >= 2 && h[0] == 'M' && h[1] == 'Z') {
            // PE de verdade e binario (zeros no proprio header DOS), entao o
            // "texto" do Sample vem vazio. As marcas de .NET (mscorlib,
            // mscoree, Il2CppInterop) moram em strings ASCII dentro do
            // binario: lemos como o strings do Unix le, senao todo .dll .NET
            // seria "binario de Windows" — a mentira classica.
            String t = (s.text != null && !s.text.isEmpty()) ? s.text : asciiStrings(h);
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

        // Asset de engine: magic no comeco manda; a extensao e pista apenas
        // quando o conteudo nao diz nada (todo .pak de Unreal comeca com um
        // numero de versao cru, que e igual a lixo qualquer).
        if (startsWith(h, UNITYFS_MAGIC)) {
            return new Detection(Kind.GAME_DATA, "asset Unity (UnityFS)", false,
                    "Isto e um ASSET do Unity (formato UnityFS): dado do jogo (textura, "
                            + "audio, cena), nao codigo de mod. Ele e parte dos arquivos do "
                            + "jogo, que o Manager nao modifica; trocar asset desses e "
                            + "modificar o jogo, nao instalar mod.", null, null);
        }
        if (isUnrealPak(s)) {
            return new Detection(Kind.GAME_DATA, "pacote de dados do Unreal (.pak)", false,
                    "Isto e um .pak do Unreal Engine: o pacote de dados do PROPRIO JOGO "
                            + "(texturas, mapas, audio). Nao e mod, e o Manager nunca "
                            + "modifica arquivo do jogo.", null, null);
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
            // Save do jogador: JSON sem nenhuma regra C4 nem script, mas com
            // cara de progresso. A extensao aqui e so o empate final, e a
            // recusa diz a verdade: save nao e mod.
            if (looksLikeSaveJson(t, s.fileName)) {
                return new Detection(Kind.SAVE_GAME, "save do jogo (progresso)", false,
                        "Isto e um SAVE do jogo (progresso do jogador em JSON), nao um mod. "
                                + "Save restaura progresso no jogo, nao adiciona codigo; o "
                                + "Manager so instala mod (codigo), entao nao tem onde isso "
                                + "encaixar.", null, null);
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
            if ("frida-gadget".equals(id.toLowerCase(Locale.ROOT))) {
                return new Detection(Kind.FRIDA_GADGET, label, false,
                        "Isto e o frida-gadget. Ele nao pode ser instalado como mod: o loader abre "
                                + "qualquer .so da pasta direto, sem o frida-gadget.config, e o gadget "
                                + "sem config cai no modo padrao (listen), que abre uma porta e segura "
                                + "o jogo esperando um PC conectar.", null, null);
            }
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

    // Extrai as sequencias de ASCII imprimivel (>= 4 chars) de um binario,
    // separadas por \n — a mesma ideia do strings(1). E o que permite ler
    // marcas de .NET/PE dentro de um arquivo binario de verdade.
    public static String asciiStrings(byte[] h) {
        if (h == null) return "";
        StringBuilder sb = new StringBuilder();
        int run = 0;
        for (byte b : h) {
            if (b >= 0x20 && b < 0x7F) {
                sb.append((char) b);
                run++;
            } else {
                if (run >= 4) sb.append('\n');
                run = 0;
            }
        }
        if (run >= 4) sb.append('\n');
        return sb.toString();
    }

    // --- pistas de container/asset/save (o CONTEUDO manda; extensao e pista)

    public static final byte[] UNITYFS_MAGIC = {'U', 'n', 'i', 't', 'y', 'F', 'S', 0};

    private static boolean startsWith(byte[] h, byte[] magic) {
        if (h.length < magic.length) return false;
        for (int i = 0; i < magic.length; i++) {
            if (h[i] != magic[i]) return false;
        }
        return true;
    }

    // manifest.json de pacote .bmod (C2): formato 1 e id no padrao do
    // contrato. O parser completo roda no instalador; aqui so o suficiente
    // para nao confundir .bmod com manifest de .xapk (xapk_version) ou de
    // save (campos de progresso).
    public static boolean isBmodManifest(byte[] raw) {
        if (raw == null || raw.length == 0 || raw.length > 64 * 1024) return false;
        Map<String, Object> json;
        try {
            json = MiniJson.parseObject(new String(raw, StandardCharsets.UTF_8));
        } catch (RuntimeException e) {
            return false;
        }
        if (!(json.get("format") instanceof Number) || ((Number) json.get("format")).intValue() != 1) {
            return false;
        }
        Object id = json.get("id");
        if (!(id instanceof String)) return false;
        return ((String) id).matches("[a-z0-9-]{3,48}");
    }

    private static final String[] GAME_CONTAINER_EXTS = {".apk", ".obb", ".xapk", ".apks"};

    // APK/OBB/XAPK: so e container do jogo se o NOME diz isso. O conteudo
    // de um zip nao prova nada (um mod de PC zipado tambem e zip); o nome
    // .apk/.obb e a pista do que o pacote e. Um .zip com classes.dex dentro
    // e rarissimo fora de APK — mas se acontecer, cai no aviso comum de zip,
    // que manda descompactar: mensagem honesta nos dois casos.
    private static boolean looksLikeGameContainer(Sample s) {
        String lower = s.fileName.toLowerCase(Locale.ROOT);
        for (String ext : GAME_CONTAINER_EXTS) {
            if (lower.endsWith(ext)) return true;
        }
        for (String name : s.zipEntryNames) {
            if (name.endsWith(".apk")) return true;
        }
        return false;
    }

    private static String gameContainerName(Sample s) {
        String lower = s.fileName.toLowerCase(Locale.ROOT);
        if (lower.endsWith(".obb")) return "arquivo OBB de expansao";
        if (lower.endsWith(".xapk")) return "pacote XAPK";
        if (lower.endsWith(".apks")) return "pacote APKS (APK split)";
        return "APK";
    }

    // .pak de Unreal: o magic e so um numero de versao cru (igual a lixo
    // qualquer); sem a extensao, nao ha como dizer. Conteudo + extensao
    // juntos e que dao a certeza honesta.
    private static boolean isUnrealPak(Sample s) {
        String lower = s.fileName.toLowerCase(Locale.ROOT);
        if (!lower.endsWith(".pak")) return false;
        return s.head.length >= 8 && s.text == null;   // binario nao-texto
    }

    // Save do jogador: JSON (o comeco tem cara de objeto/array) sem nenhuma
    // regra C4 nem script, com nome de save (ou campos tipicos de progresso).
    private static boolean looksLikeSaveJson(String text, String fileName) {
        String t = text.trim();
        if (t.isEmpty()) return false;
        char c = t.charAt(0);
        if (c != '{' && c != '[') return false;
        String lower = (fileName != null) ? fileName.toLowerCase(Locale.ROOT) : "";
        if (lower.endsWith(".json")) return true;
        String[] saveFields = {"\"save\"", "\"player\"", "\"inventory\"", "\"gold\"",
                "\"xp\"", "\"level\"", "\"save_version\"", "\"progress\""};
        for (String f : saveFields) {
            if (t.contains(f)) return true;
        }
        return false;
    }

    // --- validacao de ELF64 (so o que o loader vai mesmo abrir) -------------

    public static final class ElfCheck {
        public final boolean ok;
        public final String reason;   // null quando ok

        ElfCheck(boolean ok, String reason) {
            this.ok = ok;
            this.reason = reason;
        }
    }

    private static final int EI_CLASS = 4, EI_DATA = 5;
    private static final int ELFCLASS64 = 2, ELFDATA2LSB = 1;
    private static final int EM_AARCH64 = 183;
    private static final int ET_DYN = 3, ET_EXEC = 2;
    private static final int EH_SIZE_64 = 64, PH_ENT_SIZE_64 = 56, PH_NUM_MAX = 128;
    private static final int PT_LOAD = 1;

    // Devolve null quando o arquivo nem e ELF (o chamador so chama em isElf).
    // A partir dai, ok/reason dizem se da para tratar como .so arm64.
    public static ElfCheck validateElf64Arm64(byte[] h, long fileLength) {
        if (h.length < 24) {
            return new ElfCheck(false, "cabecalho cortado (" + h.length + " bytes lidos, ELF64 precisa de 64)");
        }
        if ((h[EI_CLASS] & 0xFF) != ELFCLASS64) {
            return new ElfCheck(false, "nao e ELF64 (classe " + (h[EI_CLASS] & 0xFF) + ")");
        }
        if ((h[EI_DATA] & 0xFF) != ELFDATA2LSB) {
            return new ElfCheck(false, "nao e little-endian (EI_DATA " + (h[EI_DATA] & 0xFF) + ")");
        }

        int eType = u16(h, 16);
        if (eType == ET_EXEC) {
            return new ElfCheck(false, "e_type e ET_EXEC (executavel); mod tem que ser ET_DYN (biblioteca)");
        }
        if (eType != ET_DYN) {
            return new ElfCheck(false, "e_type " + eType + " diferente de ET_DYN (3)");
        }
        if (elfMachine(h) != EM_AARCH64) {
            return new ElfCheck(false, "e_machine " + elfMachine(h) + " nao e arm64 (183)");
        }
        if (fileLength < EH_SIZE_64) {
            return new ElfCheck(false, "arquivo de " + fileLength + " bytes nao cabe um ELF64");
        }

        int eEhsize = u16(h, 52);
        if (eEhsize != EH_SIZE_64) {
            return new ElfCheck(false, "e_ehsize " + eEhsize + " (esperado 64)");
        }
        int ePhentsize = u16(h, 54);
        if (ePhentsize != PH_ENT_SIZE_64) {
            return new ElfCheck(false, "e_phentsize " + ePhentsize + " (esperado 56)");
        }
        int ePhnum = u16(h, 56);
        if (ePhnum == 0) {
            return new ElfCheck(false, "e_phnum 0: sem programa carregavel, nao e .so utilizavel");
        }
        if (ePhnum > PH_NUM_MAX) {
            return new ElfCheck(false, "e_phnum " + ePhnum + " absurdo (teto " + PH_NUM_MAX + ")");
        }
        long ePhoff = u64(h, 32);
        if (ePhoff < EH_SIZE_64 || ePhoff + (long) ePhnum * ePhentsize > fileLength) {
            return new ElfCheck(false, "tabela de programas fora do arquivo (e_phoff " + ePhoff
                    + ", e_phnum " + ePhnum + ", arquivo " + fileLength + " bytes)");
        }
        if (ePhoff + (long) ePhnum * ePhentsize > h.length) {
            return new ElfCheck(false, "tabela de programas cortada: o arquivo tem " + h.length
                    + " bytes lidos e a tabela precisa de " + (ePhoff + (long) ePhnum * ePhentsize));
        }

        boolean sawLoad = false;
        for (int i = 0; i < ePhnum; i++) {
            int off = (int) (ePhoff + (long) i * ePhentsize);
            int pType = u32(h, off);
            long pOffset = u64(h, off + 8);
            long pFilesz = u64(h, off + 32);
            if (pType != PT_LOAD) continue;
            sawLoad = true;
            if (pFilesz == 0 || pOffset < 0 || pOffset > fileLength || pFilesz > fileLength - pOffset) {
                return new ElfCheck(false, "PT_LOAD nao cabe no arquivo (offset " + pOffset
                        + " + " + pFilesz + " bytes, arquivo " + fileLength + ")");
            }
        }
        if (!sawLoad) {
            return new ElfCheck(false, "nenhum PT_LOAD: a biblioteca nao tem codigo para o linker mapear");
        }
        return new ElfCheck(true, null);
    }

    private static int u16(byte[] h, int off) {
        if (off + 2 > h.length) return -1;
        return (h[off] & 0xFF) | ((h[off + 1] & 0xFF) << 8);
    }

    private static int u32(byte[] h, int off) {
        if (off + 4 > h.length) return -1;
        return (h[off] & 0xFF) | ((h[off + 1] & 0xFF) << 8)
                | ((h[off + 2] & 0xFF) << 16) | ((h[off + 3] & 0xFF) << 24);
    }

    private static long u64(byte[] h, int off) {
        if (off + 8 > h.length) return -1L;
        long v = 0;
        for (int i = 7; i >= 0; i--) {
            v = (v << 8) | (h[off + i] & 0xFFL);
        }
        return v;
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
