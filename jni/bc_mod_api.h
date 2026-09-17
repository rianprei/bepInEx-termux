// bc_mod_api.h — Contrato de API exposto pelo loader do bepin-termux a cada
// mod .so dinâmico (o "plugin system" do projeto). Este é o header que um
// autor de mod inclui; o loader (main.cpp) implementa o struct e passa por
// ponteiro ao entry point do mod.
//
// DECISÃO DE NOMENCLATURA: o entry point exportado é `bc_mod_register`, NÃO
// `bc_mod_entry`. Razão técnica real: `struct bc_mod_entry` já existe em
// bc_mods_conf.h (as entradas tipadas do config), e em C++ os nomes de tag
// de struct vivem no mesmo namespace que funções — declarar uma função
// `bc_mod_entry` colidiria com o tipo `struct bc_mod_entry` (redefinition).
// Símbolo exportado = `bc_mod_register(const bc_mod_api*)`, sem mangling (C
// linkage), resolvido via dlsym normal.
//
// O que cada mod .so exporta:
//   extern "C" bool bc_mod_register(const bc_mod_api *api);
//   - Deve chamar api->register_prefix/register_postfix/resolve_symbol/log
//     durante a execução pra instalar seus hooks.
//   - Retorna true se carregou normalmente; false ou retorno imediato é
//     aceito (o loader trata como "mod inativo", nunca derruba o jogo).
//
// A API é const: o mod NÃO pode reescrever os callbacks do loader.

#ifndef BC_MOD_API_H
#define BC_MOD_API_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BC_MOD_API_VERSION 2

// Callbacks que um mod registra num hook nomeado (mesmos contratos do
// dispatcher Harmony-like, ver bc_hook_logic.h). Prefix retorna false →
// pula o original; Postfix pode ajustar o retorno via *ret.
typedef bool (*bc_prefix_fn)(const char *hook, uintptr_t a0, uintptr_t a1,
                             uintptr_t a2, uintptr_t a3, uintptr_t a4,
                             uintptr_t a5, uintptr_t a6, uintptr_t a7);
typedef void (*bc_postfix_fn)(const char *hook, uintptr_t a0, uintptr_t a1,
                              uintptr_t a2, uintptr_t a3, uintptr_t a4,
                              uintptr_t a5, uintptr_t a6, uintptr_t a7,
                              uintptr_t *ret, bool called_orig);

// Níveis de log BepInEx-style (espelho LogLevel, subconjunto).
typedef enum {
    BC_LOG_INFO = 0,
    BC_LOG_WARN,
    BC_LOG_ERROR,
} bc_log_level;

// A API que o mod recebe. Todos os ponteiros são preenchidos pelo loader;
// o mod NUNCA deve liberar/chamar-destroy neles (são do processo do jogo).
typedef struct bc_mod_api {
    // Versão do contrato (BC_MOD_API_VERSION). O mod DEVE checar; loader com
    // versão maior é compatível (novos campos no fim, zero-initializados).
    unsigned version;

    // Registra um callback prefix/postfix num hook nomeado
    // ("appInit"/"appUpdateDraw"/"appTouch"/"appKey"). Retorna false se o
    // nome é inválido ou os slots do hook estão cheios (HOOK_MAX_CALLBACKS).
    bool (*register_prefix)(const char *hook, bc_prefix_fn fn);
    bool (*register_postfix)(const char *hook, bc_postfix_fn fn);

    // Resolve um endereço dentro do libnative-lib.so do alvo (equivale ao
    // DobbySymbolResolver, mas exposto pro mod). Retorna nullptr se não achar.
    void *(*resolve_symbol)(const char *sym);

    // Loga uma linha (logcat + streaming BepInEx-style pro Termux). msg
    // é cópia no acto — o mod pode reusar o buffer depois.
    void (*log)(bc_log_level level, const char *msg);

    // Resolve endereço por AOB (array-of-bytes) scan em vez de RVA/symbol
    // fixo — ver bc_pattern_scan.h. Campo NOVO no fim: mod antigo compilado
    // contra versão 1 da API funciona igual (o loader zero-inicializa esse
    // ponteiro se o mod não souber dele; um mod que TENTAR chamar precisa
    // checar version>=2 antes). pattern_bytes/mask: BC_MOD_PATTERN_MAX_BYTES
    // (mesmo limite de bc_pattern.h). Retorna nullptr se 0 ou 2+ matches
    // (ambiguidade é falha explícita, nunca "pega o primeiro").
    void *(*resolve_pattern)(const uint8_t *pattern_bytes, const uint8_t *mask, size_t len);
} bc_mod_api;

// Assinatura do entry point exportado por CADA mod .so.
typedef bool (*bc_mod_register_fn)(const bc_mod_api *api);

#ifdef __cplusplus
}
#endif

#endif // BC_MOD_API_H