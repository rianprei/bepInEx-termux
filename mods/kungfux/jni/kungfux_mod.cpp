// kungfux_mod.cpp — Kung Fu Cat X (#132): remove os cons da wiki
// (https://battle-cats.fandom.com/wiki/Kung_Fu_Cat_X_(Rare_Cat)) sem mexer
// nos pros. Plano e contas: context/kungfu-cat-x-132-plano.md (vault).
//
//   con wiki                        col  vanilla (f0/f1/f2)   patch
//   "Slow ... movement speed"        2   8                    10 (mediana Rare)
//   "One knockback"                  1   1                    3  (mediana Rare)
//   "Single Attack"                 12   0                    1  (area)
//   "Slow attack rate"               4   100/88/73            40/28/28
//   "Very expensive"                 6   1560                 -- (sem leitor no struct)
//
// Ciclo de ataque = col4*2 + ultimo foreswing - 1 (bate com a wiki nas 3
// formas: 210/210/180f). Alvo 90f (3s): f0 foreswing 11 -> col4 40;
// f1/f2 ultimo foreswing 35 -> col4 28.
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
#define COL_AREA 12

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

static void apply(long p) {
    static const int32_t interval[N_FORMS] = {40, 28, 28};
    for (int form = 0; form < N_FORMS; form++) {
        long fb = form_base(p, form);
        *field(fb, COL_SPEED) = 10;
        *field(fb, COL_KB_COUNT) = 3;
        *field(fb, COL_AREA) = 1;
        *field(fb, COL_ATTACK_INTERVAL) = interval[form];
    }
}

static void *watch(void *) {
    int applied = 0;
    for (;;) {
        long p = g_singleton();
        if (is_vanilla(p)) {
            apply(p);
            applied++;
            char buf[160];
            snprintf(buf, sizeof(buf),
                     "[kungfux] #132 patch aplicado (#%d): speed 10, KB 3, area, "
                     "ciclo 90f (col4 40/28/28)", applied);
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
