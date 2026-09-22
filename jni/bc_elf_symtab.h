// bc_elf_symtab.h — enumeração de símbolos dinâmicos (.dynsym) de uma lib
// já carregada em memória, sem dlopen/leitura de arquivo. Parte da
// generalização do bepInEx-termux pra "qualquer jogo Cocos2d-x" (pedido do
// usuário, 2026-09-16): hoje hooka por NOME de símbolo hardcoded
// (Java_jp_co_ponos_battlecats_MyActivity_appInit); pra jogo desconhecido
// não dá pra saber o nome de antemão — a única saída é LISTAR os símbolos
// que o binário exporta e filtrar os que parecem JNI (prefixo "Java_").
//
// Mecanismo confirmado real (pesquisa kilo, 2026-09-16): é exatamente o que
// o Frida faz internamente (gumelfmodule.c) pra Module.enumerateExports() —
// parseia o dynamic symbol table diretamente da memória mapeada
// (RTLD_NOLOAD só confirma que já está carregado, nunca dlopen de verdade),
// filtra por STB_GLOBAL/STB_WEAK + STT_FUNC (evita símbolo local/undefined).
// Não reinventa: mesmo padrão de ferramenta madura.
//
// LIMITE HONESTO (achado real freebuff, 2026-09-16, docs.android.com/
// training/articles/perf-jni): muitos apps registram JNI via
// `RegisterNatives()` dentro de `JNI_OnLoad()` em vez de depender da
// convenção de nome `Java_pkg_Classe_metodo`. Nesse caso o método pode ter
// QUALQUER nome interno (até `static`/sem export nenhum) — não aparece no
// dynsym, este scanner não acha NADA, e o hook cai em DORMANT (nunca
// crasha, só não ativa). Não tem workaround sem hookar o próprio
// `RegisterNatives`/`JNI_OnLoad` (fora de escopo aqui — candidato a fase
// futura, não implementado).
//
// Testabilidade: bc_elf_symtab_scan (núcleo puro) opera sobre ponteiros já
// resolvidos em memória (symtab/strtab/count) — testável no host com dados
// sintéticos. A camada de device (bc_elf_symtab_scan_lib) resolve esses
// ponteiros a partir de PT_DYNAMIC via dl_iterate_phdr, incluindo o parse
// de DT_GNU_HASH pra achar a contagem de símbolos (Android/NDK moderno usa
// --hash-style=gnu por padrão, DT_HASH clássico geralmente ausente).

#ifndef BC_ELF_SYMTAB_H
#define BC_ELF_SYMTAB_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

// Layout mínimo do Elf64_Sym (evita depender de <elf.h> no core testável —
// mesmo valor de bits em qualquer libc/plataforma que define ELFCLASS64).
typedef struct bc_elf64_sym {
    uint32_t st_name;
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
} bc_elf64_sym;

#define BC_ELF_STB(info)  ((info) >> 4)
#define BC_ELF_STT(info)  ((info) & 0xf)
#define BC_ELF_STB_GLOBAL 1
#define BC_ELF_STB_WEAK   2
#define BC_ELF_STT_FUNC   2

#define BC_JNI_PREFIX "Java_"
#define BC_JNI_PREFIX_LEN 5

// Callback por símbolo achado. `name` aponta pro strtab (não copiado —
// válido só durante a chamada). `addr` = st_value + load_bias já somado
// pelo chamador de bc_elf_symtab_scan_lib (bruto, sem bias, na versão pura).
typedef void (*bc_elf_symtab_cb)(const char *name, uint64_t addr, void *user);

// Predicado de filtro por nome (já validado bounds-safe pelo chamador —
// `name` é sempre terminado em '\0' dentro do espaço válido do strtab).
typedef bool (*bc_elf_symtab_name_filter)(const char *name, size_t name_len);

static inline bool bc_elf_filter_jni_prefix(const char *name, size_t name_len) {
    return name_len >= BC_JNI_PREFIX_LEN &&
           strncmp(name, BC_JNI_PREFIX, BC_JNI_PREFIX_LEN) == 0;
}

