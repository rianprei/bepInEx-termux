// mechabun_mod.cpp — mod real pro bepinEx-termux: aplica o design "ideal
// comunitário" do Mecha-Bun (#426) documentado em
// battlecats-mecha-bun-ideal-comunidade.md.
//
// COMO FUNCIONA (engenharia reversa real, Ghidra 12.1 headless + radare2,
// libnative-lib.so JP 15.6.0, build-id b94cc0da...):
//   FUN_008307ec — nome dado pelo GHIDRA (endereço interno do Ghidra
//   0x8307ec, que tem offset de base +0x1000000 aplicado pelo loader do
//   Ghidra). O endereço/vaddr REAL no arquivo/binário é 0x7307ec —
//   diferença exata de 0x100000. Não afeta o mod (o hook usa busca por
//   assinatura AOB via resolve_pattern, nunca esse endereço fixo), mas
//   quem for cruzar isso manualmente com r2/objdump precisa subtrair
//   0x100000 do endereço mostrado pelo Ghidra pra achar o offset real.
//   FUN_008307ec(long bigData, int unitId) — carrega "unit%03d.csv"
//   (unitId+1, ex.: unitId=426 -> "unit427.csv") e popula uma struct
//   por unidade em bigData + (unitId+2)*0x760 + 0x9e568 — ATENCAO: o
//   offset pra ACHAR O STRUCT usa unitId+2 (achado real via decompile,
//   FUN_008307ec faz `param_2 = param_2 + 2` ANTES de multiplicar pelo
//   stride — nao e o unitId puro), enquanto o NOME DO ARQUIVO usa
//   unitId+1 — dois offsets diferentes, confirmados separadamente na
//   mesma funcao, nao confundir um com outro. Ate 4 formas (stride
//   0x1d8=472 bytes cada, `while (lVar10 != 4)` confirmado), 118
//   campos int32 por forma (offset = coluna_csv * 4, `uVar6 != 0x76`
//   onde 0x76=118 confirmado). Layout estrutural (stride/offset/
//   contagem de campo/offset+2) verificado diretamente no decompile
//   real do binário JP 15.6.0 (tmp/form_loop_result.txt).
//
// IDENTIDADE DAS COLUNAS: corrigida em cima de fonte AUTORITATIVA —
// tbcml (lib de modding Battle Cats ativa, mantida, instalada em
// ~/.venvs/battlecats-mod/), arquivo
// core/game_data/cat_base/cats.py:329-395 (`Stats.assign`). PRIMEIRA
// VERSÃO deste mod usava colunas erradas (6/7/8 pra HP, 9/10/11 pra ATK)
// baseado num doc do vault com erro de transcrição próprio (confirmado
// comparando a tabela do doc contra o CSV bruto que ele mesmo citava —
// não batia). tbcml é código executável mantido por terceiros, muito
// mais confiável que doc manuscrito — índices abaixo vêm de lá:
//   raw[0]=HP  raw[1]=knockback count  raw[2]=speed  raw[3]=ATK
//   raw[4]=attack interval  raw[5]=range  raw[6]=cost  raw[7]=recharge
//   raw[46]=wave_immunity  raw[48]=knockback_immunity
//   raw[91]=surge_immunity  raw[105]=behemoth_slayer
// (bool fields: tbcml unit_bool() = bool(value), ou seja 0=false,
// qualquer não-zero=true — setar 1 é seguro e correto.)
//
// ESCOPO — TODOS os campos abaixo confirmados via fonte autoritativa
// (tbcml unit.py/cats.py), nenhum é mais suposição rotulada:
//   - HP, ATK: multiplicar o raw por 1.8 propaga +80% pro stat final
//     computado, INDEPENDENTE da fórmula exata da curva de
//     unitlevel.csv (curva é multiplicativa sobre o raw — a mesma curva
//     aplicada a raw ou a raw*1.8 preserva a proporção 1.8x).
//   - Attack Interval, Recharge: CONFIRMADO via tbcml
//     (unit.py:126-136, `Frames.from_pair_frames`) — fórmula é
//     EXATA: frames_reais = raw * 2 ("pair frames"). Transform linear
//     provado, não suposto — escalar o raw proporcionalmente preserva a
//     proporção final exatamente.
//   - Range: CONFIRMADO via tbcml (cats.py:332, `self.range =
//     raw_data[5]`) — SEM wrapper de conversão nenhum, valor final =
//     raw direto. Escalar o raw por 250/190 dá final=250 exato.
//   - Imunidades (wave/knockback/surge) e Behemoth Slayer: bool flags
//     diretos, sem ambiguidade de escala — alta confiança, é só setar 1.
//
//   - Mini-wave (D6), Strengthen (D7), Dodge (D8): CONFIRMADO via tbcml
//     (unit.py classes Wave/Strengthen/Dodge, cats.py:assign linhas
//     349/350/362) — raw_data[35]=wave.prob, [36]=wave.level,
//     [94]=wave.is_mini; [40]=strengthen.hp_percent,
//     [41]=strengthen.multiplier_percent; [84]=dodge.prob,
//     [85]=dodge.time (frames). `Prob` é percentual direto (classe
//     `Prob`, unit.py:164-181, sem wrapper) — valores no design ideal
//     (10%, 50%, 150%, 20%, 1s=30 frames) são atribuídos direto, sem
//     conversão nenhuma precisar.
//
//   - Sage Slayer (D11), Explosion Immunity (D13): CONFIRMADO via
//     pesquisa dedicada (OpenCodePOCOC75) contra tbcml
//     (cats.py:307-330,438-449) e dados reais de outras unidades
//     (BCData unit781.csv col 111=1, unit780/784.csv col 116) — bool
//     flags diretos, mesmo padrão das outras imunidades.
//   - Warp Immunity (D14): CORRIGIDO nesta revisão — doc anterior
//     dizia "não existe campo", ERRADO. Campo real confirmado direto
//     no tbcml: `warp_blocker`, índice 75, bool flag (cats.py:227-228,
//     `self.warp_blocker = core.unit_bool(raw_data[75])`), mesmo padrão
//     das outras imunidades. Implementado.
//   - Toxic Immunity (opcional no design 3.2, baixo consenso mas
//     barato): campo real confirmado, `toxic_immunity`, índice 90,
//     bool flag (cats.py:258-259/419). Implementado.
//   - Shrug Off (D15): RESOLVIDO — pesquisa cruzada (4 fontes
//     independentes: tbcml bcu.py:176-177/547-548, BCU-java-PC
//     util.properties:364 "ot13=Dodge Attack", BCU-java-PC
//     Interpret.java:127/136, cats.py:867-920) confirma que "Shrug
//     Off" e "Dodge" são a MESMA mecânica interna — o proc `IMUATK`.
//     Não existe proc "DODGE" separado; "Shrug Off" é só o nome que a
//     comunidade deu ao mesmo proc IMUATK num contexto sem trait-alvo
//     específico. D15 = D8, já implementado via COL_DODGE_PROB/
//     COL_DODGE_TIME_FRAMES — não é aproximação, é a mesma mecânica
//     confirmada por identidade de código, não por falta de opção
//     melhor.
//
// NÃO FEITO (confirmado limitação real, não preguiça — verificado
// diretamente no tbcml, não é suposição herdada):
//   - Backswing: NÃO existe como campo CSV separado no schema real
//     (tbcml só tem "foreswing", índice 13, do attack_1) — o "backswing"
//     que a comunidade pede vem do timeline de animação
//     (mamodel/maanim), fora do escopo de um patch de memória puro.
//     Investigação dedicada (nomes de arquivo tipo "%s_entry.maanim")
//     confirma que o playback de animação é sistema de engine genérico
//     (tipo cocos2d), não dado isolado por unidade — hookar isso com
//     segurança exigiria RE de escopo comparável ou maior que o mod
//     inteiro, ainda em investigação.
//   - Ultra Form (D16): CORREÇÃO — claim anterior de "já coberto pelo
//     loop de 4 formas" estava errado. Dado real (BCData unit427.csv)
//     mostra Mecha-Bun tem só 3 formas hoje (Normal/Evolved/True); a
//     4ª iteração do loop escreve numa área que o jogo não usa/renderiza
//     pra essa unidade — não é "dar Ultra Form", é escrita inerte sem
//     efeito visível. Ultra Form de verdade exige a Ponos adicionar a
//     forma (animação/asset/stats) ao jogo — mod de patch de memória
//     não fabrica conteúdo que o jogo não tem alocado.
//   - Talents oficiais (D17): o próprio design "ideal comunitário"
//     (seção 3.3) mapeia os 10 níveis de talento propostos 1:1 pros
//     efeitos D1/D2/D3/D4/D5/D6/D7/D8/D9/D10 — TODOS já implementados
//     como stat base incondicional neste mod (sem custo de NP). Um
//     sistema de talento de verdade (hook diferente, curva de NP, save
//     de progresso) entregaria o MESMO efeito, só que atrás de grind —
//     pior pro jogador que o que já existe. Não implementado por não
//     ter ganho real, não por limitação técnica.
//
// Mecha-Bun = unitId 426 (zero-based; unit427.csv = unitId+1=427).
//
// CORRECAO CRITICA (achado real desta revisao, cross-referenciado
// contra BCData real): a sessao inteira usava unitId=425 (unit426.csv)
// por engano. unit426.csv/Unit_Explanation427_en.csv sao unidades
// DIFERENTES sem relacao com Mecha-Bun (unit426.csv = "太秦鴻＆ネコ",
// um personagem escolar). O nome real "Mecha-Bun" so aparece em
// Unit_Explanation427_en.csv -> unitId zero-based = 426 -> unit427.csv.
// Confirmado por dado real: unit427.csv raw[5] (range) = 190, batendo
// EXATO com a referencia de design "Range 190->250" usada pelo mod —
// prova forte de que 426 e o ID certo e 425 nunca foi.

