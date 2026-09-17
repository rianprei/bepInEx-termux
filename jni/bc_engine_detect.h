// bc_engine_detect.h — detecta se o processo atual roda engine Cocos2d-x,
// SEM saber o nome do jogo de antemão. Parte da generalização pedida pelo
// usuário (2026-09-16): "bepin-termux tem que se adaptar ao máximo a
// qualquer jogo Cocos2d-x, não importa o jogo".
//
// Cascata de 3 sinais (pesquisa real hermes, clone de cocos2d/cocos2d-x
// branch v4, arquivo:linha citados):
//
//   Sinal 1 — lib do MOTOR presente (mais forte, mas falha em builds
//   amalgamados): cocos/CMakeLists.txt:110 gera `libcocos2d.so` (CMake,
//   default atual); builds antigos (Android.mk 3.x) geram
//   `libcocos2dcpp.so`/`libcocos2dlua.so`; alguns forks usam
//   `libcocos2dx.so`. NÃO cobre builds que linkam o motor estaticamente
//   DENTRO do .so do próprio jogo (caso Battle Cats — libnative-lib.so).
//
//   Sinal 2 — símbolo C++ `cocos2d::` por substring, em QUALQUER lib
//   carregada (robusto — cobre até o amalgamado). Confirmado no clone:
//   Java_org_cocos2dx_lib_Cocos2dxRenderer.cpp:42 chama
//   `cocos2d::Director::getInstance()->mainLoop()` — símbolo interno
//   sobrevive em release sem strip total.
//
//   Sinal 3 — JNI stock `Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeRender`
//   (ou nativeTouches*/nativeKeyEvent) presente — só existe se o jogo usa a
//   Activity padrão do engine (org.cocos2dx.lib.Cocos2dxActivity), não uma
//   Activity própria (Battle Cats sobrescreve com MyActivity — esse sinal
//   NÃO bate pra ele, cobre outros jogos que mantêm a Activity stock).
//
// Ordem: 1 → 2 → 3 (mais barato/específico primeiro). Qualquer sinal
// positivo já classifica como Cocos2d-x. Nenhum bater → NÃO detectado
// (não hooka nada, fail-safe — não é erro, pode ser engine própria tipo a
// da PONOS, ou qualquer coisa não-Cocos2d-x).
//
// LIMITE HONESTO: esta cascata detecta A PRESENÇA do engine, não GARANTE
// que existam hooks úteis pra instalar — isso depende de
// bc_elf_symtab_scan_lib achar símbolos Java_* (ver limite do
// RegisterNatives documentado em bc_elf_symtab.h).

#ifndef BC_ENGINE_DETECT_H
#define BC_ENGINE_DETECT_H

#include <stdbool.h>
#include <string.h>
#include "bc_elf_symtab.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BC_ENGINE_UNKNOWN = 0,
    BC_ENGINE_COCOS2DX_LIB = 1,       // sinal 1: lib do motor presente
    BC_ENGINE_COCOS2DX_SYMBOL = 2,    // sinal 2: symbol cocos2d:: achado (amalgamado)
    BC_ENGINE_COCOS2DX_STOCK_JNI = 3, // sinal 3: Activity stock do engine
    BC_ENGINE_GENERIC_NATIVE = 4,     // fallback: nenhum sinal Cocos2d-x bateu,
                                       // mas alguma lib exporta símbolo Java_*
                                       // — qualquer jogo/app C++/JNI nativo,
                                       // motor desconhecido, cai aqui.
} bc_engine_signal;

#ifdef __cplusplus
}
#endif

#ifdef __ANDROID__
#include <link.h>

// Nomes reais de lib do motor Cocos2d-x confirmados no clone oficial
// (branch v4) + variantes de versões antigas — sinal 1 da cascata.
static const char *const BC_COCOS2DX_LIB_NAMES[] = {
    "libcocos2d.so",     // CMake atual (cocos/CMakeLists.txt:110)
    "libcocos2dcpp.so",  // Android.mk 3.x
    "libcocos2dlua.so",  // binding Lua
    "libcocos2dx.so",    // alguns forks
    nullptr,
};

static inline bool bc_cocos2dx_filter_symbol(const char *name, size_t name_len) {
    // substring "cocos2d::" em qualquer posição — símbolo C++ mangled
    // sempre contém o namespace literal (ex.: _ZN7cocos2d8Director...).
    // Busca por substring simples de string demangled seria frágil (nomes
    // mangled não têm "::" literal) — na prática o achado real (hermes)
    // confirma que builds sem strip total preservam o nome demangled em
    // debug info OU exportam via extern "C" wrappers com "cocos2d" no nome;
    // aqui usamos o teste mais barato e honesto: substring "cocos2d" no
    // símbolo (cobre mangled _ZN7cocos2d... e wrappers extern "C").
    (void)name_len;
    return strstr(name, "cocos2d") != nullptr;
}

static inline void bc_engine_detect_noop_cb(const char *, uint64_t, void *) {}

