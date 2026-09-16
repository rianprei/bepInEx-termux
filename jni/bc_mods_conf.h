// bc_mods_conf.h — formato e helpers do arquivo de config de hooks.
//
// v2 (2026-09-14): valores tipados além de bool, inspirado no ConfigEntry do
// BepInEx (ver context/bepinex-config-vs-bcpoc.md). Cada chave tem um TIPO
// conhecido em tempo de compilação (tabela estática), e o parse coage o valor
// ao domínio da chave — semântica AcceptableValueList/Range do BepInEx:
// valor inválido/fora do range NÃO é descartado, é CLAMPADO (bool fora do
// domínio → default; int fora do range → clamp pro limite mais próximo).
//
// Formato v2 (compatível com v1 — arquivos antigos continuam válidos):
//   appTouch=off          # bool (como v1)
//   appUpdateDraw=60      # int com clamp [min..max] definido na tabela
//   streamSource=game     # string de domínio fechado (lista aceitável)
// Comentários (#), linhas vazias e chaves desconhecidas são ignorados.
//
// Tipagem: igual BepInEx Bind<T>, o tipo NÃO vem do arquivo — vem do código
// (tabela bc_mod_schema). O arquivo só fornece texto; quem interpreta é a
// tabela. Valor de chave desconhecida no arquivo = ignorado (não há
// OrphanedEntries — nossa tabela de chaves é estática, sem Bind tardio).
//
// Lido pelo módulo no postAppSpecialize (processo do jogo) e escrito pelo
// companion (processo root) via comandos list_mods / toggle_mod / set_mod.
#ifndef BC_MODS_CONF_H
#define BC_MODS_CONF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define BC_MODS_CONF_PATH "/data/local/tmp/bc_mods.conf"
#define BC_MODS_CONF_MAX 16   // máx. de entradas no arquivo
#define BC_MODS_VAL_MAX 32    // máx. de bytes do valor (string)

