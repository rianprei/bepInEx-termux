// u_patch_parse.h — parser puro das regras C4 (.patch) e opções C3 (.conf).
// Header-only, sem dependência de plataforma: o teste host inclui direto.
// Contrato C4 (ROADMAP-UNIVERSAL.md): uma regra por linha, campos por espaço,
// '#' é comentário; <Classe> = Namespace.Nome ou só Nome (último '.' separa).
#pragma once
#include <cstdint>
#include <cstring>

typedef enum { UP_NONE, UP_RETURN, UP_MUL, UP_STATIC } up_kind_t;
typedef enum { UP_BOOL, UP_INT, UP_FLOAT } up_type_t;

struct up_rule_t {
    up_kind_t kind;
    char cls[128];     // Namespace.Nome ou só Nome
    char member[128];  // método (return/mul) ou campo (static)
    int nargs;         // return/mul (0 = sem namespace... não: nº de args)
    up_type_t type;
    char value[64];    // número, true/false ou $key
};

// Quebra "Namespace.Nome" no último '.': sem ponto, ns fica "".
// Retorna false se classe vazia.
static inline bool up_split_class(const char *cls, char *ns, size_t nsz, char *name, size_t nmsz) {
    if (!cls || !*cls) return false;
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
    } else {
        return -1;
    }
    snprintf(r->cls, sizeof(r->cls), "%s", tok[1]);
    snprintf(r->member, sizeof(r->member), "%s", tok[2]);
    int vi = 3;  // índice do próximo campo a ler
    if (r->kind != UP_STATIC) {
        // nargs: inteiro >= 0, só dígitos (strtol aceitaria lixo tipo "3x").
        long nargs = 0;
        bool neg = false;
        const char *s = tok[3];
        if (*s == '-') { neg = true; s++; }
        if (!*s) return -1;
        for (; *s; s++) {
            if (*s < '0' || *s > '9') return -1;
            nargs = nargs * 10 + (*s - '0');
            if (nargs > 64) return -1;
        }
        r->nargs = neg ? -(int)nargs : (int)nargs;
        if (r->nargs < 0) return -1;
        vi = 4;
    }
    if (!up_parse_type(tok[vi], &r->type)) return -1;
    if (r->kind == UP_MUL && r->type == UP_BOOL) return -1;  // mul só int|float
    if (tok[vi + 1][0] == '\0') return -1;
    snprintf(r->value, sizeof(r->value), "%s", tok[vi + 1]);
    if (!r->cls[0] || !r->member[0]) return -1;
    return 0;
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