#include <cstdint>
#include "bc_mod_api.h"

#define MECHABUN_UNIT_ID 426
#define FORM_STRIDE 0x1d8
#define UNIT_BLOCK_STRIDE 0x760
#define STAT_BLOCK_OFF 0x9e568
#define N_FORMS 4
// FUN_008307ec faz `param_2 = param_2 + 2` ANTES de multiplicar pelo
// stride pra achar o endereco do struct (confirmado via decompile real,
// tmp/form_loop_result.txt linha 70-72) — o indice usado pra
// enderecamento NAO e o unitId puro, e unitId+2 (provavel reserva de 2
// slots no inicio da tabela). O filename ("unit%03d.csv") usa
// unitId+1, endereco do struct usa unitId+2 — dois offsets DIFERENTES,
// nao confundir.
#define UNIT_ID_TABLE_OFFSET 2

// Índices reais (tbcml cats.py:assign), NÃO os antigos e errados.
#define COL_HP 0
#define COL_ATK 3
#define COL_ATTACK_INTERVAL 4
#define COL_RANGE 5
#define COL_RECHARGE 7
#define COL_WAVE_IMMUNITY 46
#define COL_KNOCKBACK_IMMUNITY 48
#define COL_SURGE_IMMUNITY 91
#define COL_BEHEMOTH_SLAYER 105
#define COL_WAVE_PROB 35
#define COL_WAVE_LEVEL 36
#define COL_WAVE_IS_MINI 94
#define COL_STRENGTHEN_HP_PERCENT 40
#define COL_STRENGTHEN_MULT_PERCENT 41
#define COL_DODGE_PROB 84
#define COL_DODGE_TIME_FRAMES 85
#define COL_SAGE_SLAYER 111
#define COL_EXPLOSION_IMMUNITY 116
#define COL_WARP_IMMUNITY 75
#define COL_TOXIC_IMMUNITY 90
#define COL_TARGET_FLOATING 16
#define COL_TARGET_AKU 96
#define COL_STRONG_AGAINST 23
#define TRUE_FORM_INDEX 2