#ifdef __cplusplus
extern "C" {
#endif

// Tipos suportados (espelho dos conversores BepInEx TomlTypeConverter.cs:18-98,
// subconjunto que o POC precisa: bool, int com range, string de domínio fechado)
typedef enum {
    BC_MOD_BOOL = 0,   // "on"/"off"; default quando ausente
    BC_MOD_INT,        // decimal com sinal; clamp [min,max]
    BC_MOD_ENUM,       // string de domínio fechado (lista aceitável)
} bc_mod_type;

struct bc_mod_entry {
    char name[32];
    // union de valor tipado — interpretado conforme o schema da chave
    bool b;                 // BC_MOD_BOOL
    long i;                 // BC_MOD_INT (já clampado)
    char s[BC_MODS_VAL_MAX];// BC_MOD_ENUM (já validado no domínio)
    bool present;           // linha existia no arquivo (false = default implícito)
};

// Schema estático de uma chave — o "ConfigDefinition + AcceptableValues" nosso.
struct bc_mod_schema {
    const char *name;
    bc_mod_type type;
    // BC_MOD_BOOL
    bool def_b;
    // BC_MOD_INT
    long min_i, max_i, def_i;
    // BC_MOD_ENUM
    const char *const *enum_values; // terminado em NULL
    const char *def_s;
};

// ---- Helpers de coação (semântica BepInEx Clamp) -------------------------

// BOOL: só "on"/"off" são válidos; qualquer outra coisa → default.
// (BepInEx: bool não-clampável — valor inválido falha o parse da linha;
// aqui coagimos ao default, mais tolerante que o BepInEx de propósito.)
static inline bool bc_mod_coerce_bool(const char *val, bool def, bool *out) {
    if (strcmp(val, "on") == 0)  { *out = true;  return true; }
    if (strcmp(val, "off") == 0) { *out = false; return true; }
    *out = def;
    return false;
}

// INT: strtol + clamp [min,max] (semântica AcceptableValueRange.Clamp).
// Texto não-numérico → default. Retorna true se o texto era um int válido.
static inline bool bc_mod_coerce_int(const char *val, long min, long max,
                                     long def, long *out) {
    char *end = nullptr;
    long v = strtol(val, &end, 10);
    if (end == val || *end != '\0') { *out = def; return false; }  // não-numérico
    if (v < min) v = min;
    if (v > max) v = max;
    *out = v;
    return true;
}

// ENUM: valor deve estar na lista; fora do domínio → default
// (semântica AcceptableValueList.Clamp que retorna AcceptableValues[0] —
// aqui usamos o default declarado, mais explícito que "primeiro da lista").
static inline bool bc_mod_coerce_enum(const char *val,
                                      const char *const *values,
                                      const char *def,
                                      char *out, size_t cap) {
    // Precisão explícita (cap-1) em vez de "%s": elimina o falso-positivo
    // de -Wformat-truncation (gcc não sabe estaticamente que `val`/`def`
    // cabem em `cap` — snprintf já trunca com segurança, mas o warning
    // sinaliza como "pode truncar" sem essa pista de tamanho máximo).
    for (const char *const *v = values; *v != nullptr; v++) {
        if (strcmp(val, *v) == 0) {
            snprintf(out, cap, "%.*s", (int)(cap - 1), val);
            return true;
        }
    }
    snprintf(out, cap, "%.*s", (int)(cap - 1), def);
    return false;
}

// ---- Parse -----------------------------------------------------------------

// Parseia o conteúdo (buf, nul-terminado) conforme a tabela de schema.
// out/cap: capacidade de entradas (>= nº de chaves na tabela pra ficar completo).
// Preenche TODAS as chaves da tabela (present=false quando a linha não veio,
// com valor default). Retorna nº de entradas preenchidas.
// Nunca falha: input hostil → entradas com default/valor coagido.
static inline int bc_mods_parse(const char *buf,
                                const struct bc_mod_schema *schema, int nschema,
                                struct bc_mod_entry *out, int cap) {
    // 1. inicia tudo com default, present=false
    int filled = 0;
    for (int k = 0; k < nschema && filled < cap; k++, filled++) {
        snprintf(out[filled].name, sizeof(out[filled].name), "%s", schema[k].name);
        out[filled].present = false;
        switch (schema[k].type) {
            case BC_MOD_BOOL: out[filled].b = schema[k].def_b; break;
            case BC_MOD_INT:  out[filled].i = schema[k].def_i; break;
            case BC_MOD_ENUM:
                snprintf(out[filled].s, sizeof(out[filled].s), "%s", schema[k].def_s);
                break;
        }
    }
    if (buf == nullptr) return filled;

    // 2. varre o arquivo linha a linha
    const char *p = buf;
    while (*p != '\0') {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\r' || *p == '\0') {
            while (*p != '\0' && *p != '\n') p++;
            if (*p == '\n') p++;
            continue;
        }
        const char *name = p;
        while (*p != '\0' && *p != '=' && *p != '\n' && *p != '\r') p++;
        size_t name_len = (size_t)(p - name);
        if (*p != '=' || name_len == 0 || name_len >= 32) {
            while (*p != '\0' && *p != '\n') p++;
            if (*p == '\n') p++;
            continue;
        }
        p++;  // '='
        const char *val = p;
        while (*p != '\0' && *p != '\n' && *p != '\r') p++;
        size_t val_len = (size_t)(p - val);
        if (*p == '\r') p++;
        if (*p == '\n') p++;

        // nome com espaço interno = malformado
        bool bad = false;
        for (size_t i = 0; i < name_len; i++)
            if (name[i] == ' ' || name[i] == '\t') { bad = true; break; }
        if (bad || val_len >= 256) continue;

        char nbuf[32], vbuf[256];
        memcpy(nbuf, name, name_len); nbuf[name_len] = '\0';
        memcpy(vbuf, val, val_len);  vbuf[val_len] = '\0';

        // 3. acha a chave na tabela (chave desconhecida → ignora)
        for (int k = 0; k < nschema; k++) {
            if (strcmp(schema[k].name, nbuf) != 0) continue;
            // acha o slot já inicializado com default pra essa chave
            for (int s = 0; s < filled; s++) {
                if (strcmp(out[s].name, nbuf) != 0) continue;
                out[s].present = true;  // última ocorrência vence (re-coage)
                switch (schema[k].type) {
                    case BC_MOD_BOOL:
                        bc_mod_coerce_bool(vbuf, schema[k].def_b, &out[s].b);
                        break;
                    case BC_MOD_INT:
                        bc_mod_coerce_int(vbuf, schema[k].min_i, schema[k].max_i,
                                          schema[k].def_i, &out[s].i);
                        break;
                    case BC_MOD_ENUM:
                        bc_mod_coerce_enum(vbuf, schema[k].enum_values,
                                           schema[k].def_s, out[s].s,
                                           sizeof(out[s].s));
                        break;
                }
                break;
            }
            break;
        }
    }
    return filled;
}