// Núcleo puro genérico: itera `symtab[0..sym_count)`, filtra STB_GLOBAL/WEAK
// + STT_FUNC + `filter(name)`, chama `cb` por cada um. Retorna quantos
// símbolos passaram no filtro (0 é resultado válido — símbolo pode não
// existir, não é erro). Bounds-safe: nunca lê symtab[i] se i >= sym_count,
// nunca lê strtab além de strtab_size a partir de st_name (nome inválido é
// pulado, não crasha).
static inline int bc_elf_symtab_scan_filtered(const bc_elf64_sym *symtab, size_t sym_count,
                                               const char *strtab, size_t strtab_size,
                                               bc_elf_symtab_name_filter filter,
                                               bc_elf_symtab_cb cb, void *user) {
    if (symtab == nullptr || strtab == nullptr || cb == nullptr || filter == nullptr) return 0;
    int found = 0;
    // índice 0 é sempre o símbolo nulo reservado (ELF spec) — pula.
    for (size_t i = 1; i < sym_count; i++) {
        const bc_elf64_sym *s = &symtab[i];
        int bind = BC_ELF_STB(s->st_info);
        int type = BC_ELF_STT(s->st_info);
        if (type != BC_ELF_STT_FUNC) continue;
        if (bind != BC_ELF_STB_GLOBAL && bind != BC_ELF_STB_WEAK) continue;
        if (s->st_name == 0 || s->st_name >= strtab_size) continue;  // nunca lê fora do strtab
        const char *name = strtab + s->st_name;
        // strnlen defensivo: nome sem terminador dentro de strtab_size
        // seria leitura fora dos limites — checa antes de qualquer strcmp.
        size_t max_len = strtab_size - s->st_name;
        size_t name_len = strnlen(name, max_len);
        if (name_len >= max_len) continue;  // sem '\0' encontrado no espaço válido
        if (!filter(name, name_len)) continue;
        cb(name, s->st_value, user);
        found++;
    }
    return found;
}

// Atalho: mesmo núcleo, filtro fixo em prefixo "Java_" (convenção JNI).
static inline int bc_elf_symtab_scan(const bc_elf64_sym *symtab, size_t sym_count,
                                      const char *strtab, size_t strtab_size,
                                      bc_elf_symtab_cb cb, void *user) {
    return bc_elf_symtab_scan_filtered(symtab, sym_count, strtab, strtab_size,
                                        bc_elf_filter_jni_prefix, cb, user);
}

#ifdef __cplusplus
}
#endif

// ---- Camada de device (dl_iterate_phdr + PT_DYNAMIC + DT_GNU_HASH) ----
#ifdef __ANDROID__
#include <link.h>

typedef struct bc_symtab_lib_ctx {
    const char *want_name;
    bc_elf_symtab_name_filter filter;  // nullptr = usa bc_elf_filter_jni_prefix
    bc_elf_symtab_cb cb;
    void *user;
    uintptr_t load_bias;
    int total_found;
    bool lib_matched;  // pelo menos 1 lib com esse nome foi processada
} bc_symtab_lib_ctx;

// Conta símbolos via DT_GNU_HASH: acha o maior índice alcançável pela
// cadeia de qualquer bucket, anda até o bit de fim-de-cadeia (LSB setado
// no valor do chain) — contagem = maior índice visitado + 1. Sem
// DT_GNU_HASH (raro em NDK moderno, mas possível com --hash-style=sysv),
// retorna 0 — chamador trata como "não achou nada" (fail-safe, não crasha).
static inline uint32_t bc_gnu_hash_symcount(const uint32_t *gnu_hash) {
    if (gnu_hash == nullptr) return 0;
    uint32_t nbuckets    = gnu_hash[0];
    uint32_t symoffset   = gnu_hash[1];
    uint32_t bloom_size  = gnu_hash[2];
    // bloom_shift em gnu_hash[3], não usado pra contagem — só filtro de lookup.
    const uint32_t *buckets = gnu_hash + 4 + bloom_size * 2;  // bloom words são 64-bit (2x uint32) em ELF64
    const uint32_t *chain   = buckets + nbuckets;

    uint32_t max_idx = 0;
    bool any = false;
    for (uint32_t b = 0; b < nbuckets; b++) {
        uint32_t idx = buckets[b];
        if (idx == 0) continue;  // bucket vazio
        any = true;
        // anda a cadeia até o bit de fim (LSB de chain[idx-symoffset] setado)
        for (uint32_t safety = 0; safety < 100000; safety++) {
            if (idx > max_idx) max_idx = idx;
            // BUG REAL achado por revisão (hermes): idx < symoffset causa
            // underflow em uint32_t (idx - symoffset vira número gigante) —
            // leitura fora dos limites de `chain`. Lib hostil/malformada
            // (agora alcançável: caminho genérico escaneia QUALQUER lib
            // carregada, não só as próprias) pode ter DT_GNU_HASH corrompido
            // de propósito. Fail-safe: aborta essa cadeia em vez de ler
            // memória arbitrária.
            if (idx < symoffset) break;
            uint32_t chain_val = chain[idx - symoffset];
            if (chain_val & 1) break;  // fim da cadeia
            idx++;
        }
    }
    return any ? (max_idx + 1) : 0;
}

// Trampolim que soma o load bias (endereço de carga da lib, ASLR) antes de
// repassar pro callback real do usuário — BUG REAL achado por revisão
// (freebuff, 2026-09-16): bc_elf_symtab_scan_filtered devolve st_value CRU
// (RVA relativo à base da lib, é assim que o núcleo puro é testável no
// host sem mapear lib de verdade). Sem somar dlpi_addr aqui, o endereço
// que chega em bc_generic_hook_install_all é um offset pequeno (ex.:
// 0x4000), não um ponteiro válido — DobbyInstrument armaria trampolim em
// memória arbitrária/não mapeada em vez do símbolo real. ctx->load_bias é
// setado logo abaixo, ANTES do scan, com info->dlpi_addr real da lib.
static inline void bc_symtab_bias_trampoline(const char *name, uint64_t addr, void *user) {
    bc_symtab_lib_ctx *ctx = (bc_symtab_lib_ctx *)user;
    ctx->cb(name, addr + ctx->load_bias, ctx->user);
}

