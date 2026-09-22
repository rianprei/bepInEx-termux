// bc_pattern_scan.h — AOB (array-of-bytes) signature scan, a resposta nativa
// ao gap estrutural real entre bepin-termux e BepInEx PC:
//
//   BepInEx/Harmony hooka por METADATA .NET (Type.GetMethod(name, sig)) —
//   sobrevive recompile porque nomes/assinaturas de método não mudam mesmo
//   quando o compilador realoca tudo (AccessTools.cs:596, PatchTools.cs).
//
//   bepin-termux (Dobby) hooka por ENDEREÇO FIXO (offsetsdb.h, RVA por
//   versão do jogo) — quebra a cada update, porque não existe metadata
//   gerenciada num binário nativo ARM64.
//
// AOB scan é o equivalente nativo real (mesma técnica usada por Frida,
// Cheat Engine, IDA FLIRT): em vez de "a função está no offset X", procura
// "os bytes que só essa função tem" dentro do segmento executável. Um
// recompile que só REORGANIZA código (sem mudar o corpo da função) ainda
// tem o mesmo prólogo de bytes, só em endereço diferente — o pattern
// continua achando. Não é tão forte quanto metadata (um recompile que MUDA
// o corpo da função quebra o pattern também), mas é estritamente mais
// resiliente que RVA fixo, que quebra em QUALQUER recompile.
//
// Honesto: não elimina offsetsdb.h — pattern scan é mais lento (varredura
// linear) e ambíguo se o pattern for curto demais (2+ matches = falha
// deliberada, nunca escolhe "o primeiro" às cegas). Uso pretendido: fallback
// quando o RVA do offsetsdb.h não bate mais (update do jogo), antes de
// desistir e marcar o hook como DORMANT.
//
// Testabilidade: a lógica pura (bc_pattern_scan_buffer) opera sobre um
// buffer em memória — testável no host sem ELF/Android. O device usa
// bc_pattern_scan_lib (dl_iterate_phdr, não testável no harness, fino o
// suficiente pra não esconder lógica).

#ifndef BC_PATTERN_SCAN_H
#define BC_PATTERN_SCAN_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BC_PATTERN_MAX_BYTES 64  // prólogo real cabe em ~6-16 bytes; folga generosa

// mask[i] == 0  → byte[i] é wildcard, casa qualquer valor (bytes de endereço
//                 relativo — ex.: operando de adrp/bl mudam entre builds)
// mask[i] != 0  → byte[i] tem que casar exatamente pattern[i]
// Nota (achado no review do OpenCode): é 0/não-zero, NÃO a convenção de
// string "xx??xx" de Cheat Engine/Frida — preencher mask com os bytes
// ASCII 'x'(0x78)/'?'(0x3F) funcionaria por acidente (ambos não-zero),
// não por design. Use 0 e 1 explícitos.
typedef struct bc_pattern {
    uint8_t bytes[BC_PATTERN_MAX_BYTES];
    uint8_t mask[BC_PATTERN_MAX_BYTES];
    size_t len;
} bc_pattern;

typedef enum {
    BC_SCAN_OK = 0,        // exatamente 1 match — endereço confiável
    BC_SCAN_NOT_FOUND = 1, // 0 matches
    BC_SCAN_AMBIGUOUS = 2, // 2+ matches — pattern curto demais, recusa escolher
} bc_scan_status;

// Varre `buf[0..len)` procurando `pat`. Recusa retornar endereço se achar
// 0 ou 2+ ocorrências — ambiguidade é erro, não "pega o primeiro que achar"
// (mesma filosofia de resolve_symbol: incerto == falha explícita, nunca
// silenciosa). `out_offset` só é escrito quando o retorno é BC_SCAN_OK.
static inline bc_scan_status bc_pattern_scan_buffer(const uint8_t *buf, size_t len,
                                                     const bc_pattern *pat,
                                                     size_t *out_offset) {
    if (buf == nullptr || pat == nullptr || pat->len == 0 || pat->len > len)
        return BC_SCAN_NOT_FOUND;

    size_t matches = 0;
    size_t first_match = 0;
    for (size_t i = 0; i + pat->len <= len; i++) {
        bool ok = true;
        for (size_t j = 0; j < pat->len; j++) {
            if (pat->mask[j] != 0 && buf[i + j] != pat->bytes[j]) { ok = false; break; }
        }
        if (ok) {
            if (matches == 0) first_match = i;
            matches++;
            if (matches > 1) break; // já ambíguo, não precisa continuar varrendo
        }
    }

    if (matches == 0) return BC_SCAN_NOT_FOUND;
    if (matches > 1) return BC_SCAN_AMBIGUOUS;
    if (out_offset != nullptr) *out_offset = first_match;
    return BC_SCAN_OK;
}

