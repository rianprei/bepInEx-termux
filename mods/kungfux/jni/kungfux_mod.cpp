// kungfux_mod.cpp — Kung Fu Cat X (#132): remove os cons levantados pela
// comunidade sem mexer nos pros. Cons e fontes: context/kfx-cons-CONSOLIDADO.md;
// plano e contas: context/kungfu-cat-x-132-plano.md (vault).
//
//   con                              col       vanilla (f0/f1/f2)  patch
//   "Slow ... movement speed"        2         8                   10 (mediana Rare)
//   "One knockback" / fragil         1         1                   3  (mediana Rare)
//   "Single Attack"                 12         0                   1  (area)
//   "Slow attack rate"               4         100/88/73           40 (ciclo 90f)
//   3o golpe carrega o dano e erra  3,59-62   multi-hit f1/f2     1 golpe (soma) no frame 11
//   HP baixo (16.9k Lv30)            0         999                 1648 (Lv30 ~28k ~ Dancer TF 27.5k)
//   range curto                      5         300                 350 (Dancer TF 330)
//   "Very expensive"                 6         1560                1200 (Cap.2 1800 < Dancer 2250)
//
// Ciclo de ataque = col4*2 + ultimo foreswing - 1 (bate com a wiki nas 3
// formas: 210/210/180f). Com 1 golpe so, o ultimo foreswing vira o col13
// (11f nas 3 formas) -> col4 40 = 90f (3s) em todas; DPS igual ao de antes,
// mas o dano sai no frame 11 em vez do 35 e nao some quando os golpes 1-2
// empurram o inimigo.
//
// Leitores do struct (build 338b0601): multi-hit por tabela de colunas
// por golpe — ATK (3,59,60) 0x1f8310 lida em 0x872584/0x87270c, foreswing
// (13,61,62) 0x1f8328 em 0x872b4c, ability (63,64,65) 0x1f8340 em 0x8734bc;
// range 0x872cc0. Custo: 0x793f44 multiplica o col6 por 100 logo apos o
// load (1560 -> 156000), entao escala proporcional (vale antes ou depois).
// Warp immunity fica: contestada (3 fontes acham que atrapalha, outras a
// chamam de unica vantagem).
//
// Sem hook: o 01_mechabun ja engancha o loader de unit CSV (Dobby recusa
// hook duplo e o AOB some depois do 1o hook). Aqui uma thread le o struct
// do #132 via getter do singleton (0x601f5c: adrp+add+ret, puro) e so
// escreve quando os valores vanilla batem; se o jogo recarregar os dados,
// reaplica. Leitores provados (build 338b0601): col1 0x872290, col2
// 0x872338, col4 0x872c7c, col12 0x8749ac.
#include <cstdint>
#include <cstdio>
#include <pthread.h>
#include <unistd.h>
#include "bc_mod_api.h"

#define UNIT_ID 132
#define UNIT_ID_TABLE_OFFSET 2
#define UNIT_BLOCK_STRIDE 0x760
#define FORM_STRIDE 0x1d8
#define STAT_BLOCK_OFF 0x9e318
#define N_FORMS 3

#define COL_HP 0
#define COL_KB_COUNT 1
#define COL_SPEED 2
#define COL_ATK 3
#define COL_ATTACK_INTERVAL 4
#define COL_RANGE 5
#define COL_COST 6
#define COL_AREA 12
#define COL_FORESWING1 13
#define COL_ATK2 59
#define COL_ATK3 60
#define COL_FORESWING2 61
#define COL_FORESWING3 62
#define COL_USE_ABILITY2 64
#define COL_USE_ABILITY3 65

// Getter do singleton P (0x601f5c): adrp x0,#0xb71000 / add x0,x0,#0x320 / ret.
// Unico no binario (contagem = 1).
static const uint8_t kGetterPattern[] = {
    0x80, 0x2b, 0x00, 0x90, 0x00, 0x80, 0x0c, 0x91, 0xc0, 0x03, 0x5f, 0xd6,
};
static const uint8_t kGetterMask[sizeof(kGetterPattern)] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};

typedef long (*singleton_fn)();
static singleton_fn g_singleton = nullptr;
static const bc_mod_api *g_api = nullptr;
static bool g_started = false;
static pthread_mutex_t g_register_mutex = PTHREAD_MUTEX_INITIALIZER;

