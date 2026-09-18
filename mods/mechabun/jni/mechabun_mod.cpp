// mechabun_mod.cpp — mod real pro bepinEx-termux: aplica o design "ideal
// comunitário" do Mecha-Bun (#426) documentado em
// battlecats-mecha-bun-ideal-comunidade.md.
//
// COMO FUNCIONA (engenharia reversa real, Ghidra 12.1 headless + radare2,
// libnative-lib.so JP 15.6.0, build-id b94cc0da...):
//   FUN_008307ec(long bigData, int unitId) — carrega "unit%03d.csv"
//   (unitId+1) e popula uma struct por unidade em
//   bigData + unitId*0x760 + 0x9e568, com até 4 formas (stride 0x1d8=472
//   bytes cada), 118 campos int32 por forma (offset = coluna_csv * 4).
//   Layout estrutural (stride/offset/contagem de campo) verificado
//   diretamente na disassembly real do binário JP 15.6.0.
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
// NÃO FEITO (confirmado limitação real, não preguiça):
//   - Backswing: NÃO existe como campo CSV separado no schema real
//     (tbcml só tem "foreswing", índice 13, do attack_1) — o "backswing"
//     que a comunidade pede vem do timeline de animação
//     (mamodel/maanim), fora do escopo de um patch de memória puro.
//   - Warp Immunity (D14): não existe campo correspondente no schema
//     tbcml pra este slot de stats — não fabricado.
//
// Mecha-Bun = unitId 425 (zero-based; unit426.csv = unitId+1=426).

#include <cstdint>
#include "bc_mod_api.h"

#define MECHABUN_UNIT_ID 425
#define FORM_STRIDE 0x1d8
#define UNIT_BLOCK_STRIDE 0x760
#define STAT_BLOCK_OFF 0x9e568
#define N_FORMS 4

// Índices reais (tbcml cats.py:assign), NÃO os antigos e errados.
#define COL_HP 0
#define COL_SPEED 2
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

    long unit_base = big_data + (long)unit_id * UNIT_BLOCK_STRIDE + STAT_BLOCK_OFF;
    for (int form = 0; form < N_FORMS; form++) {
        long fb = unit_base + (long)form * FORM_STRIDE;

        // Alta confiança: proporcional preserva o resultado final
        // independente da fórmula da curva de level.
        scale_field(fb, COL_HP, 9, 5);     // +80%
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
                    "+80%, range/recarga/freq ajustados, 3 imunidades + "
                    "behemoth slayer + mini-wave + strengthen + dodge)");
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
    api->log(BC_LOG_INFO, "[mechabun] hook instalado, aguardando load do unit 426");
    return true;
}