#ifdef __cplusplus
}
#endif

// ---- Camada de device (dl_iterate_phdr), NÃO incluída no selftest_harness ----
// Guardada atrás de __ANDROID__ pra não puxar link.h/dlfcn no host build.
#ifdef __ANDROID__
#include <link.h>
#include <string.h>

typedef struct bc_scan_ctx {
    const char *want_name;   // nome da lib alvo (ex.: "libnative-lib.so")
    const bc_pattern *pat;
    void *found_addr;        // resultado, só válido se ok_count==1 && ambiguous_count==0
    int ok_count;            // segmentos com exatamente 1 match cada
    int ambiguous_count;     // segmentos que já vieram ambíguos por si só
} bc_scan_ctx;

static inline int bc_scan_phdr_cb(struct dl_phdr_info *info, size_t, void *data) {
    bc_scan_ctx *ctx = (bc_scan_ctx *)data;
    if (ctx->want_name == nullptr || info->dlpi_name == nullptr) return 0;
    if (strstr(info->dlpi_name, ctx->want_name) == nullptr) return 0;

    for (int i = 0; i < info->dlpi_phnum; i++) {
        const ElfW(Phdr) *phdr = &info->dlpi_phdr[i];
        if (phdr->p_type != PT_LOAD || !(phdr->p_flags & PF_X)) continue;
        const uint8_t *seg = (const uint8_t *)(info->dlpi_addr + phdr->p_vaddr);
        // p_memsz inclui BSS (zero-fill, pode passar do que o arquivo mapeia
        // de verdade); p_filesz é o que existe fisicamente no arquivo/página
        // mapeada. Escanear p_memsz num segmento PF_X arrisca ler além do
        // mapeamento real → segfault se memsz > filesz (achado real na
        // review do OpenCode). min() é sempre seguro: nunca lê além do que
        // o arquivo garante mapeado.
        size_t seglen = phdr->p_filesz < phdr->p_memsz ? phdr->p_filesz : phdr->p_memsz;
        size_t off;
        bc_scan_status st = bc_pattern_scan_buffer(seg, seglen, ctx->pat, &off);
        if (st == BC_SCAN_OK) {
            ctx->ok_count++;
            ctx->found_addr = (void *)(seg + off); // só o último importa se ok_count>1 (vira ambíguo de qualquer forma)
        } else if (st == BC_SCAN_AMBIGUOUS) {
            ctx->ambiguous_count++;
        }
    }
    return 0; // continua iterando (pode haver >1 lib com nome parecido)
}

// Varre o(s) segmento(s) PT_LOAD+PF_X da lib `image_name` já carregada no
// processo. Mesma semântica de ambiguidade de bc_pattern_scan_buffer, agora
// agregada entre TODOS os segmentos/libs que casam o nome — 2+ matches em
// qualquer lugar já é BC_SCAN_AMBIGUOUS.
static inline bc_scan_status bc_pattern_scan_lib(const char *image_name,
                                                  const bc_pattern *pat,
                                                  void **out_addr) {
    bc_scan_ctx ctx = {};
    ctx.want_name = image_name;
    ctx.pat = pat;
    dl_iterate_phdr(bc_scan_phdr_cb, &ctx);
    if (ctx.ambiguous_count > 0) return BC_SCAN_AMBIGUOUS; // qualquer segmento ambíguo já basta
    if (ctx.ok_count == 0) return BC_SCAN_NOT_FOUND;
    if (ctx.ok_count > 1) return BC_SCAN_AMBIGUOUS;        // 2+ segmentos com 1 match cada
    if (out_addr != nullptr) *out_addr = ctx.found_addr;
    return BC_SCAN_OK;
}
#endif // __ANDROID__

#endif // BC_PATTERN_SCAN_H
