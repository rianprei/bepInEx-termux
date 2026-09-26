// u_frida_config.h — parte pura do u_frida (F11): nomes de arquivo, pacote
// usável, montagem e VALIDAÇÃO do config JSON do gadget. Header-only, sem
// dependência de plataforma: o teste host inclui direto.
//
// Regra de descoberta do config (fonte: frida-core/lib/gadget/gadget.vala,
// derive_config_path_from_file_path): <dir>/<stem>.config, onde stem é o
// nome do binário sem a última extensão. Por isso o binário se chama
// frida-gadget.bin (SEM .so — senão o loader daria dlopen sozinho, sem
// config, e o gadget travaria o jogo em modo listen/wait) e o config é
// frida-gadget.config ao lado dele.
#pragma once
#include <cstddef>
#include <cstdio>
#include <cstring>

#define UF_GADGET_FILE "frida-gadget.bin"
#define UF_CONFIG_FILE "frida-gadget.config"
#define UF_CONFIG_MAX 4096  // config maior que isso = recusado (não lê pela metade)

// É mod .js? *.js, menos oculto/subpasta. *.js.off não termina em .js,
// então já cai fora pela regra abaixo (igual C1 dos .so/.patch).
static inline bool uf_is_js_mod(const char *name) {
    if (name == nullptr || name[0] == '\0' || name[0] == '.') return false;
    if (strchr(name, '/') != nullptr) return false;
    size_t n = strlen(name);
    if (n < 4) return false;
    return strcmp(name + n - 3, ".js") == 0;
}

// Pacote usável pra montar caminho (/data/local/tmp/mods/<pkg>,
// /data/data/<pkg>/...): charset de nome de pacote Android [A-Za-z0-9._],
// sem '/' nem "..", não começa com '.', não é zygote* (processo ainda não
// especializado). Mesma regra do tools/deploy_frida.sh.
static inline bool uf_pkg_ok(const char *pkg) {
    if (pkg == nullptr || pkg[0] == '\0' || pkg[0] == '.') return false;
    if (strncmp(pkg, "zygote", 6) == 0 || strstr(pkg, "..") != nullptr) return false;
    size_t n = 0;
    for (const char *p = pkg; *p; p++, n++) {
        char c = *p;
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '.' || c == '_';
        if (!ok) return false;
    }
    return n < 128;
}

// Config modo script-directory: carrega TODO *.js do moddir, sem filtro
// (o gadget só existe dentro do processo desse jogo mesmo). on_change
// ignore = determinístico (reinstala + reinicia o jogo).
static inline int uf_build_config(char *out, size_t outsz, const char *moddir) {
    return snprintf(out, outsz,
                    "{\"interaction\":{\"type\":\"script-directory\",\"path\":\"%s\",\"on_change\":\"ignore\"}}",
                    moddir);
}

// ---- validação do config (achado crítico: gadget fora do modo script) ----
// O default do gadget é ListenInteraction com on_load=wait: config `{}`,
// sem interaction, ou type listen/connect CONGELA o jogo esperando cliente
// e abre socket. Então só passa JSON estrito, objeto no topo, com
// interaction.type == "script" | "script-directory" (chave única, sem
// escape). Qualquer dúvida (JSON inválido, NUL, >4KB, chave duplicada ou
// com escape) = recusa: falhar fechado é não carregar o gadget.

struct uf_json {
    const char *p, *end;
    int depth;
};

static inline void uf_json_ws(uf_json &j) {
    while (j.p < j.end && (*j.p == ' ' || *j.p == '\t' || *j.p == '\n' || *j.p == '\r')) j.p++;
}

static inline bool uf_json_hex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

// String JSON; devolve o conteúdo cru (sem aspas) e se tinha escape.
static inline bool uf_json_str(uf_json &j, const char **s, size_t *len, bool *esc) {
    if (j.p >= j.end || *j.p != '"') return false;
    const char *b = ++j.p;
    bool e = false;
    while (j.p < j.end) {
        unsigned char c = (unsigned char)*j.p;
        if (c == '"') {
            *s = b;
            *len = (size_t)(j.p - b);
            *esc = e;
            j.p++;
            return true;
        }
        if (c < 0x20) return false;
        if (c == '\\') {
            e = true;
            if (++j.p >= j.end) return false;
            char x = *j.p;
            if (x == 'u') {
                for (int i = 0; i < 4; i++)
                    if (++j.p >= j.end || !uf_json_hex(*j.p)) return false;
            } else if (x == '\0' || !strchr("\"\\/bfnrt", x)) {
                return false;
            }
        }
        j.p++;
    }
    return false;
}

static inline bool uf_json_digits(uf_json &j) {
    const char *b = j.p;
    while (j.p < j.end && *j.p >= '0' && *j.p <= '9') j.p++;
    return j.p > b;
}

static inline bool uf_json_num(uf_json &j) {
    if (j.p < j.end && *j.p == '-') j.p++;
    if (j.p < j.end && *j.p == '0') j.p++;
    else if (!uf_json_digits(j)) return false;
    if (j.p < j.end && *j.p == '.') { j.p++; if (!uf_json_digits(j)) return false; }
    if (j.p < j.end && (*j.p == 'e' || *j.p == 'E')) {
        j.p++;
        if (j.p < j.end && (*j.p == '+' || *j.p == '-')) j.p++;
        if (!uf_json_digits(j)) return false;
    }
    return true;
}

