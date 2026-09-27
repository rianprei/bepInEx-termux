// fuzz_selmix — alvo de fuzzing dos DEMAIS núcleos puros que o
// test/selftest_harness.cpp já exercita com ENTRADA EXTERNA (a que vem do
// usuário/companion/arquivo e não de constante no código).
//
// Isto fecha o alcance pedido: os parsers nomeados um a um têm harness
// próprio (fuzz_c4_line, fuzz_elf_preflight, fuzz_frida_config); este cobre
// o resto da superfície de string do mesmo harness, que é onde a entrada
// externa também vira memória do processo:
//
//   - bc_mods_parse / bc_mods_format: o .conf que o companion escreve em
//     /data/data/<pkg>/... e o Manager regrava. Round-trip parse->format->parse.
//   - bc_mod_watch_register / find / fire_changed: registro de chave vindo do
//     conf, com a tabela preenchida até o limite.
//   - bc_generic_allowlist_contains_buf: a allowlist de pacote é um .conf que
//     o usuário dá push por adb; o nome testado também é externo.
//   - bc_seq_take / bc_seq_learn / bc_seq_split: valor da property persist.*,
//     lido do JNI (string do processo) — buffer de tamanho variável no
//     chamador, então o cap também é fuzzado.
//   - bc_pattern_scan_buffer: pattern + segmento de memória, ambos externos
//     no caminho de dados do dump de assinaturas.
//   - dump_* (C5): nome de classe/método/campo do metadata do jogo, que é
//     entrada do usuário sob o ponto de vista do mod (e o Manager consome).
//   - up_sig_hash / up_dedupe_*: a chave de dedupe é o texto da REGRA, ou
//     seja, o que o u_patch leu do .bpatch do usuário.
//
// Compilar:
//   clang++ -std=c++17 -g -O1 -fsanitize=fuzzer,address,undefined \
//           -I../../jni fuzz_selmix.cpp -o fuzz_selmix

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdio>

#include "../../jni/bc_mods_conf.h"
#include "../../jni/bc_mod_graph.h"
#include "../../jni/bc_signal.h"
#include "../../jni/bc_pattern_scan.h"
#include "../../jni/bc_generic_allowlist.h"
#include "../../jni/bc_path_decide.h"
#include "../../jni/bc_crashguard.h"
#include "../../mods/common/dump_core.h"
#include "../../mods/u_patch/jni/u_patch_dedupe.h"

namespace {

constexpr size_t kMaxInput = 32 * 1024;

char g_buf[kMaxInput + 1];

static const char *const T_SRC_DOMAIN[] = {"game", "companion", nullptr};
static const struct bc_mod_schema T_SCHEMA[] = {
    {"appInit",       BC_MOD_BOOL, true,  0, 0, 0,    nullptr, nullptr},
    {"appUpdateDraw", BC_MOD_BOOL, true,  0, 0, 0,    nullptr, nullptr},
    {"appTouch",      BC_MOD_BOOL, true,  0, 0, 0,    nullptr, nullptr},
    {"appKey",        BC_MOD_BOOL, true,  0, 0, 0,    nullptr, nullptr},
    {"throttle_every",BC_MOD_INT,  false, 1, 600, 60, nullptr, nullptr},
    {"stream_source", BC_MOD_ENUM, false, 0, 0, 0,    T_SRC_DOMAIN, "game"},
};
static const int T_SCHEMA_N = (int)(sizeof(T_SCHEMA) / sizeof(T_SCHEMA[0]));

static int g_watch_calls;
static void w_dummy(const char *, const struct bc_mod_entry *, const struct bc_mod_entry *) {
    g_watch_calls++;
}

// Derivados do primeiro byte: TAMANHOS de buffer e escolhas de sub-alvo. É o
// que faz o fuzzer alcançar o bug de "buffer pequeno" e não só o de
// "conteúdo".
//
// REGRA DESTE HARNESS: cap passado ao parser == sizeof do buffer de verdade.
// Todo chamador real (main.cpp:1461, 1567, mods/u_dump) faz
// `bc_seq_split(buf, key, sizeof(key), name, sizeof(name))` — o cap É o
// tamanho do buffer, e o parser é responsável por caber dentro dele. Passar
// cap MAIOR que o buffer seria erro do harness, não bug do parser, e o
// ASan acusaria o harness. Por isso os buffers abaixo são dimensionados pelo
// cap derivado, e não o contrário.
struct Derived {
    size_t small;     // 0..8   → cap de buffer minúsculo
    size_t medium;    // 1..72  → cap de buffer médio
    bool do_conf;     // alterna config/seq/allowlist/dump
    bool do_scan;
    bool do_dump;
};

Derived derive(const uint8_t *data, size_t size) {
    Derived d = {1, 16, true, true, true};
    if (size == 0) return d;
    uint8_t c = data[0];
    d.small = c % 9;
    d.medium = (size_t)((c / 3) % 72) + 1;
    d.do_conf = (c % 2) == 0;
    d.do_scan = (c % 3) != 2;
    d.do_dump = (c % 5) != 4;
    return d;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size > kMaxInput) size = kMaxInput;
    memcpy(g_buf, data, size);
    g_buf[size] = '\0';