typedef void (*orig_load_unit_fn)(long big_data, int unit_id);
static orig_load_unit_fn g_orig = nullptr;
static const bc_mod_api *g_api = nullptr;

// Assinatura AOB do prólogo de FUN_008307ec (48 bytes, ÚNICA no binário
// JP 15.6.0 inteiro — verificado via busca exaustiva de bytes no .so
// completo, contagem exata = 1).
static const uint8_t kPattern[] = {
    0xe8, 0x0f, 0x19, 0xfc, 0xfd, 0x7b, 0x01, 0xa9, 0xfc, 0x6f, 0x02, 0xa9,
    0xfa, 0x67, 0x03, 0xa9, 0xf8, 0x5f, 0x04, 0xa9, 0xf6, 0x57, 0x05, 0xa9,
    0xf4, 0x4f, 0x06, 0xa9, 0xfd, 0x43, 0x00, 0x91, 0xff, 0x03, 0x07, 0xd1,
    0x48, 0xd0, 0x3b, 0xd5, 0xf3, 0x03, 0x00, 0xaa, 0xe0, 0x63, 0x01, 0x91,
};
static const uint8_t kMask[sizeof(kPattern)] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};

static inline int32_t *field_ptr(long form_base, int col) {
    return reinterpret_cast<int32_t *>(form_base + (long)col * 4);
}

