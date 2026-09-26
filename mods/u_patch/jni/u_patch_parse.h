// u_patch_parse.h — parser puro das regras C4 (.patch) e opções C3 (.conf).
// Header-only, sem dependência de plataforma: o teste host inclui direto.
// Contrato C4 (ROADMAP-UNIVERSAL.md): uma regra por linha, campos por espaço,
// '#' é comentário; <Classe> = Namespace.Nome ou só Nome (último '.' separa).
#pragma once
#include <cstdint>
#include <cstring>

typedef enum { UP_NONE, UP_RETURN, UP_MUL, UP_STATIC, UP_FIELD } up_kind_t;
typedef enum { UP_BOOL, UP_INT, UP_FLOAT } up_type_t;

struct up_rule_t {
    up_kind_t kind;
    char cls[128];     // Namespace.Nome ou só Nome
    char member[128];  // método (return/mul), campo (static/field)
    int nargs;         // return/mul: nº de args; field: args do método ou -1 (auto)
    char fmethod[128]; // field: método onde fixar ("" = auto, até 8 métodos)
    up_type_t type;
    char value[64];    // número, true/false ou $key
};

// Quebra "Namespace.Nome" no último '.': sem ponto, ns fica "".
// Retorna false se classe vazia.
static inline bool up_split_class(const char *cls, char *ns, size_t nsz, char *name, size_t nmsz) {
    // nsz/nmsz == 0 não é caso de chamador: sem isso, `nlen = nsz - 1` vira
    // SIZE_MAX e o memcpy copia o resto do heap (achado do review).
    if (!cls || !*cls || nsz == 0 || nmsz == 0) return false;
    const char *dot = strrchr(cls, '.');
    if (!dot) {
        if (nsz) ns[0] = '\0';
        snprintf(name, nmsz, "%s", cls);
    } else {
        size_t nlen = (size_t)(dot - cls);
        if (nlen >= nsz) nlen = nsz - 1;
        memcpy(ns, cls, nlen);
        ns[nlen] = '\0';
        snprintf(name, nmsz, "%s", dot + 1);
    }
    return name[0] != '\0';
}

// Divide linha em tokens por espaço/tab. Corta no 1º '#' (comentário).
// Retorna nº de tokens (0 = vazia/comentário).
static inline int up_tokenize(char *line, char *tok[], int maxtok) {
    char *hash = strchr(line, '#');
    if (hash) *hash = '\0';
    int n = 0;
    char *p = line;
    while (*p && n < maxtok) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (!*p) break;
        tok[n++] = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') p++;
        if (*p) *p++ = '\0';
    }
    return n;
}

static inline bool up_parse_type(const char *s, up_type_t *out) {
    if (strcmp(s, "bool") == 0) { *out = UP_BOOL; return true; }
    if (strcmp(s, "int") == 0) { *out = UP_INT; return true; }
    if (strcmp(s, "float") == 0) { *out = UP_FLOAT; return true; }
    return false;
}

// nargs: inteiro >= 0, só dígitos (strtol aceitaria lixo tipo "3x").
static inline bool up_parse_nargs(const char *tok, int *out) {
    long nargs = 0;
    bool neg = false;
    const char *s = tok;
    if (*s == '-') { neg = true; s++; }
    if (!*s) return false;
    for (; *s; s++) {
        if (*s < '0' || *s > '9') return false;
        nargs = nargs * 10 + (*s - '0');
        if (nargs > 64) return false;
    }
    *out = neg ? -(int)nargs : (int)nargs;
    return *out >= 0;
}

// Parse de UMA linha. 0 = regra ok, 1 = vazia/comentário (pular),
// -1 = inválida (logar e seguir).
static inline int up_parse_line(char *line, up_rule_t *r) {
    char *tok[8];
    int n = up_tokenize(line, tok, 8);
    if (n == 0) return 1;
    memset(r, 0, sizeof(*r));
    if (strcmp(tok[0], "return") == 0) {
        // return <Classe> <Método> <nargs> <tipo> <valor>
        if (n != 6) return -1;
        r->kind = UP_RETURN;
    } else if (strcmp(tok[0], "mul") == 0) {
        // mul <Classe> <Método> <nargs> <int|float> <fator>
        if (n != 6) return -1;
        r->kind = UP_MUL;
    } else if (strcmp(tok[0], "static") == 0) {
        // static <Classe> <campo> <tipo> <valor>
        if (n != 5) return -1;
        r->kind = UP_STATIC;
    } else if (strcmp(tok[0], "field") == 0) {
        // field <Classe> <campo> <tipo> <valor> [<Método> <nargs>]
        // 5 campos = auto (até 8 métodos de instância); 7 = método explícito.
        if (n != 5 && n != 7) return -1;
        r->kind = UP_FIELD;
    } else {
        return -1;
    }
    snprintf(r->cls, sizeof(r->cls), "%s", tok[1]);
    snprintf(r->member, sizeof(r->member), "%s", tok[2]);
    int vi = 3;  // índice do tipo (return/mul: 4, após o nargs)
    if (r->kind == UP_RETURN || r->kind == UP_MUL) {
        if (!up_parse_nargs(tok[3], &r->nargs)) return -1;
        vi = 4;
    } else if (r->kind == UP_FIELD) {
        if (n == 5) {
            r->nargs = -1;  // auto: sem método
        } else {
            snprintf(r->fmethod, sizeof(r->fmethod), "%s", tok[5]);
            if (!r->fmethod[0]) return -1;
            if (!up_parse_nargs(tok[6], &r->nargs)) return -1;
            // Regra única C4 (Manager alinhado): nargs -1 é auto-only (5
            // tokens). Em 7 tokens (método explícito), nargs >= 0 obrigatório.
            if (r->nargs < 0) return -1;
        }
    }
    if (!up_parse_type(tok[vi], &r->type)) return -1;
    if (r->kind == UP_MUL && r->type == UP_BOOL) return -1;  // mul só int|float
    if (tok[vi + 1][0] == '\0') return -1;
    snprintf(r->value, sizeof(r->value), "%s", tok[vi + 1]);
    if (!r->cls[0] || !r->member[0]) return -1;
    return 0;
}