    Derived d = derive(data, size);

    // ---- bc_mods_parse / format / watch ---------------------------------
    if (d.do_conf) {
        struct bc_mod_entry e[BC_MODS_CONF_MAX];
        struct bc_mod_entry e2[BC_MODS_CONF_MAX];
        int n = bc_mods_parse(g_buf, T_SCHEMA, T_SCHEMA_N, e, BC_MODS_CONF_MAX);
        if (n < 0) n = 0;
        char out[80];
        int w = bc_mods_format(T_SCHEMA, T_SCHEMA_N, e, n, out, sizeof(out));
        if (w > 0) {
            int n2 = bc_mods_parse(out, T_SCHEMA, T_SCHEMA_N, e2, BC_MODS_CONF_MAX);
            if (n2 > n) n2 = n;
            (void)bc_mods_equal(e, e2, n2);
        }
        (void)bc_mods_format(T_SCHEMA, T_SCHEMA_N, e, n, out, d.small);
        (void)bc_mods_schema_find(T_SCHEMA, T_SCHEMA_N, g_buf);
        for (int i = 0; i < n; i++) {
            char v[80];
            (void)bc_mods_value_str(&T_SCHEMA[i], &e[i], v, sizeof(v));
            (void)bc_mods_value_str(&T_SCHEMA[i], &e[i], v, d.small);
            (void)bc_mods_entry_equal(&e[i], &e2[i]);
        }

        static bc_mod_watch wt[BC_MODS_CONF_MAX];
        int count = 0;
        g_watch_calls = 0;
        (void)bc_mod_watch_register(wt, BC_MODS_CONF_MAX, &count, g_buf, w_dummy);
        (void)bc_mod_watch_find(wt, count, g_buf);
        // Enche a tabela até o limite (o registro de chave é O(n), o
        // registro de 512 chaves é o pior caso do caminho de insert).
        for (int i = 0; i < BC_MODS_CONF_MAX; i++) {
            char kn[32];
            snprintf(kn, sizeof(kn), "k%d", i);
            (void)bc_mod_watch_register(wt, BC_MODS_CONF_MAX, &count, kn, w_dummy);
        }
        (void)bc_mod_watch_fire_changed(wt, count, e, e2, n);
    }

    // ---- bc_seq_* : property persist.* ----------------------------------
    // Buffers dimensionados pelo cap (ver a regra acima), nunca o contrário.
    {
        char last_big[80] = {};
        (void)bc_seq_learn(g_buf, last_big, d.medium);
        (void)bc_seq_take(g_buf, last_big, d.medium);
        // cap 0 = o chamador recusa antes; o parser também tem de recusar.
        (void)bc_seq_take(g_buf, last_big, 0);
        (void)bc_seq_learn(g_buf, last_big, 0);
        // O payload nomeado "<seq> <nome>" com key e payload de caps
        // diferentes — é assim que main.cpp chama (unbuf/unkey/unname).
        {
            char key[80] = {}, payload[80] = {};
            (void)bc_seq_split(g_buf, key, d.medium, payload, d.small);
            (void)bc_seq_split(g_buf, key, d.small, payload, d.medium);
            (void)bc_seq_split(g_buf, key, 1, payload, 1);
            (void)bc_seq_split(g_buf, key, 0, payload, d.medium);
            (void)bc_seq_split(g_buf, key, d.medium, payload, 0);
            // Aliasing: key == payload (o Caso 59 exercita o par, e um
            // chamador descuidado poderia reusar o buffer).
            char alias[80] = {};
            (void)bc_seq_split(g_buf, alias, d.medium, alias, d.medium);
        }
    }

    // ---- allowlist de pacote ---------------------------------------------
    {
        char pkg[256];
        size_t pl = size < sizeof(pkg) - 1 ? size : sizeof(pkg) - 1;
        memcpy(pkg, g_buf, pl);
        pkg[pl] = '\0';
        (void)bc_generic_allowlist_contains_buf(g_buf, pkg);
        (void)bc_generic_allowlist_contains_buf(g_buf, "com.foo.bar");
    }