// Multiplica um campo por num/den (inteiro exato, sem float).
static void scale_field(long form_base, int col, int64_t num, int64_t den) {
    int32_t *p = field_ptr(form_base, col);
    *p = (int32_t)(((int64_t)*p * num) / den);
}

static void set_bool_field(long form_base, int col) {
    *field_ptr(form_base, col) = 1;
}

// Atribui valor direto (percentual/frames já na unidade certa, sem
// escala — Prob e Frames do tbcml não têm wrapper de conversão).
static void set_field(long form_base, int col, int32_t value) {
    *field_ptr(form_base, col) = value;
}

static void hooked_load_unit(long big_data, int unit_id) {
    g_orig(big_data, unit_id);  // deixa o parse original do CSV rodar
    if (unit_id != MECHABUN_UNIT_ID) return;

    long unit_base = big_data +
        (long)(unit_id + UNIT_ID_TABLE_OFFSET) * UNIT_BLOCK_STRIDE +
        STAT_BLOCK_OFF;
    for (int form = 0; form < N_FORMS; form++) {
        long fb = unit_base + (long)form * FORM_STRIDE;

        // Alta confiança: proporcional preserva o resultado final
        // independente da fórmula da curva de level.
        // D16 Ultra Form (folded na True Form, sem forma nova): raw HP
        // da True Form em unit427.csv (JP 15.0.0) = 4000, referência
        // "atual" de HP Lv50 = 86.400 (fonte: design doc). Alvo Ultra
        // = 520.000 -> razao exata 520000/86400 = 325/54 (fracao
        // irredutivel, mesma logica proporcional das outras escalas:
        // curva de level e multiplicativa sobre o raw, entao a razao
        // final bate o alvo independente da formula exata da curva).
        // Formas 0/1 (Normal/Evolved) mantem o alvo ideal padrao
        // (+80%, D2); só a True Form recebe o tier Ultra.
        if (form == TRUE_FORM_INDEX) {
            scale_field(fb, COL_HP, 325, 54);  // alvo Ultra: 520.000 HP Lv50
        } else {
            scale_field(fb, COL_HP, 9, 5);     // +80% (D2)
        }
        scale_field(fb, COL_ATK, 9, 5);    // +80%

        // Confirmado via tbcml (não suposição): range sem transform,
        // recharge/attack_interval = raw*2 exato (pair frames).
        scale_field(fb, COL_RANGE, 250, 190);        // 190 -> 250
        scale_field(fb, COL_RECHARGE, 2136, 2536);   // 2536f -> 2136f (-400f)
        scale_field(fb, COL_ATTACK_INTERVAL, 26, 32); // 32f -> 26f

        // Alta confiança: bool flags diretos.
        set_bool_field(fb, COL_WAVE_IMMUNITY);
        set_bool_field(fb, COL_KNOCKBACK_IMMUNITY);
        set_bool_field(fb, COL_SURGE_IMMUNITY);
        set_bool_field(fb, COL_BEHEMOTH_SLAYER);
        set_bool_field(fb, COL_SAGE_SLAYER);        // D11
        set_bool_field(fb, COL_EXPLOSION_IMMUNITY); // D13
        set_bool_field(fb, COL_WARP_IMMUNITY);      // D14
        set_bool_field(fb, COL_TOXIC_IMMUNITY);     // opcional (design ideal 3.2)

        // D16 Ultra (traits): Strong Against Floating/Relic/Aku.
        // Mecha-Bun já nasce com strong=1 e target_relic=1 nativos
        // (confirmado no raw real, unit427.csv linha 3/True Form) —
        // só faltam target_floating e target_aku pra completar os 3
        // traits pedidos pelo design ideal.
        set_bool_field(fb, COL_STRONG_AGAINST);
        set_bool_field(fb, COL_TARGET_FLOATING);
        set_bool_field(fb, COL_TARGET_AKU);

        // D6 Mini-wave: 10% chance, nível 1, variante mini.
        set_field(fb, COL_WAVE_PROB, 10);
        set_field(fb, COL_WAVE_LEVEL, 1);
        set_bool_field(fb, COL_WAVE_IS_MINI);

        // D7 Strengthen: ativa a 50% de HP, +50% de dano (150%).
        set_field(fb, COL_STRENGTHEN_HP_PERCENT, 50);
        set_field(fb, COL_STRENGTHEN_MULT_PERCENT, 150);

        // D8 Dodge: 20% de chance, esquiva por 1s (30 frames a 30fps).
        set_field(fb, COL_DODGE_PROB, 20);
        set_field(fb, COL_DODGE_TIME_FRAMES, 30);
    }
    if (g_api != nullptr) {
        g_api->log(BC_LOG_INFO,
                    "[mechabun] design ideal comunitario aplicado (HP/ATK "
                    "+80%, range/recarga/freq ajustados, 7 imunidades + "
                    "behemoth/sage slayer + mini-wave + strengthen + dodge)");
    }
}

extern "C" BC_MOD_EXPORT bool bc_mod_register(const bc_mod_api *api) {
    if (api == nullptr || api->version < 3 || api->install_hook == nullptr ||
        api->resolve_pattern == nullptr) {
        return false;  // loader antigo sem install_hook — mod fica inativo, não crasha
    }
    g_api = api;
    void *target = api->resolve_pattern(kPattern, kMask, sizeof(kPattern));
    if (target == nullptr) {
        api->log(BC_LOG_WARN,
                  "[mechabun] assinatura do loader de unit CSV nao achada "
                  "(build do jogo mudou?) - mod fica inativo");
        return false;
    }
    bool ok = api->install_hook(target, (void *)hooked_load_unit,
                                 (void **)&g_orig);
    if (!ok) {
        api->log(BC_LOG_ERROR, "[mechabun] install_hook falhou");
        return false;
    }
    api->log(BC_LOG_INFO, "[mechabun] hook instalado, aguardando load do unit 427");
    return true;
}