static inline int bc_symtab_phdr_cb(struct dl_phdr_info *info, size_t, void *data) {
    bc_symtab_lib_ctx *ctx = (bc_symtab_lib_ctx *)data;
    // want_name == nullptr → escaneia TODAS as libs carregadas (usado pelo
    // sinal 2 da cascata de detecção de engine, que não sabe o nome da lib
    // de antemão — o motor pode estar amalgamado em qualquer .so do processo).
    if (info->dlpi_name == nullptr) return 0;
    if (ctx->want_name != nullptr && strstr(info->dlpi_name, ctx->want_name) == nullptr) return 0;
    ctx->lib_matched = true;

    const ElfW(Dyn) *dyn = nullptr;
    for (int i = 0; i < info->dlpi_phnum; i++) {
        if (info->dlpi_phdr[i].p_type == PT_DYNAMIC) {
            dyn = (const ElfW(Dyn) *)(info->dlpi_addr + info->dlpi_phdr[i].p_vaddr);
            break;
        }
    }
    if (dyn == nullptr) return 0;  // sem PT_DYNAMIC — não é lib dinâmica normal

    const bc_elf64_sym *symtab = nullptr;
    const char *strtab = nullptr;
    size_t strtab_size = 0;
    const uint32_t *gnu_hash = nullptr;

    for (const ElfW(Dyn) *d = dyn; d->d_tag != DT_NULL; d++) {
        switch (d->d_tag) {
            case DT_SYMTAB:
                symtab = (const bc_elf64_sym *)(info->dlpi_addr + d->d_un.d_ptr);
                break;
            case DT_STRTAB:
                strtab = (const char *)(info->dlpi_addr + d->d_un.d_ptr);
                break;
            case DT_STRSZ:
                strtab_size = (size_t)d->d_un.d_val;
                break;
            case DT_GNU_HASH:
                gnu_hash = (const uint32_t *)(info->dlpi_addr + d->d_un.d_ptr);
                break;
            default:
                break;
        }
    }
    if (symtab == nullptr || strtab == nullptr || strtab_size == 0) return 0;

    uint32_t sym_count = bc_gnu_hash_symcount(gnu_hash);
    if (sym_count == 0) return 0;  // sem DT_GNU_HASH ou hash vazio — fail-safe, não adivinha contagem

    ctx->load_bias = (uintptr_t)info->dlpi_addr;
    bc_elf_symtab_name_filter f = ctx->filter != nullptr ? ctx->filter : bc_elf_filter_jni_prefix;
    int n = bc_elf_symtab_scan_filtered(symtab, sym_count, strtab, strtab_size, f,
                                         bc_symtab_bias_trampoline, ctx);
    ctx->total_found += n;
    return 0;  // continua — pode haver mais de uma lib casando o nome
}

// Enumera símbolos JNI-like (Java_*) de qualquer lib carregada cujo nome
// contenha `image_name`. Retorna quantos achou (0 é válido — ver limite
// do RegisterNatives no topo do arquivo). `lib_found` (se não-nulo) avisa
// se a lib em si foi localizada, distinguindo "lib não existe"
// de "lib existe mas 0 símbolo Java_* exportado" (RegisterNatives).
static inline int bc_elf_symtab_scan_lib(const char *image_name, bc_elf_symtab_cb cb,
                                          void *user, bool *lib_found) {
    bc_symtab_lib_ctx ctx = {};
    ctx.want_name = image_name;
    ctx.cb = cb;
    ctx.user = user;
    dl_iterate_phdr(bc_symtab_phdr_cb, &ctx);
    if (lib_found != nullptr) *lib_found = ctx.lib_matched;
    return ctx.total_found;
}

// Igual a bc_elf_symtab_scan_lib, mas com filtro de nome customizado (ex.:
// substring "cocos2d::" em vez do prefixo "Java_") — reusado pela cascata
// de detecção de engine em bc_engine_detect.h (sinal 2, symbol C++).
static inline int bc_elf_symtab_scan_lib_filtered(const char *image_name,
                                                   bc_elf_symtab_name_filter filter,
                                                   bc_elf_symtab_cb cb, void *user,
                                                   bool *lib_found) {
    bc_symtab_lib_ctx ctx = {};
    ctx.want_name = image_name;
    ctx.filter = filter;
    ctx.cb = cb;
    ctx.user = user;
    dl_iterate_phdr(bc_symtab_phdr_cb, &ctx);
    if (lib_found != nullptr) *lib_found = ctx.lib_matched;
    return ctx.total_found;
}
#endif // __ANDROID__

#endif // BC_ELF_SYMTAB_H
