// bc_hook_logic.h — Núcleo puro do multi-hook dispatcher (Prefix/Postfix) +
// decisão de estado unpatch/repatch. Sem dependência de Dobby, logcat ou
// Android (só stdint/string/stdbool) para rodar no selftest_harness (host)
// com o MESMO código do device — o main.cpp DELEGA a este header, então o
// harness testa a lógica real, não um mock paralelo (gap que o kilo achou).
//
// Design (padrão Harmony sobre o Dobby):
//   - Dobby troca a função inteira (1 hook = 1 replacement).
//   - hook_std() é o único ponto por onde toda chamada original passa e vira
//     o dispatcher: Prefix roda antes do orig (pode cancelar), Postfix roda
//     depois (observa/ajusta retorno). VÁRIOS callbacks por hook, 4 slots.
//
// Contrato igual Harmony: Prefix retorna false → PULA o original (postfix
// ainda roda, vê *called_orig=false). Zero callbacks = passthrough idêntico.
//
// Estado unpatch/repatch: o HookState (backup + resolved_addr) mapeia pros
// campos reais de PLANS[]. As chamadas device-only (DobbyDestroy/DobbyHook)
// ficam atrás de 2 function pointers injetados — main.cpp passa as reais, o
// harness passa stubs. Assim a LÓGICA de decisão é idêntica nos dois.

#ifndef BC_HOOK_LOGIC_H
#define BC_HOOK_LOGIC_H

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HOOK_MAX_CALLBACKS 4
#define BC_HOOK_NAMES_COUNT 4

// --- Callbacks wide (AAPCS64, x0..x7) — idêntico ao real ---
typedef bool (*HookPrefixFn)(const char *name, uintptr_t a0, uintptr_t a1,
                             uintptr_t a2, uintptr_t a3, uintptr_t a4,
                             uintptr_t a5, uintptr_t a6, uintptr_t a7);
typedef void (*HookPostfixFn)(const char *name, uintptr_t a0, uintptr_t a1,
                              uintptr_t a2, uintptr_t a3, uintptr_t a4,
                              uintptr_t a5, uintptr_t a6, uintptr_t a7,
                              uintptr_t *ret, bool called_orig);
// Orig wide (8 args).
typedef uintptr_t (*jnifn_wide_t)(uintptr_t, uintptr_t, uintptr_t, uintptr_t,
                                  uintptr_t, uintptr_t, uintptr_t, uintptr_t);

typedef struct HookCallbacks {
    HookPrefixFn prefix[HOOK_MAX_CALLBACKS];
    int prefix_n;
    HookPostfixFn postfix[HOOK_MAX_CALLBACKS];
    int postfix_n;
} HookCallbacks;

// Nomes fixos (1:1 com PLANS[]). appInit=0, appUpdateDraw=1, appTouch=2,
// appKey=3. Return -1 se desconhecido.
static inline int hook_slot_by_name(const char *name) {
    static const char *const kNames[BC_HOOK_NAMES_COUNT] =
        {"appInit", "appUpdateDraw", "appTouch", "appKey"};
    if (name == nullptr) return -1;
    for (int i = 0; i < BC_HOOK_NAMES_COUNT; i++)
        if (strcmp(kNames[i], name) == 0) return i;
    return -1;
}

// Registro (single-thread, antes de install_all). Retorna true/false.
static inline bool hook_register_prefix_by_slot(HookCallbacks *cbs, int slot,
                                                HookPrefixFn fn) {
    if (cbs == nullptr || fn == nullptr) return false;
    if (slot < 0 || slot >= BC_HOOK_NAMES_COUNT) return false;
    if (cbs[slot].prefix_n >= HOOK_MAX_CALLBACKS) return false;
    cbs[slot].prefix[cbs[slot].prefix_n++] = fn;
    return true;
}
static inline bool hook_register_prefix(HookCallbacks *g_cbs,
                                        const char *name, HookPrefixFn fn) {
    return hook_register_prefix_by_slot(g_cbs, hook_slot_by_name(name), fn);
}
static inline bool hook_register_postfix_by_slot(HookCallbacks *cbs, int slot,
                                                 HookPostfixFn fn) {
    if (cbs == nullptr || fn == nullptr) return false;
    if (slot < 0 || slot >= BC_HOOK_NAMES_COUNT) return false;
    if (cbs[slot].postfix_n >= HOOK_MAX_CALLBACKS) return false;
    cbs[slot].postfix[cbs[slot].postfix_n++] = fn;
    return true;
}
static inline bool hook_register_postfix(HookCallbacks *g_cbs,
                                         const char *name, HookPostfixFn fn) {
    return hook_register_postfix_by_slot(g_cbs, hook_slot_by_name(name), fn);
}