static inline int32_t *field(long form_base, int col) {
    return reinterpret_cast<int32_t *>(form_base + (long)col * 4);
}

static long form_base(long p, int form) {
    return p + (long)(UNIT_ID + UNIT_ID_TABLE_OFFSET) * UNIT_BLOCK_STRIDE +
           STAT_BLOCK_OFF + (long)form * FORM_STRIDE;
}

// unit133.csv (EN 15.5.0): HP 999 em todas, ATK 1280 (f0) e 370 (f2).
static bool is_vanilla(long p) {
    return *field(form_base(p, 0), COL_HP) == 999 &&
           *field(form_base(p, 0), COL_ATK) == 1280 &&
           *field(form_base(p, 2), COL_ATK) == 370 &&
           *field(form_base(p, 2), COL_AREA) == 0;
}

static void scale_field(long fb, int col, int num, int den) {
    int32_t *f = field(fb, col);
    *f = (int32_t)((int64_t)*f * num / den);
}

static void apply(long p) {
    for (int form = 0; form < N_FORMS; form++) {
        long fb = form_base(p, form);
        *field(fb, COL_SPEED) = 10;
        *field(fb, COL_KB_COUNT) = 3;
        *field(fb, COL_AREA) = 1;
        *field(fb, COL_ATTACK_INTERVAL) = 40;
        scale_field(fb, COL_HP, 1648, 999);
        scale_field(fb, COL_RANGE, 350, 300);
        scale_field(fb, COL_COST, 1200, 1560);
        // Multi-hit -> 1 golpe com o dano somado, no foreswing do golpe 1.
        if (*field(fb, COL_ATK2) > 0) {
            *field(fb, COL_ATK) += *field(fb, COL_ATK2) + *field(fb, COL_ATK3);
            *field(fb, COL_ATK2) = 0;
            *field(fb, COL_ATK3) = 0;
            *field(fb, COL_FORESWING2) = 0;
            *field(fb, COL_FORESWING3) = 0;
            *field(fb, COL_USE_ABILITY2) = 0;
            *field(fb, COL_USE_ABILITY3) = 0;
        }
    }
}

static void *watch(void *) {
    int applied = 0;
    for (;;) {
        long p = g_singleton();
        if (is_vanilla(p)) {
            apply(p);
            applied++;
            long tf = form_base(p, 2);
            char buf[256];
            snprintf(buf, sizeof(buf),
                     "[kungfux] #132 patch aplicado (#%d): speed 10, KB 3, area, "
                     "ciclo 90f, 1 golpe (TF atk %d, fs %d), HP %d, range %d, custo %d",
                     applied, *field(tf, COL_ATK), *field(tf, COL_FORESWING1), *field(tf, COL_HP),
                     *field(tf, COL_RANGE), *field(tf, COL_COST));
            g_api->log(BC_LOG_INFO, buf);
        }
        sleep(1);
    }
    return nullptr;
}

extern "C" BC_MOD_EXPORT bool bc_mod_register(const bc_mod_api *api) {
    pthread_mutex_lock(&g_register_mutex);
    bool ok = g_started;  // hot-reload: thread ja roda, nada a refazer
    if (!ok && api != nullptr && api->log != nullptr && api->version >= 2 &&
        api->resolve_pattern != nullptr) {
        g_api = api;
        g_singleton = reinterpret_cast<singleton_fn>(
            api->resolve_pattern(kGetterPattern, kGetterMask, sizeof(kGetterPattern)));
        pthread_t t;
        if (g_singleton == nullptr) {
            api->log(BC_LOG_WARN,
                     "[kungfux] getter do singleton nao achado (build mudou?) - mod inativo");
        } else if (pthread_create(&t, nullptr, watch, nullptr) != 0) {
            api->log(BC_LOG_ERROR, "[kungfux] pthread_create falhou - mod inativo");
        } else {
            pthread_detach(t);
            g_started = ok = true;
            api->log(BC_LOG_INFO, "[kungfux] ativo, aguardando unit133.csv carregar");
        }
    }
    pthread_mutex_unlock(&g_register_mutex);
    return ok;
}
