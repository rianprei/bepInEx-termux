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
//   - texto C4   -> vira <id>.bpatch, lido pelo u_patch
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

    // Extensao do arquivo de REGRAS. ".bpatch" e nao ".patch" porque o nome
    // colidia com o .patch de diff do git e confundia quem olha a pasta de mods
    // (decisao do usuario, 2026-09-27).
    //
    // A constante e o UNICO lugar onde ela mora. Antes ela aparecia escrita em
    // seis arquivos (o detector, o Mod Maker, o BmodInstaller, o ModInfo, a
    // tela de detalhe do jogo), e seis literais e uma extensao esperando a
    // proxima metade do rename. Quem precisa do nome — inclusive o tradutor de
    // .dll, que produz texto C4 — le daqui ou daqui deriva pelo
    // ModContentDetector.
    //
    // O CONTEUDO decide o tipo, nao o nome: um arquivo C4 valido chamado
    // "minhas_regras.patch", ou sem extensao nenhuma, e reconhecido e
    // instalado como <id>.bpatch. Ver as fixtures regras_ext_antiga.patch e
    // regras_sem_extensao, e os casos da matriz de tipos.
    public static final String RULES_EXT = ".bpatch";

    // Nome do arquivo de regras para um id. Quem grava o arquivo usa isto em vez
    // de montar `id + extensao` num literal: o tradutor de .dll (que produz
    // texto C4), o Mod Maker e o instalador passam pelo mesmo caminho, entao nao
    // ha como um deles voltar a escrever a extensao antiga.
    public static String rulesFileName(String id) {
        return (id == null ? "" : id) + RULES_EXT;
    }

    public enum Kind {
        BMOD,             // zip com manifest.json do formato C2 (pelo CONTEUDO)
        ZIP_PLAIN,        // zip sem manifest.json de pacote .bmod
        ZIP_GAME_CONTAINER, // APK/OBB/XAPK: pacote do jogo, nunca mod
        BEPINEX_PC,       // zip de mod de PC com layout BepInEx: dll do PC;
                          // traducao para .bpatch e o futuro (dll2patch)
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
        TEXT_OTHER        // texto que nao e .bpatch nem script
    }

    // Resultado da deteccao: o que e, se instala, e por que (sempre em PT-BR).
    public static final class Detection {
        public final Kind kind;
        public final String label;
        public final boolean installable;
        public final String reason;      // por que roda, ou por que nao roda
        public final String targetExt;   // ".so" / ".bpatch" / ".js", ou null
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
            return new Detection(Kind.BINARY_UNKNOWN, "arquivo vazio", false,
                    "Este arquivo está vazio (0 byte). Não é mod de tipo nenhum: foi um "
                            + "download que falhou ou uma cópia que não copiou nada. Baixe o "
                            + "mod de novo e confira que o arquivo tem conteúdo.", null, null);
        }

        if (isZip(h)) {
            // manifest.json pelo CONTEUDO (formato 1 + id C2), nao pela
            // existencia: o .xapk tambem traz um manifest.json, mas e o do
            // instalador de APK (xapk_version), nao o de pacote .bmod. Sem
            // essa leitura, um APK renomeado .bmod seria "instalado" e o
            // erro so apareceria quebrado, depois, dentro do fluxo C2.
            if (isBmodManifest(s.zipManifestBytes)) {
                return new Detection(Kind.BMOD, "pacote de mod (.bmod)", true,
                        "Pacote de mod (.bmod), no formato do projeto: pode instalar.",
                        null, null);
            }
            // Layout de mod de PC (pasta BepInEx com plugins .dll): dizemos a
            // verdade — o .dll e do computador e não roda no celular; a
            // conversao de mods simples e o que vem por cima disso.
            if (isBepInExLayout(s)) {
                return new Detection(Kind.BEPINEX_PC, "mod da versão de PC (BepInEx)", false,
                        "Isto é um mod da versão de PC do jogo, embalado para o BepInEx do "
                                + "computador (a pasta BepInEx com plugins está aí dentro). O .dll "
                                + "dele é feito para o jogo rodando no computador e não funciona "
                                + "no celular. Em breve vai dar para converter mods simples para o "
                                + "formato que o celular roda (.bpatch); por enquanto, procure a "
                                + "versão para Android deste mod.", null, null);
            }
            if (looksLikeGameContainer(s)) {
                String what = gameContainerName(s);
                return new Detection(Kind.ZIP_GAME_CONTAINER, what, false,
                        "Isto é o " + what + " do próprio jogo, não um mod. O Manager nunca "
                                + "modifica arquivo do jogo: nem pacote do aplicativo, nem expansão. "
                                + "Para instalar mod, escolha o arquivo de mod em si (.so, .bpatch "
                                + "ou .js).", null, null);
            }
            return new Detection(Kind.ZIP_PLAIN, "arquivo compactado (.zip)", false,
                    "Isto é um arquivo compactado (.zip) comum, sem pacote de mod dentro. O "
                            + "Manager só instala pacote de mod (.bmod) ou arquivo de mod avulso. "
                            + "Descompacte no gerenciador de arquivos e volte aqui com o arquivo "
                            + "de dentro.", null, null);
        }

        if (isElf(h)) {
            if (isFridaGadgetName(s.fileName) || s.fridaMarker) {
                return new Detection(Kind.FRIDA_GADGET, "ferramenta Frida, não mod", false,
                        "Isto é o programa do Frida (a ferramenta que roda os scripts), não um "
                                + "mod. Se ele entrar como mod, o jogo abre e fica travado esperando "
                                + "um computador conectar. O Manager sabe cuidar dele: toque em "
                                + "instalar e ele vai para o lugar certo, junto com a configuração "
                                + "que faz os scripts .js da pasta rodarem sozinhos.", null, null);
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
                    return new Detection(Kind.ELF_MALFORMED, "mod para Android estragado", false,
                            "O arquivo é um mod para Android, mas está estragado: " + elf.reason
                                    + ". Instalar mod pela metade fecha o jogo. Baixe o mod de novo "
                                    + "e tente outra vez.", null, null);
                }
                return installAs(s, ".so", Kind.ELF_ARM64, "mod nativo para Android");
            }
            return new Detection(Kind.ELF_OTHER_ARCH, "mod para outro aparelho", false,
                    "É um mod de verdade, mas feito para outro tipo de aparelho (" + archName(machine)
                            + "). O seu celular só roda a versão para ARM de 64 bits; procure o "
                            + "download para ARM 64 deste mod.", null, null);
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
                    return new Detection(Kind.DOTNET_IL2CPP, "mod .dll (versão nova do jogo)", false,
                            "Este .dll é um mod feito para a versão do jogo compilada com IL2CPP "
                                    + "(tipo novo de build). O celular ainda não roda esse formato: não "
                                    + "há como instalar agora. Em breve a conversão de mods simples para "
                                    + "regras .bpatch vai cuidar dos casos fáceis; por enquanto, use o "
                                    + "mod maker ou procure outra versão do mod.", null, null);
                }
                if (engineIl2cpp) {
                    return new Detection(Kind.DOTNET_MONO, "mod .dll da versão de PC", false,
                            "Este .dll é um mod da versão de PC do jogo (feita com Mono). O jogo do "
                                    + "seu celular não foi feito nesse formato: o arquivo não roda "
                                    + "aqui, de jeito nenhum. Em breve a conversão de mods simples para "
                                    + "regras .bpatch vai cuidar dos casos fáceis; por enquanto, use o "
                                    + "mod maker ou procure a versão para Android do mod.", null, null);
                }
                return new Detection(Kind.DOTNET_MONO, "mod .dll da versão de PC", false,
                        "Este .dll é um mod da versão de PC do jogo (feita com Mono). Não roda em "
                                + "nenhum jogo aqui hoje: o celular ainda não tem esse suporte. Em "
                                + "breve a conversão de mods simples para regras .bpatch vai cuidar dos "
                                + "casos fáceis; por enquanto, use o mod maker.", null, null);
            }
            return new Detection(Kind.PE_NATIVE, "programa de Windows", false,
                    "Isto é um programa de Windows, não um mod para Android. Não roda no celular "
                            + "de jeito nenhum, nem com root.", null, null);
        }

        if (isMachO(h)) {
            return new Detection(Kind.MACHO, "mod de iPhone/Mac", false,
                    "Isto é um mod para iPhone ou Mac, não para Android. Não roda no seu "
                            + "celular; procure a versão para Android deste mod.", null, null);
        }

        // Asset de engine: magic no comeco manda; a extensao e pista apenas
        // quando o conteudo nao diz nada (todo .pak de Unreal comeca com um
        // numero de versao cru, que e igual a lixo qualquer).
        if (startsWith(h, UNITYFS_MAGIC)) {
            return new Detection(Kind.GAME_DATA, "dado do jogo (Unity)", false,
                    "Isto é um arquivo de dados do próprio jogo (uma cena, imagem ou som "
                            + "empacotado pelo Unity — formato UnityFS). Não é mod: é uma peça do "
                            + "jogo. O Manager nunca modifica arquivo do jogo.", null, null);
        }
        if (isUnrealPak(s)) {
            return new Detection(Kind.GAME_DATA, "dado do jogo (Unreal)", false,
                    "Isto é um arquivo de dados do próprio jogo (formato .pak, do Unreal "
                            + "Engine): imagens, mapas e sons que já vieram com o jogo. Não é mod; "
                            + "o Manager nunca modifica arquivo do jogo.", null, null);
        }

        if (s.text != null) {
            String t = s.text;
            if (t.contains("Auto Assembler") || t.contains("<CheatEngine") || t.contains("Cheat Engine")) {
                return new Detection(Kind.CHEAT_ENGINE, "tabela do Cheat Engine", false,
                        "Isto é uma tabela do Cheat Engine, ferramenta de computador. Não é um "
                                + "formato que o celular aproveite aqui; procure um mod para Android "
                                + "do jogo.", null, null);
            }
            if (!PatchGenerator.parse(t).isEmpty()) {
                return installAs(s, RULES_EXT, Kind.PATCH, "regras de mod (" + RULES_EXT + ")");
            }
            if (hasJsMarker(t)) {
                return installAs(s, ".js", Kind.FRIDA_JS, "script Frida (.js)");
            }
            if (t.contains("gg.") && t.contains("function") ) {
                return new Detection(Kind.LUA_GG, "script do GameGuardian", false,
                        "Isto é um script do GameGuardian, que roda em outro aplicativo, não "
                                + "aqui. O Manager ainda não instala esse tipo de script; os scripts "
                                + "que rodam aqui são os .js do Frida.", null, null);
            }
            // Save do jogador: JSON sem nenhuma regra C4 nem script, mas com
            // cara de progresso. A extensao aqui e so o empate final, e a
            // recusa diz a verdade: save nao e mod.
            if (looksLikeSaveJson(t, s.fileName)) {
                return new Detection(Kind.SAVE_GAME, "save do jogo", false,
                        "Isto é o save do jogo (o progresso do jogador), não um mod. O save "
                                + "guarda onde você parou; não adiciona nada ao jogo. O Manager só "
                                + "instala mod, então não há o que fazer com ele aqui.", null, null);
            }
            return new Detection(Kind.TEXT_OTHER, "arquivo de texto", false,
                    "Isto é um arquivo de texto, mas não é um mod: não tem regras de .bpatch nem "
                            + "é um script .js. O Manager instala mod em arquivo .so (para Android), "
                            + ".bpatch e .js.", null, null);
        }

        return new Detection(Kind.BINARY_UNKNOWN, "arquivo desconhecido", false,
                "Não dá para dizer o que é este arquivo: não é pacote de mod (.bmod), nem mod "
                        + "para Android (.so), nem mod de computador (.dll), nem texto de mod "
                        + "(.bpatch ou .js).", null, null);
    }

    // Monta a Detection de um tipo que instala como <id><ext>. Nome invalido
    // ou reservado volta como installable=false, com o motivo.
    private static Detection installAs(Sample s, String ext, Kind kind, String label) {
        String id = baseId(s.fileName);
        if (id == null) {
            return new Detection(kind, label, false,
                    "O nome do arquivo não serve como nome de mod: use só letras, números, "
                            + "ponto, hífen e underscore, até 48 caracteres (exemplo: meu_mod.so).",
                    null, null);
        }
        if (RESERVED_IDS.contains(id.toLowerCase(Locale.ROOT))) {
            if ("frida-gadget".equals(id.toLowerCase(Locale.ROOT))) {
                return new Detection(Kind.FRIDA_GADGET, label, false,
                        "Isto é o programa do Frida, não um mod. Se entrar como mod, o jogo abre "
                                + "e fica travado esperando um computador conectar. Instale-o pelo "
                                + "Manager, que coloca cada coisa no seu lugar.", null, null);
            }
            return new Detection(kind, label, false,
                    "'" + id + "' é o nome de uma peça interna do próprio Manager. Renomeie o "
                            + "arquivo para não sobrescrever uma peça do sistema.", null, null);
        }
        return new Detection(kind, label, true, "", ext, id);
    }

    // ".so" / ".bpatch" / ".js" viram id sem extensao. Nulo se nao sobrar
    // nada utilizavel (o resto da deteccao explica o motivo).
    public static String baseId(String fileName) {
        if (fileName == null) return null;
        String name = fileName;
        int slash = Math.max(name.lastIndexOf('/'), name.lastIndexOf('\\'));
        if (slash >= 0) name = name.substring(slash + 1);
        int dot = name.lastIndexOf('.');
        if (dot > 0) name = name.substring(0, dot);
        // ".so", ".bpatch" ou oculto: nao sobra nome de mod, so extensao.
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
        if (lower.endsWith(".obb")) return "arquivo de expansão (OBB)";
        if (lower.endsWith(".xapk")) return "pacote XAPK";
        if (lower.endsWith(".apks")) return "pacote de aplicativo dividido";
        return "pacote do aplicativo (APK)";
    }

    // Layout de mod de PC: zip com a pasta BepInEx (plugins/patchers) dentro,
    // ou qualquer .dll dentro de uma pasta plugins. E CONTEUDO, nao extensao:
    // o zip podia estar com qualquer nome.
    private static boolean isBepInExLayout(Sample s) {
        for (String name : s.zipEntryNames) {
            String lower = name.toLowerCase(Locale.ROOT);
            if (lower.startsWith("bepinex/")) return true;
            if (lower.contains("plugins/") && lower.endsWith(".dll")) return true;
        }
        return false;
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
            return new ElfCheck(false, "cabeçalho cortado (" + h.length + " bytes lidos, eram precisos 64)");
        }
        if ((h[EI_CLASS] & 0xFF) != ELFCLASS64) {
            return new ElfCheck(false, "não é o formato de 64 bits esperado");
        }
        if ((h[EI_DATA] & 0xFF) != ELFDATA2LSB) {
            return new ElfCheck(false, "não está na ordem de bytes esperada");
        }

        int eType = u16(h, 16);
        if (eType == ET_EXEC) {
            return new ElfCheck(false, "é um programa fechado nele mesmo, não uma biblioteca (ET_EXEC em vez de ET_DYN)");
        }
        if (eType != ET_DYN) {
            return new ElfCheck(false, "tipo de arquivo fora do esperado (" + eType + ")");
        }
        if (elfMachine(h) != EM_AARCH64) {
            return new ElfCheck(false, "foi feito para outro tipo de aparelho (código " + elfMachine(h) + ")");
        }
        if (fileLength < EH_SIZE_64) {
            return new ElfCheck(false, "o arquivo tem " + fileLength + " bytes, pequeno demais");
        }

        int eEhsize = u16(h, 52);
        if (eEhsize != EH_SIZE_64) {
            return new ElfCheck(false, "cabeçalho com tamanho fora do padrão (" + eEhsize + ")");
        }
        int ePhentsize = u16(h, 54);
        if (ePhentsize != PH_ENT_SIZE_64) {
            return new ElfCheck(false, "tabela interna com tamanho fora do padrão (" + ePhentsize + ")");
        }
        int ePhnum = u16(h, 56);
        if (ePhnum == 0) {
            return new ElfCheck(false, "sem nenhuma parte carregável por dentro");
        }
        if (ePhnum > PH_NUM_MAX) {
            return new ElfCheck(false, "tabela interna com " + ePhnum + " partes, um absurdo");
        }
        long ePhoff = u64(h, 32);
        if (ePhoff < EH_SIZE_64 || ePhoff + (long) ePhnum * ePhentsize > fileLength) {
            return new ElfCheck(false, "a tabela interna fica fora do arquivo (posição " + ePhoff
                    + ", " + ePhnum + " partes, arquivo de " + fileLength + " bytes)");
        }
        if (ePhoff + (long) ePhnum * ePhentsize > h.length) {
            return new ElfCheck(false, "tabela interna cortada: o arquivo tem " + h.length
                    + " bytes lidos e a tabela precisaria de " + (ePhoff + (long) ePhnum * ePhentsize));
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
                return new ElfCheck(false, "uma parte do código fica fora do arquivo (posição "
                        + pOffset + ", " + pFilesz + " bytes, arquivo de " + fileLength + ")");
            }
        }
        if (!sawLoad) {
            return new ElfCheck(false, "não tem nenhuma parte de código para o sistema carregar");
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

    // Nome do processador para o qual o arquivo foi feito. Texto que vai
    // aparecer para o usuario: sem codigo de maquina, so o nome que ele
    // reconhece da pagina de download do mod.
    private static String archName(int machine) {
        switch (machine) {
            case 3: return "computador de 32 bits";
            case 40: return "celular ARM de 32 bits";
            case 62: return "computador de 64 bits (x86-64)";
            case 183: return "ARM de 64 bits";
            case 243: return "RISC-V";
            case 258: return "LoongArch";
            default: return "um tipo de aparelho não listado (código " + machine + ")";
        }
    }
}