    // ---- crashguard: números de morte vindos de fora -------------------
    {
        long long now = (long long)size;
        int cnt = (int)(size & 0xffff);
        int ts = (int)(d.medium);
        (void)bc_crashguard_blocks(cnt, ts, now);
        (void)bc_crashguard_next_count(cnt, ts, now);
        (void)bc_decide_path(g_buf[0] ? g_buf : nullptr, size & 1, size & 2);
    }

    // ---- bc_pattern_scan_buffer: pattern + segmento do input ------------
    if (d.do_scan) {
        uint8_t seg[512];
        size_t slen = size < sizeof(seg) ? size : sizeof(seg);
        for (size_t i = 0; i < slen; i++) seg[i] = g_buf[i];
        bc_pattern pat = {};
        pat.len = 1 + (size % (BC_PATTERN_MAX_BYTES - 1));
        for (size_t j = 0; j < BC_PATTERN_MAX_BYTES; j++) {
            pat.bytes[j] = (uint8_t)(j < size ? g_buf[j] : 0);
            // mask 0 = wildcard, não-zero = byte fixo: alterna pra achar
            // pattern de 1 byte (o caso ambíguo por construção).
            pat.mask[j] = (uint8_t)((j % 2) ? 1 : 0);
        }
        size_t off = 0;
        (void)bc_pattern_scan_buffer(seg, slen, &pat, &off);
        (void)bc_pattern_scan_buffer(seg, 0, &pat, &off);
        // len > len do buffer: precisa recusar antes de varrer.
        bc_pattern big = pat;
        big.len = BC_PATTERN_MAX_BYTES;
        (void)bc_pattern_scan_buffer(seg, slen, &big, &off);
    }

    // ---- dump C5: nome de classe/campo vem do metadata ------------------
    if (d.do_dump) {
        char out[80];
        (void)dump_write_header(out, sizeof(out), g_buf, (long long)size, g_buf);
        (void)dump_write_header(out, d.medium, g_buf, (long long)size, g_buf);
        (void)dump_write_header(out, 1, g_buf, (long long)size, g_buf);
        (void)dump_write_class(out, sizeof(out), g_buf, g_buf);
        (void)dump_write_class(out, d.medium, g_buf, g_buf);
        (void)dump_write_method(out, sizeof(out), g_buf, g_buf, (int)d.small, g_buf, size & 1);
        (void)dump_write_method(out, d.small, g_buf, g_buf, (int)d.medium, g_buf, size & 1);
        (void)dump_write_field(out, sizeof(out), g_buf, g_buf, g_buf, size & 1, size);
        (void)dump_write_field(out, d.medium, g_buf, g_buf, g_buf, size & 1, size);
        {
            // parts derivado do input: nome aninhado com '/' e NUL no meio.
            const char *parts[4];
            int np = 0;
            for (int i = 0; i < 4; i++) {
                if ((size >> (i + 1)) % 3 == 0) {
                    parts[np++] = ((size >> i) & 1) ? g_buf : nullptr;
                }
            }
            (void)dump_join_class_name(out, sizeof(out), g_buf, parts, np);
            (void)dump_join_class_name(out, d.medium, g_buf, parts, np);
            (void)dump_join_class_name(out, 1, g_buf, parts, np);
        }
        {
            char pkg[80];
            (void)dump_pick_pkg(g_buf, g_buf, pkg, sizeof(pkg));
            (void)dump_pick_pkg(g_buf, g_buf, pkg, d.medium);
            (void)dump_pick_pkg(g_buf, g_buf, pkg, 1);
        }
    }

    // ---- dedupe do u_patch: a chave é o texto da REGRA lida do .bpatch ---
    {
        uint64_t h = up_sig_hash(g_buf);
        (void)up_dedupe_should_log(nullptr, 0, h);
        static up_applied_t t[UP_APPLIED_MAX];
        int n = 0;
        (void)up_dedupe_mark(t, &n, UP_APPLIED_MAX, h, UP_ST_SEEN);
        (void)up_dedupe_mark(t, &n, UP_APPLIED_MAX, h, UP_ST_OK);
        (void)up_dedupe_find(t, n, h);
        (void)up_dedupe_should_log(t, n, h);
        for (int i = 0; i < UP_APPLIED_MAX; i++) {
            char k[32];
            snprintf(k, sizeof(k), "regra-%d", i);
            (void)up_dedupe_mark(t, &n, UP_APPLIED_MAX, up_sig_hash(k), UP_ST_SEEN);
        }
        (void)up_dedupe_full(n, UP_APPLIED_MAX);
    }

    return 0;
}
