// fuzz_c4_line — alvo de fuzzing das LINHAS .bpatch/.conf do u_patch.
//
// Cobre o contrato C4 inteiro (mods/u_patch/jni/u_patch_parse.h, header-only,
// sem plataforma) com entrada de usuário: up_tokenize, up_parse_line,
// up_parse_type, up_parse_nargs, up_split_class, up_foreach_line e
// up_conf_get (opções C3). É o MESMO código que roda DENTRO do processo do
// jogo: um crash aqui derruba o jogo, e 2 mortes em 20s acionam o crashguard.
//
// O que o harness exercita, por exec:
//   1. a linha inteira como 1 regra C4 (up_parse_line);
//   2. o buffer inteiro como ARQUIVO de regras (up_foreach_line + o mesmo
//      up_parse_line por linha, com nº de linha e buffer de destino fuzzados);
//   3. up_split_class com TAMANHOS de buffer derivados do input (o achado
//      #12 foi exatamente nsz/nmsz == 0 virando memcpy de SIZE_MAX);
//   4. up_conf_get com chave derivada do input (parsing "key=value" C3).
//
// Sem alocação por exec: tudo em buffer de pilha, então leak só aparece se o
// parser alocar — por isso o gate roda com ASan (LeakSanitizer incluído).
//
// Compilar:
//   clang++ -std=c++17 -g -O1 -fsanitize=fuzzer,address,undefined \
//           -I../../mods/u_patch/jni fuzz_c4_line.cpp -o fuzz_c4_line

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdio>

#include "../../mods/u_patch/jni/u_patch_parse.h"

namespace {

// Limite de entrada: um .bpatch/.conf de verdade tem poucas linhas, mas o
// parser precisa ser limitado de qualquer jeito (o .bpatch vem de um arquivo
// que o usuário/Manager controla). Acima disso o resto é ignorado — mesmo
// caminho do truncamento real (o mod lê o arquivo com teto).
constexpr size_t kMaxInput = 8192;

// Um .bpatch de 4 KB é o teto prático; acima disso o buffer de linha do
// mod não dá conta mesmo. Mantemos folga pra exercitar truncamento.
char g_buf[kMaxInput + 1];

struct Ctx {
    int calls;
    int last_lineno;
    // tampos de saída derivados do input: o achado #12 (nsz/nmsz == 0) só
    // aparece se o harness variar o TAMANHO, não só o conteúdo.
    size_t ns_cap;
    size_t nm_cap;
    up_rule_t rule;
};

int fuzz_line_cb(char *line, int lineno, void *vctx) {
    Ctx *ctx = (Ctx *)vctx;
    ctx->calls++;
    ctx->last_lineno = lineno;
    int rc = up_parse_line(line, &ctx->rule);

    // Se a linha casou, o que o mod faz com a regra também recebe dado do
    // usuário: classe vira (ns, nome) e o valor vira escrita checada.
    if (rc == 0) {
        char ns[128];
        char name[128];
        if (up_split_class(ctx->rule.cls, ns, ctx->ns_cap, name, ctx->nm_cap)) {
            (void)up_is_zygote(ctx->rule.cls);
            // Tipo declarado pelo jogo (host-side we não temos il2cpp, mas o
            // verificador de escrita é puro e recebe string do dump).
            char why[320];
            size_t want = ctx->rule.type == UP_BOOL ? 1u : 4u;
            (void)up_value_type_check(false, "System.Int32", want, why, sizeof(why));
            (void)up_value_type_check(true, name, want, why, sizeof(why));
            (void)up_value_size_by_name(name);
        }
    }
    return 0;
}

// Derivados determinísticos do byte de controle (primeiro byte do input):
// usados pra variar tameiros e o teto de linhas sem gastar entropia.
struct Derived {
    size_t ns_cap;    // 0..8
    size_t nm_cap;    // 0..8
    int max_lines;    // 0 = sem teto, 1..16
    size_t conf_key;  // 0..7
};

Derived derive(const uint8_t *data, size_t size) {
    Derived d = {1, 1, 0, 0};
    if (size == 0) return d;
    uint8_t c = data[0];
    d.ns_cap = c % 9;             // inclui 0 (o caso do achado #12)
    d.nm_cap = (c / 9) % 9;       // idem, independente do ns
    d.max_lines = (c % 3 == 0) ? ((c / 5) % 16) : 0;
    d.conf_key = (c / 7) % 8;
    return d;
}

const char *const kConfKeys[] = {
    "appInit", "throttle_every", "stream_source", "on", "", "=", "a", "#x",
};

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size > kMaxInput) size = kMaxInput;
    // up_tokenize/up_parse_line DESTRÓIEM o buffer (escrevem NUL). Copia com
    // NUL final garantido: é assim que o mod entrega a linha (read() + \0).
    memcpy(g_buf, data, size);
    g_buf[size] = '\0';

    Derived d = derive(data, size);

    // 1) a linha inteira como 1 regra C4.
    {
        char line[kMaxInput + 1];
        memcpy(line, g_buf, size + 1);
        up_rule_t r;
        (void)up_parse_line(line, &r);
    }

    // 1b) up_tokenize direto: o mesmo tokenizador, com maxtok reduzido,
    //     para pegar leitura de tok[] além do que a linha parseou.
    {
        char line[kMaxInput + 1];
        memcpy(line, g_buf, size + 1);
        char *tok[8];
        int n = up_tokenize(line, tok, 8);
        for (int i = 0; i < n; i++) {
            int na = 0;
            (void)up_parse_nargs(tok[i], &na);
            up_type_t t;
            (void)up_parse_type(tok[i], &t);
        }
    }

    // 2) o buffer como arquivo de regras.
    {
        char file[kMaxInput + 1];
        memcpy(file, g_buf, size + 1);
        Ctx ctx = {};
        ctx.ns_cap = d.ns_cap;
        ctx.nm_cap = d.nm_cap;
        int visited = up_foreach_line(file, fuzz_line_cb, &ctx, d.max_lines);
        // Guarda de loop infinito: o contrato é visitar <= nº de '\n' + 1.
        if (visited < 0 || visited > (int)size + 1) __builtin_trap();
    }

    // 3) up_split_class com tameiros varios (inclui 0, achado #12).
    {
        char ns[256];
        char name[256];
        (void)up_split_class(g_buf, ns, d.ns_cap, name, d.nm_cap);
        (void)up_split_class(g_buf, ns, sizeof(ns), name, d.nm_cap);
        (void)up_split_class(g_buf, ns, d.ns_cap, name, sizeof(name));
    }

    // 4) opções C3: up_conf_get sobre o mesmo buffer, com chave do input.
    //    A chave "" é o caso degenerado (klen == 0 casa em toda linha) e
    //    fica na lista de propósito: é o pior caso do strncmp/advance.
    {
        const char *key = kConfKeys[d.conf_key];
        char out[64];
        (void)up_conf_get(g_buf, key, out, sizeof(out));
        char tiny[1];
        (void)up_conf_get(g_buf, key, tiny, sizeof(tiny));
    }

    return 0;
}
