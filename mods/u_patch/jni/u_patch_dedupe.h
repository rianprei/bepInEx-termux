// u_patch_dedupe.h — "qual regra eu já tentei?" sem string gigante e sem
// inundar o log (achados #6 e #7 do review).
//
// Antes: a chave era o texto da regra num char[224], e o texto todo (id +
// classe + membro + valor + método = até ~456 bytes) TRUNCAVA. Duas regras
// com prefixo de nome comum viravam a mesma chave truncada, e a segunda era
// "já aplicada" para sempre, sem log. Pior: a tabela tinha 128 entradas e
// quando enchia, up_mark voltava sem inserir — aí up_find_applied devolvia
// -1 eternamente e a MESMA falha era logada a cada 2s (inundação do log.txt
// C1, que é compartilhado com o loader e os outros mods).
//
// Agora: a chave é o hash FNV-1a 64 do texto COMPLETO (colisão de 2^-64, sem
// truncamento), a tabela é maior, e o log de uma regra é controlado pelo
// estado da entrada — não pela capacidade da tabela. Cheia, o mod avisa UMA
// vez e para de aplicar regra nova (orçamento de CPU finito em vez de
// reprocessar para sempre).
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>

#define UP_APPLIED_MAX 512   // teto de regras distintas lembradas

// 1 = já logou esta regra (falhou 1x), 2 = aplicada com sucesso.
#define UP_ST_SEEN 1
#define UP_ST_OK 2

struct up_applied_t {
    uint64_t hash;
    uint8_t state;
};

// FNV-1a 64: barato, sem estado, e pega a string INTEIRA (por isso não
// depende de buffer de tamanho fixo).
static inline uint64_t up_sig_hash(const char *s) {
    uint64_t h = 1469598103934665603ull;
    if (!s) return 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        h ^= (uint64_t)*p;
        h *= 1099511628211ull;
    }
    return h ? h : 1;  // 0 reservado para "nunca visto"
}

static inline int up_dedupe_find(const struct up_applied_t *t, int n, uint64_t hash) {
    if (!hash) return -1;
    for (int i = 0; i < n; i++)
        if (t[i].hash == hash) return i;
    return -1;
}

// Insere/atualiza. false = tabela cheia (regra NÃO passa a ser lembrada).
static inline bool up_dedupe_mark(struct up_applied_t *t, int *n, int max,
                                  uint64_t hash, uint8_t state) {
    int i = up_dedupe_find(t, *n, hash);
    if (i >= 0) { t[i].state = state; return true; }
    if (*n >= max) return false;
    t[*n].hash = hash;
    t[*n].state = state;
    (*n)++;
    return true;
}

// Log de falha só na 1a vez da regra — decide pelo ESTADO, não pela
// capacidade da tabela (achado #6: antes era "se a entrada existe e já
// logou", e entrada que não cabia na tabela nunca existia = log a cada 2s).
static inline bool up_dedupe_should_log(const struct up_applied_t *t, int n, uint64_t hash) {
    int i = up_dedupe_find(t, n, hash);
    return i < 0 || t[i].state == 0;
}

static inline bool up_dedupe_full(int n, int max) { return n >= max; }
