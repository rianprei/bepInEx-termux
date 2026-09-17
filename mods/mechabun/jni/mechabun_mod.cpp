// mechabun_mod.cpp — mod real pro bepinEx-termux: buff HP/ATK do Mecha-Bun
// (#426) em +80%, primeiro passo do design "comunitário ideal" documentado
// em battlecats-mecha-bun-ideal-comunidade.md (D2: "HP Up massivo (80%+)").
//
// COMO FUNCIONA (achado por engenharia reversa real, Ghidra + radare2,
// libnative-lib.so JP 15.6.0, build-id b94cc0da...):
//   FUN_008307ec(long bigData, int unitId) — carrega "unit%03d.csv"
//   (unitId+1) e popula uma struct por unidade em
//   bigData + unitId*0x760 + 0x9e568, com até 4 formas (stride 0x1d8=472
//   bytes cada), 118 campos int32 por forma (offset = coluna_csv * 4).
//   Colunas confirmadas em battlecats-mecha-bun-forense.md:
//     col6/7/8  = HP  (Lv1/Lv30/Lv50), raw pré-curva de crescimento
//     col9/10/11 = ATK (base/Lv30/Lv50), raw pré-curva
//   O jogo aplica a curva de unitlevel.csv DEPOIS disso — multiplicar o
//   raw por 1.8 aqui produz +80% no stat FINAL calculado, sem precisar
//   saber a fórmula exata da curva (ela é a mesma pra raw original e pra
//   raw*1.8, então a proporção se preserva).
//
// ESCOPO HONESTO (v1, não fabrica o que não foi verificado):
//   - Range (col2) e velocidade (col3) são ÍNDICES de lookup table, não
//     valor direto (confirmado no forense: raw 9 -> 190 exibido, raw 36
//     -> 23 exibido — não é proporção linear). Escalar o índice não
//     escala o resultado de forma previsível — NÃO mexido nesta v1.
//   - Recarga/frequência de ataque/imunidades (Surge/Wave/KB) — colunas
//     ainda não localizadas neste levantamento. NÃO implementado nesta
//     v1, documentado como próximo passo real, não fingido como feito.
//
// Mecha-Bun = unitId 425 (zero-based; unit426.csv = unitId+1=426).

#include <cstdint>
#include "bc_mod_api.h"

#define MECHABUN_UNIT_ID 425
#define FORM_STRIDE 0x1d8
#define UNIT_BLOCK_STRIDE 0x760
#define STAT_BLOCK_OFF 0x9e568
#define N_FORMS 4
#define HP_BUFF_NUM 9  // *9/5 = *1.8 exato em inteiro, sem float
#define HP_BUFF_DEN 5

typedef void (*orig_load_unit_fn)(long big_data, int unit_id);
static orig_load_unit_fn g_orig = nullptr;
static const bc_mod_api *g_api = nullptr;

// Assinatura AOB do prólogo de FUN_008307ec (48 bytes, ÚNICA no binário
// JP 15.6.0 inteiro — verificado: python bytes.count() no .so completo
// deu exatamente 1 ocorrência). Sem wildcard: todo byte é exato porque a
// checagem de unicidade já passou sem precisar mascarar imediato nenhum.
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

static void buff_field(long base, int col) {
    auto *p = reinterpret_cast<int32_t *>(base + (long)col * 4);
    *p = (int32_t)(((int64_t)*p * HP_BUFF_NUM) / HP_BUFF_DEN);
}

static void hooked_load_unit(long big_data, int unit_id) {
    g_orig(big_data, unit_id);  // deixa o parse original do CSV rodar
    if (unit_id != MECHABUN_UNIT_ID) return;

    long unit_base = big_data + (long)unit_id * UNIT_BLOCK_STRIDE + STAT_BLOCK_OFF;
    for (int form = 0; form < N_FORMS; form++) {
        long form_base = unit_base + (long)form * FORM_STRIDE;
        // HP: col 6, 7, 8 (Lv1/Lv30/Lv50)
        buff_field(form_base, 6);
        buff_field(form_base, 7);
        buff_field(form_base, 8);
        // ATK: col 9, 10, 11 (base/Lv30/Lv50)
        buff_field(form_base, 9);
        buff_field(form_base, 10);
        buff_field(form_base, 11);
    }
    if (g_api != nullptr) {
        g_api->log(BC_LOG_INFO, "[mechabun] HP/ATK +80% aplicado (4 formas)");
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
