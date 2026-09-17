// bc_generic_hook.h — instala hook genérico de LOG (não de replace) em
// símbolo Java_* descoberto em runtime, sem conhecer assinatura de
// antemão. Peça final da generalização Cocos2d-x pedida pelo usuário
// (2026-09-16): "detecta todos mais so atua no que voce selecionar".
//
// Mecanismo: DobbyInstrument(addr, callback) — Dobby real, dobby.h:127.
// Isso NÃO substitui a função (diferente de DobbyHook usado no caminho
// Battle-Cats-specific) — injeta um callback ANTES da execução original,
// recebe o contexto de registrador, e a função original roda normal
// depois. Sem isso, um hook genérico teria que chamar a função original
// através de um trampolim com assinatura desconhecida (contagem de args,
// tipos float vs int em registrador separado no ARM64 AAPCS64) — arriscado
// de quebrar ABI e crashar o processo. DobbyInstrument não tem esse risco
// porque nunca precisa reconstruir a chamada original.
//
// LIMITE HONESTO: callback só loga que a função foi chamada (endereço +
// símbolo), não decodifica argumentos (exigiria assinatura conhecida por
// símbolo, que é exatamente o que não temos aqui). Throttle por símbolo
// evita flood de log em hot path (ex.: nativeRender chamado por frame).

#ifndef BC_GENERIC_HOOK_H
#define BC_GENERIC_HOOK_H

#include <stdint.h>
#include <stdio.h>
#include <atomic>
#include "dobby.h"
#include "bc_elf_symtab.h"

#ifdef __ANDROID__

#define BC_GENERIC_HOOK_MAX 8
#define BC_GENERIC_HOOK_LOG_EVERY 500  // loga 1a chamada + 1 a cada N depois (throttle hot path)

typedef void (*bc_generic_log_fn)(const char *symbol, uint64_t call_count);

struct bc_generic_hook_slot {
    char symbol[128];
    std::atomic<uint32_t> call_count{0};
};

static bc_generic_hook_slot g_generic_slots[BC_GENERIC_HOOK_MAX];
static int g_generic_slot_count = 0;
static bc_generic_log_fn g_generic_log_cb = nullptr;

// Callback fábrica: cada slot precisa de callback PRÓPRIO porque
// DobbyInstrument não repassa contexto do usuário — geramos 8 funções
// estáticas distintas (BC_GENERIC_HOOK_MAX fixo) via macro.
#define BC_GENERIC_CB(N)                                                     \
    static void bc_generic_cb_##N(void *address, DobbyRegisterContext *ctx) { \
        (void)address; (void)ctx;                                            \
        uint32_t n = g_generic_slots[N].call_count.fetch_add(1, std::memory_order_relaxed) + 1; \
        if (n == 1 || n % BC_GENERIC_HOOK_LOG_EVERY == 0) {                   \
            if (g_generic_log_cb) g_generic_log_cb(g_generic_slots[N].symbol, n); \
        }                                                                     \
    }

BC_GENERIC_CB(0)
BC_GENERIC_CB(1)
BC_GENERIC_CB(2)
BC_GENERIC_CB(3)
BC_GENERIC_CB(4)
BC_GENERIC_CB(5)
BC_GENERIC_CB(6)
BC_GENERIC_CB(7)

static dobby_instrument_callback_t g_generic_cbs[BC_GENERIC_HOOK_MAX] = {
    bc_generic_cb_0, bc_generic_cb_1, bc_generic_cb_2, bc_generic_cb_3,
    bc_generic_cb_4, bc_generic_cb_5, bc_generic_cb_6, bc_generic_cb_7,
};

struct bc_generic_install_ctx {
    int installed;
};

static inline void bc_generic_install_symbol_cb(const char *name, uint64_t addr, void *user) {
    bc_generic_install_ctx *ctx = (bc_generic_install_ctx *)user;
    if (g_generic_slot_count >= BC_GENERIC_HOOK_MAX) return;
    int slot = g_generic_slot_count;
    snprintf(g_generic_slots[slot].symbol, sizeof(g_generic_slots[slot].symbol), "%s", name);
    int rc = DobbyInstrument((void *)addr, g_generic_cbs[slot]);
    if (rc == 0) {
        g_generic_slot_count++;
        ctx->installed++;
    }
    // rc != 0 → símbolo específico fica DORMANT (sem log), resto da
    // cascata continua tentando os outros achados — fail-safe por símbolo,
    // não por processo inteiro.
}

// Instala hook genérico de log em até BC_GENERIC_HOOK_MAX símbolos Java_*
// achados em TODAS as libs carregadas do processo (não sabemos qual lib é
// "a" nativa do jogo genérico, ao contrário do Battle Cats onde
// TARGET_LIB é fixo). Retorna quantos foram instalados com sucesso.
static inline int bc_generic_hook_install_all(bc_generic_log_fn log_cb) {
    g_generic_log_cb = log_cb;
    g_generic_slot_count = 0;
    bc_generic_install_ctx ctx{0};
    bool lib_found = false;
    bc_elf_symtab_scan_lib(nullptr, bc_generic_install_symbol_cb, &ctx, &lib_found);
    return ctx.installed;
}

#endif // __ANDROID__

#endif // BC_GENERIC_HOOK_H