typedef struct bc_lib_name_ctx {
    const char *want_name;
    bool found;
} bc_lib_name_ctx;

static inline int bc_lib_name_phdr_cb(struct dl_phdr_info *info, size_t, void *data) {
    bc_lib_name_ctx *ctx = (bc_lib_name_ctx *)data;
    if (info->dlpi_name != nullptr && strstr(info->dlpi_name, ctx->want_name) != nullptr) {
        ctx->found = true;
        return 1;  // achou — para de iterar
    }
    return 0;
}

// Sinal 1: varre libs carregadas por nome conhecido do motor.
static inline bool bc_detect_cocos2dx_lib(void) {
    for (int i = 0; BC_COCOS2DX_LIB_NAMES[i] != nullptr; i++) {
        bc_lib_name_ctx ctx = {BC_COCOS2DX_LIB_NAMES[i], false};
        dl_iterate_phdr(bc_lib_name_phdr_cb, &ctx);
        if (ctx.found) return true;
    }
    return false;
}

// Sinal 2: substring "cocos2d" em símbolo exportado de QUALQUER lib
// carregada (image_name=nullptr → escaneia todas) — cobre build
// amalgamado (motor linkado estático dentro do .so do jogo, caso Battle
// Cats). Mais caro que sinal 1 (varre símbolo de cada lib), por isso vem
// depois na cascata.
static inline bool bc_detect_cocos2dx_symbol(void) {
    bool lib_found = false;
    int n = bc_elf_symtab_scan_lib_filtered(nullptr, bc_cocos2dx_filter_symbol,
                                             bc_engine_detect_noop_cb, nullptr, &lib_found);
    return n > 0;
}

// Sinal 3: JNI stock do engine (só existe se o jogo usa Activity padrão,
// não uma Activity própria tipo MyActivity do Battle Cats).
static inline bool bc_cocos2dx_filter_stock_jni(const char *name, size_t name_len) {
    static const char *const kStock = "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeRender";
    size_t kLen = strlen(kStock);
    return name_len == kLen && strcmp(name, kStock) == 0;
}

static inline bool bc_detect_cocos2dx_stock_jni(void) {
    bool lib_found = false;
    int n = bc_elf_symtab_scan_lib_filtered(nullptr, bc_cocos2dx_filter_stock_jni,
                                             bc_engine_detect_noop_cb, nullptr, &lib_found);
    return n > 0;
}

// Cascata Cocos2d-x — roda os 3 sinais em ordem de custo, retorna o
// primeiro que bater (ou BC_ENGINE_UNKNOWN se nenhum bateu).
static inline bc_engine_signal bc_detect_cocos2dx(void) {
    if (bc_detect_cocos2dx_lib()) return BC_ENGINE_COCOS2DX_LIB;
    if (bc_detect_cocos2dx_symbol()) return BC_ENGINE_COCOS2DX_SYMBOL;
    if (bc_detect_cocos2dx_stock_jni()) return BC_ENGINE_COCOS2DX_STOCK_JNI;
    return BC_ENGINE_UNKNOWN;
}

// Fallback genérico: nenhum sinal Cocos2d-x bateu, mas o app é nativo
// C++/JNI de QUALQUER motor (Unreal, engine própria, etc.) se alguma lib
// carregada exportar pelo menos um símbolo Java_*. bc_generic_hook.h já é
// engine-agnóstico (hooka por símbolo, não por motor) — esse sinal só
// autoriza a entrada pro pipeline de hook genérico mesmo sem reconhecer o
// motor.
//
// LIMITE HONESTO (mesmo de bc_elf_symtab.h): app que registra tudo via
// RegisterNatives() em vez de exportar Java_* não tem símbolo nenhum pra
// achar — continua indetectável por qualquer sinal, cai em UNKNOWN mesmo
// sendo C++ de verdade. "Sem erros" aqui significa nunca crasha (DORMANT),
// não significa "sempre acha algo pra hookar".
static inline bool bc_detect_generic_native(void) {
    bool lib_found = false;
    int n = bc_elf_symtab_scan_lib(nullptr, bc_engine_detect_noop_cb, nullptr, &lib_found);
    return n > 0;
}

// Cascata completa — tenta reconhecer o motor primeiro (Cocos2d-x, sinal
// mais específico), e só se nenhum bater cai no fallback genérico
// (qualquer C++/JNI nativo). Retorna o primeiro que bater, ou
// BC_ENGINE_UNKNOWN se nem símbolo Java_* existir.
static inline bc_engine_signal bc_detect_engine(void) {
    bc_engine_signal sig = bc_detect_cocos2dx();
    if (sig != BC_ENGINE_UNKNOWN) return sig;
    if (bc_detect_generic_native()) return BC_ENGINE_GENERIC_NATIVE;
    return BC_ENGINE_UNKNOWN;
}

#endif // __ANDROID__

#endif // BC_ENGINE_DETECT_H