// --- Dispatcher idêntico ao hook_std() do main.cpp (parte de callbacks) ---
// cb pode ser nullptr (slot inválido) → passthrough. orig pode ser nullptr.
// Prefix retorna false → pula orig. Postfix sempre roda e pode ajustar *ret.
// prefix_ns/postfix_ns opcionais: recebem o overhead (o main.cpp soma em
// g_hook_overhead_*; aqui só medimos se passado não-null).
// ATENÇÃO (achado de review forense, não documentado antes): o loop de
// prefixes abaixo usa `&& run_orig` na condição — um prefix que retorna
// false não só pula o orig, também PARA de chamar os prefixes seguintes
// no mesmo slot (curto-circuito). Múltiplos prefixes no mesmo hook não
// são todos garantidos de rodar.
static inline uintptr_t hook_dispatch(const char *name,
                                      const HookCallbacks *cb,
                                      jnifn_wide_t orig,
                                      uintptr_t a0, uintptr_t a1,
                                      uintptr_t a2, uintptr_t a3,
                                      uintptr_t a4, uintptr_t a5,
                                      uintptr_t a6, uintptr_t a7,
                                      long *prefix_ns_out,
                                      long *postfix_ns_out) {
    bool run_orig = true;
    struct timespec ts_start, ts_pre_end, ts_mid, ts_end;
    clock_gettime(CLOCK_MONOTONIC, &ts_start);
    if (cb != nullptr) {
        for (int i = 0; i < cb->prefix_n && run_orig; i++) {
            if (!cb->prefix[i](name, a0, a1, a2, a3, a4, a5, a6, a7))
                run_orig = false;
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &ts_pre_end);
    uintptr_t ret = 0;
    if (run_orig && orig != nullptr) ret = orig(a0, a1, a2, a3, a4, a5, a6, a7);
    clock_gettime(CLOCK_MONOTONIC, &ts_mid);
    if (cb != nullptr) {
        for (int i = 0; i < cb->postfix_n; i++) {
            cb->postfix[i](name, a0, a1, a2, a3, a4, a5, a6, a7, &ret, run_orig);
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &ts_end);
    if (prefix_ns_out != nullptr)
        *prefix_ns_out = (ts_pre_end.tv_sec - ts_start.tv_sec) * 1000000000L +
                         (ts_pre_end.tv_nsec - ts_start.tv_nsec);
    if (postfix_ns_out != nullptr)
        *postfix_ns_out = (ts_end.tv_sec - ts_mid.tv_sec) * 1000000000L +
                          (ts_end.tv_nsec - ts_mid.tv_nsec);
    return ret;
}

// --- Estado p/ unpatch/repatch (mapeia pros campos reais de PLANS[]) ---
typedef struct HookState {
    void *backup;        // trampoline Dobby (NULL = não instalado)
    void *resolved_addr; // endereço real resolvido (NULL = nunca resolveu)
} HookState;

typedef int (*HookDestroyFn)(void *addr);
typedef int (*HookInstallFn)(void *target, void *replacement, void **backup);

// unpatch (lógica real): restaura bytes e zera backup, mantém resolved.
// Retorno: 1=desativado agora, 0=idempotente (já desativado),
// -1=erro (slot inválido / sem resolved / destroy falhou).
static inline int bc_unpatch_hook(HookState *states, const char *shortname,
                                  HookDestroyFn destroy) {
    int idx = hook_slot_by_name(shortname);
    if (idx < 0) return -1;
    HookState *s = &states[idx];
    if (s->backup == nullptr) return 0;           // idempotente
    if (s->resolved_addr == nullptr) return -1;   // sem endereço (anomaly)
    if (destroy != nullptr && destroy(s->resolved_addr) != 0) return -1;
    s->backup = nullptr;                          // zera trampoline
    // resolved_addr NÃO zerado → "unpatched" (foi ativo), não "no-target"
    return 1;
}

// repatch (lógica real): reinstala o hook num endereço-alvo explícito.
// `target` é OBRIGATÓRIO (endereço real já resolvido pelo caller — símbolo,
// pattern scan, ou s->resolved_addr sobrevivente de um unpatch anterior no
// mesmo slot). Sem isso a função não tem como descobrir um endereço válido
// sozinha; um placeholder aqui dentro seria só um SIGSEGV adiado pro dia em
// que alguém ligar isto a um install() de verdade (Dobby).
// Retorno: 1=instalado, 0=já ativo, -1=erro (slot inválido / sem target /
// install falhou).
static inline int bc_repatch_hook(HookState *states, const char *shortname,
                                  HookInstallFn install, void *target,
                                  void *replacement, void **backup_storage) {
    int idx = hook_slot_by_name(shortname);
    if (idx < 0) return -1;
    HookState *s = &states[idx];
    if (s->backup != nullptr) return 0;           // já ativo
    if (target == nullptr) return -1;             // sem endereço válido pra reinstalar
    void *new_backup = nullptr;
    if (install != nullptr &&
        install(target, replacement, &new_backup) != 0) return -1;
    s->backup = new_backup;
    s->resolved_addr = target;
    if (backup_storage != nullptr) *backup_storage = new_backup;
    return 1;
}

#ifdef __cplusplus
}
#endif

#endif // BC_HOOK_LOGIC_H