static inline bool uf_json_lit(uf_json &j, const char *w) {
    size_t n = strlen(w);
    if ((size_t)(j.end - j.p) < n || memcmp(j.p, w, n) != 0) return false;
    j.p += n;
    return true;
}

static inline bool uf_json_value(uf_json &j);

// Objeto JSON. Com want: chave com escape = recusa (poderia decodificar
// pra want), conta as ocorrências de want e guarda onde começa o valor.
static inline bool uf_json_obj(uf_json &j, const char *want, const char **val, int *count) {
    if (j.p >= j.end || *j.p != '{' || ++j.depth > 32) return false;
    j.p++;
    uf_json_ws(j);
    if (j.p < j.end && *j.p == '}') { j.p++; j.depth--; return true; }
    for (;;) {
        const char *k;
        size_t klen;
        bool esc;
        uf_json_ws(j);
        if (!uf_json_str(j, &k, &klen, &esc)) return false;
        uf_json_ws(j);
        if (j.p >= j.end || *j.p != ':') return false;
        j.p++;
        uf_json_ws(j);
        if (want) {
            if (esc) return false;
            if (klen == strlen(want) && memcmp(k, want, klen) == 0) {
                (*count)++;
                *val = j.p;
            }
        }
        if (!uf_json_value(j)) return false;
        uf_json_ws(j);
        if (j.p >= j.end) return false;
        if (*j.p == '}') { j.p++; j.depth--; return true; }
        if (*j.p != ',') return false;
        j.p++;
    }
}

static inline bool uf_json_arr(uf_json &j) {
    if (++j.depth > 32) return false;
    j.p++;  // '['
    uf_json_ws(j);
    if (j.p < j.end && *j.p == ']') { j.p++; j.depth--; return true; }
    for (;;) {
        uf_json_ws(j);
        if (!uf_json_value(j)) return false;
        uf_json_ws(j);
        if (j.p >= j.end) return false;
        if (*j.p == ']') { j.p++; j.depth--; return true; }
        if (*j.p != ',') return false;
        j.p++;
    }
}

static inline bool uf_json_value(uf_json &j) {
    uf_json_ws(j);
    if (j.p >= j.end) return false;
    const char *s;
    size_t n;
    bool e;
    switch (*j.p) {
        case '{': return uf_json_obj(j, nullptr, nullptr, nullptr);
        case '[': return uf_json_arr(j);
        case '"': return uf_json_str(j, &s, &n, &e);
        case 't': return uf_json_lit(j, "true");
        case 'f': return uf_json_lit(j, "false");
        case 'n': return uf_json_lit(j, "null");
        default: return uf_json_num(j);
    }
}

// true só se o config (buf, n bytes, n <= UF_CONFIG_MAX) for JSON válido
// com interaction.type script ou script-directory. why recebe (se não nulo)
// o type achado ou o motivo da recusa, pro log.
static inline bool uf_config_is_script_mode(const char *buf, size_t n, char *why, size_t whysz) {
    const char *reason = "json-invalido";
    const char *tv = nullptr;
    size_t tlen = 0;
    bool ok = false;
    if (buf == nullptr || n == 0) {
        reason = "vazio";
    } else if (n > UF_CONFIG_MAX) {
        reason = "maior-que-4KB";
    } else if (memchr(buf, '\0', n) == nullptr) {
        uf_json top = {buf, buf + n, 0};
        const char *iv = nullptr;
        int ic = 0;
        uf_json_ws(top);
        if (uf_json_obj(top, "interaction", &iv, &ic)) {
            uf_json_ws(top);
            if (top.p != top.end) {
                reason = "json-invalido";
            } else if (ic != 1) {
                reason = ic == 0 ? "sem-interaction(padrao-listen)" : "interaction-duplicado";
            } else {
                uf_json in = {iv, buf + n, 0};
                const char *tp = nullptr;
                int tc = 0;
                if (in.p >= in.end || *in.p != '{' || !uf_json_obj(in, "type", &tp, &tc)) {
                    reason = "interaction-nao-objeto";
                } else if (tc != 1) {
                    reason = tc == 0 ? "sem-type(padrao-listen)" : "type-duplicado";
                } else {
                    uf_json tj = {tp, buf + n, 0};
                    bool esc;
                    if (!uf_json_str(tj, &tv, &tlen, &esc) || esc) {
                        tv = nullptr;
                        reason = "type-nao-string";
                    } else {
                        ok = (tlen == 6 && memcmp(tv, "script", 6) == 0) ||
                             (tlen == 16 && memcmp(tv, "script-directory", 16) == 0);
                    }
                }
            }
        }
    }
    if (why && whysz) {
        if (tv) snprintf(why, whysz, "%.*s", (int)tlen, tv);
        else snprintf(why, whysz, "%s", reason);
    }
    return ok;
}