// ---- Format ----------------------------------------------------------------

// Serializa SOMENTE as entradas não-default (present==true), v1/v2 mistos:
// bool → on/off; int → decimal; enum → texto. Retorna bytes usados ou -1.
// Config todo-default → saída vazia → companion unlinka (arquivo mínimo).
static inline int bc_mods_format(const struct bc_mod_schema *schema, int nschema,
                                 const struct bc_mod_entry *entries, int n,
                                 char *buf, size_t cap) {
    if (buf == nullptr || cap == 0) return -1;
    size_t used = 0;
    buf[0] = '\0';
    for (int i = 0; i < n; i++) {
        if (!entries[i].present) continue;
        // acha schema pra saber o tipo na hora de formatar
        const struct bc_mod_schema *sc = nullptr;
        for (int k = 0; k < nschema; k++) {
            if (strcmp(schema[k].name, entries[i].name) == 0) { sc = &schema[k]; break; }
        }
        if (sc == nullptr) continue;  // sem schema = não sabe formatar
        int w;
        switch (sc->type) {
            case BC_MOD_BOOL:
                w = snprintf(buf + used, cap - used, "%s=%s\n",
                             entries[i].name, entries[i].b ? "on" : "off");
                break;
            case BC_MOD_INT:
                w = snprintf(buf + used, cap - used, "%s=%ld\n",
                             entries[i].name, entries[i].i);
                break;
            case BC_MOD_ENUM:
                w = snprintf(buf + used, cap - used, "%s=%s\n",
                             entries[i].name, entries[i].s);
                break;
            default:
                w = -1;
        }
        if (w < 0 || (size_t)w >= cap - used) return -1;  // truncou: recusa
        used += (size_t)w;
    }
    return (int)used;
}

// ---- Comparação (guard de no-op no reload) --------------------------------

// Acha o schema de uma chave (nullptr se não existir).
static inline const struct bc_mod_schema *bc_mods_schema_find(
        const struct bc_mod_schema *schema, int nschema, const char *name) {
    for (int i = 0; i < nschema; i++)
        if (strcmp(schema[i].name, name) == 0) return &schema[i];
    return nullptr;
}

// Serializa o VALOR de uma entrada conforme o schema ("on", "60", "companion")
// — usado no log de delta do reload ("nome: antigo -> novo"). out/cap deve
// comportar BC_MODS_VAL_MAX. Nunca falha (tipo desconhecido → "?").
static inline const char *bc_mods_value_str(const struct bc_mod_schema *sc,
                                            const struct bc_mod_entry *e,
                                            char *out, size_t cap) {
    switch (sc->type) {
        case BC_MOD_BOOL: snprintf(out, cap, "%s", e->b ? "on" : "off"); break;
        case BC_MOD_INT:  snprintf(out, cap, "%ld", e->i); break;
        case BC_MOD_ENUM: snprintf(out, cap, "%s", e->s); break;
        default:          snprintf(out, cap, "?"); break;
    }
    return out;
}

// true se duas entradas da MESMA chave são equivalentes pra comportamento:
// valor (b/i/s) + present. present NÃO é cosmético — o gate do throttle em
// main.cpp exige present (linha existir no arquivo = throttle ativo), então
// "ausente" → "presente com o default" É mudança real mesmo com mesmo i.
static inline bool bc_mods_entry_equal(const struct bc_mod_entry *a,
                                       const struct bc_mod_entry *b) {
    return strcmp(a->name, b->name) == 0 &&
           a->present == b->present &&
           a->b == b->b &&
           a->i == b->i &&
           strcmp(a->s, b->s) == 0;
}

// true se dois configs parseados completos (mesmo schema, mesma ordem) são
// idênticos — o guard de no-op do reload (sem delta real, não mexe em nada,
// espelho do SettingChanged do BepInEx que só dispara em mudança).
static inline bool bc_mods_equal(const struct bc_mod_entry *a,
                                 const struct bc_mod_entry *b, int n) {
    for (int i = 0; i < n; i++)
        if (!bc_mods_entry_equal(&a[i], &b[i])) return false;
    return true;
}