// Nome de processo ainda-zygote (cmdline não especializado): não serve
// como <pkg>. Contrato C1: o mod prefere getenv("BEPINEX_PKG").
static inline bool up_is_zygote(const char *s) {
    return s && strncmp(s, "zygote", 6) == 0;
}

// --- checagem de tipo antes de escrever (achados #2 e #3 do review) -------
//
// O C4 deixa o tipo VIR DA REGRA (bool|int|float) e o jogo decide o tamanho
// real do campo. Escrever 4 bytes num campo de 1 byte suja os vizinhos do
// objeto, e num método de STRUCT o x0 nem é ponteiro (é o valor), então o
// store vai para endereço arbitrário. Estas funções são puras: o mod passa o
// que o il2cpp respondeu e elas dizem se pode escrever e por que não.
//
// Devolve 0 = pode escrever; -1 = recusa, com o motivo em `why`.
static inline int up_value_size_by_name(const char *type_name) {
    if (!type_name) return 0;
    if (strcmp(type_name, "System.Boolean") == 0) return 1;
    if (strcmp(type_name, "System.Int32") == 0) return 4;
    if (strcmp(type_name, "System.Single") == 0) return 4;
    if (strcmp(type_name, "System.Char") == 0) return 2;
    return 0;  // System.Int64/Double, referência, enum, struct: fora do C4
}

static inline int up_value_type_check(bool klass_is_valuetype, const char *type_name,
                                      size_t want_size, char *why, size_t whysz) {
    if (klass_is_valuetype) {
        snprintf(why, whysz,
                 "a classe e STRUCT: em metodo de instancia de struct o x0 nao e ponteiro "
                 "para o objeto, entao o store iria para endereco arbitrario — o verbo field "
                 "so vale para classe (para struct, use um metodo estatico com o verbo static)");
        return -1;
    }
    if (!type_name) return 0;  // il2cpp sem type_get_name: segue o comportamento antigo
    int real = up_value_size_by_name(type_name);
    if (real == 0) {
        snprintf(why, whysz,
                 "campo do tipo %s nao e bool/int/float do C4 (1/4 bytes) — regra recusada "
                 "para nao escrever em cima dos campos vizinhos", type_name);
        return -1;
    }
    if ((size_t)real != want_size) {
        snprintf(why, whysz,
                 "campo e %s (%d byte(s)) e a regra escreve %zu byte(s) — regra recusada "
                 "para nao corromper o campo vizinho do objeto",
                 type_name, real, want_size);
        return -1;
    }
    return 0;
}

// --- #13: percorrer linhas do .patch sem loop infinito -------------------
//
// O scanner usa isto no lugar do while manual. O bug historico (linha vazia
// sem avanco -> while eterno no worker, sem log) fica IMPOSSIVEL aqui: o
// avanco esta na propria funcao e o harness conta as linhas visitadas.
typedef int (*up_line_cb)(char *line, int lineno, void *ctx);

static inline int up_foreach_line(char *buf, up_line_cb cb, void *ctx, int max_lines) {
    if (!buf || !cb) return 0;
    int visited = 0;
    char *line = buf;
    while (line) {
        if (max_lines > 0 && visited >= max_lines) break;
        int lineno = visited + 1;
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        cb(line, lineno, ctx);
        visited++;
        line = nl ? nl + 1 : nullptr;
        if (line && !*line) line = nullptr;   // fim do buffer
    }
    return visited;
}

// Conf C3: busca "key=value" no buffer (linhas '\n', '#' comentário).
// Retorna true e copia o valor (sem espaços ao redor).
static inline bool up_conf_get(const char *buf, const char *key, char *out, size_t outsz) {
    if (!buf || !key || !out || outsz == 0) return false;
    size_t klen = strlen(key);
    const char *p = buf;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (*p == '#') { while (*p && *p != '\n') p++; continue; }
        if (!*p) break;
        if (strncmp(p, key, klen) == 0) {
            const char *e = p + klen;
            while (*e == ' ' || *e == '\t') e++;
            if (*e != '=') { while (*p && *p != '\n') p++; continue; }
            const char *v = e + 1;
            while (*v == ' ' || *v == '\t') v++;
            size_t n = 0;
            while (v[n] && v[n] != '\n' && v[n] != '\r' && v[n] != '#' && n + 1 < outsz) {
                out[n] = v[n];
                n++;
            }
            while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '\t')) n--;
            out[n] = '\0';
            return true;
        }
        while (*p && *p != '\n') p++;
    }
    return false;
}