// ---- Watch-per-key callbacks (BepInEx ConfigEntry.SettingChanged port) ----------
//
// BepInEx (ConfigEntryBase.cs:22-24 + ConfigFile.cs:596-611):
//   - cada ConfigEntry<T> se registra no evento FILE-Level SettingChanged;
//   - filtra por `args.ChangedSetting == this` e re-emite pro evento POR-ENTRADA;
//   - OnSettingChanged envolve CADA handler em try/catch — handler ruim vira
//     Logger.LogError, loop continua (ConfigFile.cs:596-610).
//
// bc-poc: o polling loop já detecta deltas (main.cpp:888). Aqui: tabela plana
// de callbacks por-chave + busca lookup + função de disparo fatorada (single
// source of truth, testada no harness — espelho de hook_dispatch em
// bc_hook_logic.h que o main.cpp DELEGA e o harness testa diretamente).
//
// Limitation: C não tem exceções → NÃO isolamos crashes por callback.
// Em C++ um callback C++ pode lançar, mas a assinatura é extern "C"
// (pointer de função C) → UB lançar através disso. Documentado: callbacks
// internos do bc-poc são conhecidos e não-panicking.

// Callback: dispara quando uma chave muda entre reload. old/new são entradas
// do schema (ambos sempre preenchidos — bc_mods_parse preenche todos os slots
// com defaults). key é o nome da chave (g_cfg[i].name).
typedef void (*bc_mod_watch_fn)(const char *key,
                                const struct bc_mod_entry *old_val,
                                const struct bc_mod_entry *new_val);

// Par chave → callback (tabela plana; tamanho = número de chaves no schema)
typedef struct {
    char key[32];
    bc_mod_watch_fn fn;
} bc_mod_watch;

// Registra callback pra uma chave. Idempotent: re-registrar a mesma chave
// substitui o callback (não duplica). Retorna true se ok (capacidade + args).
static inline bool bc_mod_watch_register(bc_mod_watch *table, int cap, int *count,
                                         const char *key, bc_mod_watch_fn fn) {
    if (table == nullptr || cap <= 0 || count == nullptr ||
        key == nullptr || fn == nullptr || strlen(key) >= sizeof(table[0].key))
        return false;
    for (int i = 0; i < *count; i++) {
        if (strcmp(table[i].key, key) == 0) {
            table[i].fn = fn;
            return true;
        }
    }
    if (*count >= cap) return false;
    snprintf(table[*count].key, sizeof(table[*count].key), "%s", key);
    table[*count].fn = fn;
    (*count)++;
    return true;
}

// Busca callback registrado pra uma chave. nullptr se não registrado.
static inline bc_mod_watch_fn bc_mod_watch_find(const bc_mod_watch *table,
                                                 int count, const char *key) {
    if (table == nullptr || key == nullptr) return nullptr;
    for (int i = 0; i < count; i++)
        if (strcmp(table[i].key, key) == 0)
            return table[i].fn;
    return nullptr;
}

// Dispara callbacks pra TODAS as chaves que mudaram entre before[] e after[].
// Usa bc_mods_entry_equal() → match inclui present+valor (presence NÃO é
// cosmético — linha ausente→presente com default É mudança real). Fatorada
// aqui (e não inline no main.cpp reload loop) pra ser testável no harness.
// Retorna nº de callbacks disparados.
static inline int bc_mod_watch_fire_changed(const bc_mod_watch *table, int count,
                                             const struct bc_mod_entry *before,
                                             const struct bc_mod_entry *after, int n) {
    if (table == nullptr || count <= 0 || before == nullptr ||
        after == nullptr || n <= 0)
        return 0;
    int fired = 0;
    for (int i = 0; i < n; i++) {
        if (bc_mods_entry_equal(&before[i], &after[i])) continue;
        bc_mod_watch_fn fn = bc_mod_watch_find(table, count, after[i].name);
        if (fn != nullptr) {
            fn(after[i].name, &before[i], &after[i]);
            fired++;
        }
    }
    return fired;
}

#ifdef __cplusplus
}
#endif

#endif  // BC_MODS_CONF_H
