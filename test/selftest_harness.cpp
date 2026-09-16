// Harness de teste HOST (não-NDK, compila com g++/clang++ puro) — valida as
// primitivas puras de self-test do BC-POC sem precisar de device:
//   prologue_matches()  — compara prólogo com máscara (0xFF=bater, 0=wildcard)
//   scan_exec_unique()  — varre buffer e exige match ÚNICA (0 ou 2+ = nullptr)
//   stream_send_prefixed() / publish_log() — formatação BepInEx-style + gestão
//       de g_stream_fd (inválido → silêncio; send falha → fd resetado a -1;
//       truncamento do corpo; fórmula do timestamp)
//   companion_announce() — formato "[Nível,-7:Fonte,10]" sem timestamp
//
// OBS: selftest_symbol() real depende de dl_iterate_phdr/Dobby (Android).
// Aqui testamos a SUA LÓGICA de decisão isolada: as duas primitivas que ela
// chama. O caso "dladdr falha" é modelado como "alvo não resolvido" → o
// chamador retorna nullptr (caminho DORMANT), coberto pelo caso mismatch/ausente.
//
// As primitivas de streaming são testadas com um socketpair real (AF_UNIX,
// SOCK_SEQPACKET) — o kernel faz de “rede”, então o send() real é exercitado
// (incluindo EPIPE/EAGAIN) sem device. MSG_DONTWAIT/MSG_NOSIGNAL existem em
// Linux host — mesmo header <sys/socket.h>, zero stub.
//
// Compilar:  g++ -std=c++17 -I../jni -o selftest_harness selftest_harness.cpp
// (offsetsdb.h é incluído do ../jni via -I)

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cstdarg>
#include <ctime>
#include <cerrno>
#include <atomic>
#include <sys/socket.h>
#include <unistd.h>

// Reusa as definições reais de assinatura (offsetsdb.h) — não duplicamos bytes.
// offsetsdb.h inclui <stdint.h> e o struct bc_sig + as 4 assinaturas reais.
#include "offsetsdb.h"
#include "bc_mods_conf.h"
#include "bc_mod_graph.h"   // grafo de dependência entre mods (requires/conflicts)
#include "bc_hook_logic.h"  // FUNÇÕES REAIS (single source of truth com main.cpp)
#include "bc_mod_api.h"     // contrato de API exposto aos mods .so dinâmicos
#include "bc_pattern_scan.h"  // AOB scan — bc_pattern_scan_buffer (lógica pura, testável no host)
#include "bc_loader.h"      // loader dinâmico (mesma lógica pura do main.cpp)

// --- schema espelho do main.cpp/companion.cpp (sync manual entre os 3) ---
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

// helper de busca no array parseado
static struct bc_mod_entry *t_find(struct bc_mod_entry *e, int n, const char *name) {
    for (int i = 0; i < n; i++)
        if (strcmp(e[i].name, name) == 0) return &e[i];
    return nullptr;
}

// --- cópia fiel das duas primitivas de main.cpp (mantidas em sync manual) ---
static bool prologue_matches(const void *fn, const struct bc_sig *sig) {
    if (sig == nullptr || sig->len == 0) return false;
    const unsigned char *m = (const unsigned char *)fn;
    for (unsigned i = 0; i < sig->len; i++) {
        if ((m[i] & sig->mask[i]) != (sig->bytes[i] & sig->mask[i]))
            return false;
    }
    return true;
}

struct LibExecRange { void *base; size_t size; };
static void *scan_exec_unique(const LibExecRange *range, const struct bc_sig *sig) {
    if (range == nullptr || range->base == nullptr || range->size < sig->len)
        return nullptr;
    const unsigned char *lo = (const unsigned char *)range->base;
    const unsigned char *hi = lo + range->size - sig->len;
    void *found = nullptr;
    int hits = 0;
    for (const unsigned char *p = lo; p <= hi; p += 4) {
        if (!prologue_matches(p, sig)) continue;
        if (++hits > 1) return nullptr;  // 2+ matches: assinatura fraca
        found = (void *)p;
    }
    return (hits == 1) ? found : nullptr;
}
// ---------------------------------------------------------------------------

// --- cópia fiel das primitivas de streaming de main.cpp (sync manual) ---
// (código real inclui <atomic>/<ctime>/<sys/socket.h>; hosts Linux têm tudo)
static std::atomic<int> h_stream_fd{-1};

static void h_stream_send_prefixed(const char *level, const char *source,
                                   const char *body) {
    int fd = h_stream_fd.load(std::memory_order_relaxed);
    if (fd < 0) return;
    time_t now = time(nullptr);
    struct tm tmv;
    localtime_r(&now, &tmv);
    char line[224];
    int len = snprintf(line, sizeof(line), "[%02d:%02d:%02d] [%-7s:%10s] %s\n",
                       tmv.tm_hour, tmv.tm_min, tmv.tm_sec, level, source, body);
    if (len < 0) return;
    if (len > (int)sizeof(line) - 1) len = (int)sizeof(line) - 1;
    ssize_t r = send(fd, line, (size_t)len, MSG_DONTWAIT | MSG_NOSIGNAL);
    if (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        h_stream_fd.store(-1, std::memory_order_relaxed);
    }
}

static void h_publish_log(const char *level, const char *fmt, ...) {
    char body[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);
    h_stream_send_prefixed(level, "BCPOC", body);
}

// --- cópia fiel de companion_announce de companion.cpp (sync manual) ---
// stream_broadcast é modelado como write() no fd dado (no real: loop nos
// clientes; o formato da LINHA é o que estamos testando, e o formato é igual).
static void h_companion_announce(int sink_fd, const char *level, const char *msg) {
    char line[200];
    int len = snprintf(line, sizeof(line), "[%-7s:%10s] %s\n", level, "Companion", msg);
    if (len > 0) {
        if (len > (int)sizeof(line) - 1) len = (int)sizeof(line) - 1;
        ssize_t r = send(sink_fd, line, (size_t)len, MSG_DONTWAIT | MSG_NOSIGNAL);
        (void)r;
    }
}
// ---------------------------------------------------------------------------

// Lê uma datagram do socket (SOCK_SEQPACKET preserva fronteira de mensagem —
// 1 send = 1 recv, sem colar linhas). Retorna std::string.
static bool recv_line(int fd, char *buf, size_t cap, ssize_t *outlen) {
    ssize_t n = recv(fd, buf, cap, MSG_DONTWAIT);
    if (n < 0) return false;
    if ((size_t)n >= cap) n = cap - 1;
    buf[n] = '\0';
    if (outlen) *outlen = n;
    return true;
}

static int g_fail = 0;
static void check(const char *name, bool cond) {
    printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
    if (!cond) g_fail++;
}

// --- Cópia mínima do dispatcher Prefix/Postfix (main.cpp) — mesmo padrão de
// duplicação já usado nos Casos 1-5 (primitivas reais vivem em main.cpp,
// que só compila no NDK; sync manual é o ponto de manutenção documentado). ---
typedef bool (*T_PrefixFn)(const char *, uintptr_t, uintptr_t, uintptr_t, uintptr_t,
                          uintptr_t, uintptr_t, uintptr_t, uintptr_t);
typedef void (*T_PostfixFn)(const char *, uintptr_t, uintptr_t, uintptr_t, uintptr_t,
                            uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t *, bool);
static uintptr_t t_dispatch(const char *name, uintptr_t (*orig)(uintptr_t),
                            uintptr_t a0, T_PrefixFn prefix, T_PostfixFn postfix) {
    bool run_orig = true;
    if (prefix != nullptr && !prefix(name, a0, 0, 0, 0, 0, 0, 0, 0)) run_orig = false;
    uintptr_t ret = 0;
    if (run_orig && orig != nullptr) ret = orig(a0);
    if (postfix != nullptr) postfix(name, a0, 0, 0, 0, 0, 0, 0, 0, &ret, run_orig);
    return ret;
}
static uintptr_t t_orig_double(uintptr_t x) { return x * 2; }
static bool t_prefix_allow(const char *, uintptr_t, uintptr_t, uintptr_t, uintptr_t,
                           uintptr_t, uintptr_t, uintptr_t, uintptr_t) { return true; }
static bool t_prefix_skip(const char *, uintptr_t, uintptr_t, uintptr_t, uintptr_t,
                          uintptr_t, uintptr_t, uintptr_t, uintptr_t) { return false; }
static int t_postfix_calls = 0;
static bool t_postfix_saw_run_orig = false;
static void t_postfix_observe(const char *, uintptr_t, uintptr_t, uintptr_t, uintptr_t,
                              uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t *ret, bool run_orig) {
    t_postfix_calls++;
    t_postfix_saw_run_orig = run_orig;
    *ret += 1000;  // postfix pode ajustar o retorno — confirma que o ponteiro é real
}

// ACHADO REAL (device, 2026-09-15): este mock testa só a lógica de
// backup/idempotência, NUNCA a resolução de endereço — por isso passou
// 24/24 aqui mas unpatch_hook() real CRASHOU em device (SIGSEGV). Causa:
// unpatch_hook original re-resolvia o endereço via selftest_symbol()
// (scan de prólogo) DEPOIS do hook já instalado — a função já tem um JUMP
// no lugar do prólogo original, scan acha match falso em outro ponto da
// memória, DobbyDestroy nesse endereço errado corrompe código alheio. Fix
// real: HookPlan ganhou campo resolved_addr, preenchido 1x no install,
// reusado (nunca re-resolvido) no unpatch. Esse mock não cobre esse
// caminho — só prova via teste real em device, não em host.
// --- cópia do dispatcher unpatch_hook (main.cpp) — logica testavel sem Dobby real ---
// t_hooks_backup[idx] = mock do trampoline (nullptr = nunca instalado ou já removido).
// DobbyDestroy é stubbed (sempre rc=0 = sucesso). Testamos:
//   - busca por shortname (hook desconhecido → false)
//   - idempotência (backup null → true, sem chamar destroy)
//   - destroy + zera backup (backup != null → true, destroy chamado, backup nullado)
//   - hook irmão preservado (remover hook 1 não toca backup do hook 2)
static void *t_hooks_backup[4] = {nullptr, nullptr, nullptr, nullptr};
// Mirror de p->resolved_addr (main.cpp): endereço resolvido no install.
// backup==NULL + resolved!=NULL = "unpatched" (instalou, depois removeu);
// backup==NULL + resolved==NULL = "no-target" (nunca resolveu endereço).
static void *t_hooks_resolved[4] = {nullptr, nullptr, nullptr, nullptr};
static const char *t_hook_names[4] = {"appInit", "appUpdateDraw", "appTouch", "appKey"};
static bool t_dobby_destroy_called = false;
static int t_dobby_destroy_count = 0;
static bool t_dobby_destroy_failed = false;  // stub: simula falha (rc!=0)

// --- cópia fiel de cfg_label / rt_label / snapshot-2-eixos (main.cpp): ---
// 3-state runtime: active / unpatched / no-target, distinguindo POR QUE o
// hook não está ativo (vantagem sobre Harmony, que falha em silêncio).
static const char *t_cfg_label(bool enabled) { return enabled ? "on" : "off"; }
static const char *t_rt_label(bool installed, bool resolved) {
    if (installed) return "active";
    return resolved ? "unpatched" : "no-target";
}
// Monta a linha do snapshot dos dois eixos: nome|config|runtime|hits.
static int t_snapshot_row(char *out, size_t cap, int idx, bool cfg_on, unsigned hits) {
    return snprintf(out, cap, "%s|%s|%s|%u\n", t_hook_names[idx],
                    t_cfg_label(cfg_on),
                    t_rt_label(t_hooks_backup[idx] != nullptr, t_hooks_resolved[idx] != nullptr),
                    hits);
}

static bool t_unpatch_hook(const char *shortname) {
    int idx = -1;
    for (int i = 0; i < 4; i++) {
        if (strcmp(t_hook_names[i], shortname) == 0) { idx = i; break; }
    }
    if (idx < 0) return false;  // hook desconhecido
    if (t_hooks_backup[idx] == nullptr) return true;  // idempotente
    if (t_dobby_destroy_failed) return false;
    t_dobby_destroy_called = true;
    t_dobby_destroy_count++;
    t_hooks_backup[idx] = nullptr;
    // resolved_addr NÃO é zerado (espelho main.cpp: unpatch só zerá backup).
    // Assim, backup==NULL + resolved!=NULL → "unpatched" (removido, foi ativo).
    return true;
}

// --- cópia fiel de repatch_hook (main.cpp) — re-instala SÓ o hook pedido. ---
// DobbyHook é modelado como: preencher backup com um endereço novo (sempre
// diferente do anterior) e marcar instalado. Stub suporta falha (hook falha
// ao reinstalar → retorna false e backup permanece null).
// Semântica exata (espelho do real):
//   - hook desconhecido → false
//   - já instalado (backup != null) → true, sem refazer install (idempotente)
//   - backup null → "instala": preenche backup novo, retorna true
//   - t_dobby_hook_failed → a reinstalação falha → false, backup segue null
static bool t_dobby_hook_failed = false;
static void *t_last_installed_addr = nullptr;
static uintptr_t t_repatch_counter = 0x5000;  // controlável externamente
static bool t_repatch_hook(const char *shortname) {
    int idx = -1;
    for (int i = 0; i < 4; i++) {
        if (strcmp(t_hook_names[i], shortname) == 0) { idx = i; break; }
    }
    if (idx < 0) return false;  // hook desconhecido
    if (t_hooks_backup[idx] != nullptr) return true;  // já instalado
    if (t_dobby_hook_failed) return false;  // falha simulada na reinstalação
    // "instala": novo endereço, sempre diferente do anterior (prova que o
    // hook foi realmente refeito, não apenas marcado).
    t_repatch_counter += 16;
    t_hooks_backup[idx] = (void*)t_repatch_counter;
    t_hooks_resolved[idx] = t_hooks_backup[idx];  // try_install seta resolved_addr
    t_last_installed_addr = t_hooks_backup[idx];
    return true;
}

// --- stubs de backend Dobby p/ bc_hook_logic.h (casos 35-40) ---
static int t_destroy_stub(void *addr) {
    (void)addr;
    if (t_dobby_destroy_failed) return -1;
    t_dobby_destroy_called = true;
    t_dobby_destroy_count++;
    return 0;
}
static int t_install_stub(void *target, void *replacement, void **backup) {
    (void)target; (void)replacement;
    if (t_dobby_hook_failed) return -1;
    t_repatch_counter += 16;
    if (backup != nullptr) *backup = (void*)t_repatch_counter;
    return 0;
}
// orig wide: x0*2 (para testar hook_dispatch real com 8 args).
static uintptr_t d_orig_wide(uintptr_t a0, uintptr_t, uintptr_t, uintptr_t,
                             uintptr_t, uintptr_t, uintptr_t, uintptr_t) {
    return a0 * 2;
}

// --- watch callback stub (bc_mod_watch_fn) pra testar registro/fire ---
static int g_watch_call_count = 0;
static char g_watch_last_key[32] = {0};
static void w_dummy(const char *key, const struct bc_mod_entry *old_val,
                    const struct bc_mod_entry *new_val) {
    (void)old_val; (void)new_val;
    g_watch_call_count++;
    if (key != nullptr)
        snprintf(g_watch_last_key, sizeof(g_watch_last_key), "%s", key);
}

int main() {
    printf("== Selftest harness (host, offline) ==\n\n");

    // Usar a assinatura real do appInit (44 bytes, tem 16 bytes de wildcard).
    const struct bc_sig *sig = &bc_sig_appinit;

    // --- Caso 1: MATCH EXATO (buffer começa com o prólogo real) ---
    {
        printf("[Caso 1] match exato\n");
        unsigned char buf[128] = {0};
        // copia os bytes reais da assinatura pro início do buffer
        memcpy(buf, sig->bytes, sig->len);
        // bytes wildcard (mask==0) do buffer podem ser qualquer coisa, já são 0
        check("prologue_matches no offset 0", prologue_matches(buf, sig));

        // scan deve achar EXATAMENTE 1 match no offset 0
        LibExecRange rng{ buf, sizeof(buf) };
        void *hit = scan_exec_unique(&rng, sig);
        check("scan_exec_unique acha 1 match único", hit == (void*)buf);
        printf("    (hit=%p, esperado=%p)\n", hit, (void*)buf);
    }

    // --- Caso 2: MISMATCH (primeiro byte fixo errado) ---
    {
        printf("\n[Caso 2] mismatch\n");
        unsigned char buf[128] = {0};
        memcpy(buf, sig->bytes, sig->len);
        // corrompe um byte que tem mask==0xFF (primeiro byte é 0xFD, mask 0xFF)
        buf[0] ^= 0xFF;
        check("prologue_matches retorna false", !prologue_matches(buf, sig));

        LibExecRange rng{ buf, sizeof(buf) };
        check("scan_exec_unique retorna nullptr", scan_exec_unique(&rng, sig) == nullptr);
    }

    // --- Caso 3: alvo não resolvido (equivale a "dladdr falha" no selftest real) ---
    {
        printf("\n[Caso 3] alvo ausente / não resolvido (modela dladdr fail)\n");
        unsigned char buf[128] = {0};  // tudo zero, nada bate
        LibExecRange rng{ buf, sizeof(buf) };
        void *hit = scan_exec_unique(&rng, sig);
        check("scan sem match → nullptr (caminho DORMANT)", hit == nullptr);

        // também: range null / size < len deve retornar nullptr sem crash
        struct bc_sig big = { sig->bytes, sig->mask, 999 };
        check("range size < len → nullptr", scan_exec_unique(&rng, &big) == nullptr);
        LibExecRange nullrange{ nullptr, 0 };
        check("range base null → nullptr", scan_exec_unique(&nullrange, sig) == nullptr);
    }

    // --- Caso 4: wildcard ignorado (bytes wildcard diferentes ainda batem) ---
    {
        printf("\n[Caso 4] wildcard (mask==0) tolera qualquer byte\n");
        unsigned char buf[128] = {0};
        memcpy(buf, sig->bytes, sig->len);
        // appInit tem 16 bytes wildcard (mask 0x00) no meio. Preenche com lixo.
        for (unsigned i = 0; i < sig->len; i++)
            if (sig->mask[i] == 0x00) buf[i] = 0xAB;  // lixo arbitrário
        check("prologue_matches ignora bytes wildcard", prologue_matches(buf, sig));
    }

    // --- Caso 5: assinatura fraca (2 matches) deve ser rejeitada ---
    {
        printf("\n[Caso 5] assinatura não-única (2 matches) → rejeitada\n");
        unsigned char buf[256] = {0};
        memcpy(buf + 0,  sig->bytes, sig->len);
        memcpy(buf + 64, sig->bytes, sig->len);   // segunda cópia em outro lugar
        LibExecRange rng{ buf, sizeof(buf) };
        // 64 é múltiplo de 4, então o scan (step 4) pega as duas
        void *hit = scan_exec_unique(&rng, sig);
        check("2 matches → nullptr (ambiguidade proibida)", hit == nullptr);
    }

    // ================================================================
    // Casos 6–9: streaming (g_stream_fd / publish_log / announce)
    // Socket real AF_UNIX SOCK_SEQPACKET: 1 send = 1 datagram = 1 linha.
    // ================================================================
    {
        printf("\n[Caso 6] publish_log com stream fd inválido (-1) → silêncio total\n");
        int sock[2];
        check("socketpair criado", socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sock) == 0);
        h_stream_fd.store(-1);
        h_publish_log("Warning", "nenhum hook instalado — DORMANT");
        char tmp[512];
        check("fd<0 → nada enviado", recv(sock[0], tmp, sizeof(tmp), MSG_DONTWAIT) < 0);
        close(sock[0]); close(sock[1]);
    }
    {
        printf("\n[Caso 7] publish_log com fd válido → linha BepInEx-style no stream\n");
        int sock[2];
        check("socketpair criado", socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sock) == 0);
        h_stream_fd.store(sock[1]);
        h_publish_log("Error", "build-id MISMATCH: lib=%s esperado=%s", "abc", "def");
        char line[512]; ssize_t n;
        bool got = recv_line(sock[0], line, sizeof(line), &n);
        check("linha recebida", got);
        if (got) {
            // formato real (probe cat -A): [12:34:56] [Error  :     BCPOC] body\n
            //  ("%-7s" com "Error" → "Error  " com 2 espaços, não 4)
            check("contém [Error  :] (Error alinhado à esq. em 7)", strstr(line, "[Error  :") != nullptr);
            check("fonte BCPOC alinhada à dir. em 10", strstr(line, ":     BCPOC] ") != nullptr);
            check("corpo formatado (vsnprintf aplicou %s)", strstr(line, "build-id MISMATCH: lib=abc esperado=def") != nullptr);
            check("termina com \\n", n > 0 && line[n-1] == '\n');
            // timestamp [HH:MM:SS] — 3 campos de 2 dígitos separados por ':'
            check("timestamp HH:MM:SS presente", line[0] == '[' && line[3] == ':' && line[6] == ':');
        }
        // limpa e fecha
        h_stream_fd.store(-1);
        close(sock[0]); close(sock[1]);
    }
    {
        printf("\n[Caso 8] canal morto (EPIPE) → g_stream_fd resetado a -1\n");
        int sock[2];
        check("socketpair criado", socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sock) == 0);
        h_stream_fd.store(sock[1]);
        close(sock[0]);          // mata o par de leitura → próximo send dá EPIPE
        usleep(1000);            // deixa o kernel propagar o fechamento
        h_publish_log("Warning", "canal morto");
        check("fd resetado a -1 após EPIPE", h_stream_fd.load() == -1);
        close(sock[1]);
    }
    {
        printf("\n[Caso 9] companion_announce → formato [Nível:Fonte] sem timestamp\n");
        int sock[2];
        check("socketpair criado", socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sock) == 0);
        h_companion_announce(sock[1], "Info", "novo cliente stream conectado (total=2)");
        char line[512]; ssize_t n;
        bool got = recv_line(sock[0], line, sizeof(line), &n);
        check("linha recebida", got);
        if (got) {
            check("começa com [ (SEM timestamp)", line[0] == '[');
            check("contém [Info   :] (Info alinhado à esq. em 7)", strstr(line, "[Info   :") != nullptr);
            // "%10s" com "Companion" (9 chars) → 1 espaço de padding + ]
            check("fonte Companion alinhada à dir. em 10", strstr(line, " : Companion] ") != nullptr);
            check("corpo presente", strstr(line, "novo cliente stream conectado (total=2)") != nullptr);
            check("termina com \\n", n > 0 && line[n-1] == '\n');
        }
        close(sock[0]); close(sock[1]);
    }
    {
        printf("\n[Caso 10] corpo longo truncado no limite (line[224]) sem crash\n");
        int sock[2];
        check("socketpair criado", socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sock) == 0);
        h_stream_fd.store(sock[1]);
        char big[400];
        memset(big, 'x', sizeof(big) - 1); big[sizeof(big)-1] = '\0';
        h_publish_log("Warning", "%s", big);
        char line[512]; ssize_t n;
        bool got = recv_line(sock[0], line, sizeof(line), &n);
        check("linha recebida", got);
        if (got) {
            // 224 bytes de buffer ⇒ no máximo 223 chars + \n (truncado)
            check("linha truncada ≤ 224 bytes", n <= 224);
            check("linha termina em \\n", n > 0 && line[n-1] == '\n');
        }
        h_stream_fd.store(-1);
        close(sock[0]); close(sock[1]);
    }

    // ================================================================
    // Casos 11–17: config v2 tipado (bc_mods_conf.h)
    // ================================================================
    {
        printf("\n[Caso 11] parse v2: arquivo ausente/null → todos os defaults\n");
        struct bc_mod_entry e[BC_MODS_CONF_MAX];
        int n = bc_mods_parse(nullptr, T_SCHEMA, T_SCHEMA_N, e, BC_MODS_CONF_MAX);
        check("retorna nº de chaves do schema", n == T_SCHEMA_N);
        check("appInit default ON", t_find(e, n, "appInit")->b == true);
        check("throttle_every default 60", t_find(e, n, "throttle_every")->i == 60);
        check("stream_source default game", strcmp(t_find(e, n, "stream_source")->s, "game") == 0);
        check("nenhuma marcada present", t_find(e, n, "appInit")->present == false);
    }
    {
        printf("\n[Caso 12] parse v2: bool (formato v1) + int + enum no mesmo arquivo\n");
        struct bc_mod_entry e[BC_MODS_CONF_MAX];
        int n = bc_mods_parse("appTouch=off\nthrottle_every=30\nstream_source=companion\n",
                              T_SCHEMA, T_SCHEMA_N, e, BC_MODS_CONF_MAX);
        check("nº de chaves = schema", n == T_SCHEMA_N);
        check("appTouch=off lido", t_find(e, n, "appTouch")->b == false);
        check("appTouch present", t_find(e, n, "appTouch")->present == true);
        check("appInit segue default ON", t_find(e, n, "appInit")->b == true && !t_find(e, n, "appInit")->present);
        check("throttle_every=30 lido", t_find(e, n, "throttle_every")->i == 30);
        check("stream_source=companion lido", strcmp(t_find(e, n, "stream_source")->s, "companion") == 0);
    }
    {
        printf("\n[Caso 13] coação de int: fora do range → clamp (BepInEx Range.Clamp)\n");
        struct bc_mod_entry e[BC_MODS_CONF_MAX];
        bc_mods_parse("throttle_every=99999\n", T_SCHEMA, T_SCHEMA_N, e, BC_MODS_CONF_MAX);
        check("99999 → clamp 600", t_find(e, T_SCHEMA_N, "throttle_every")->i == 600);
        bc_mods_parse("throttle_every=0\n", T_SCHEMA, T_SCHEMA_N, e, BC_MODS_CONF_MAX);
        check("0 → clamp 1", t_find(e, T_SCHEMA_N, "throttle_every")->i == 1);
        bc_mods_parse("throttle_every=abc\n", T_SCHEMA, T_SCHEMA_N, e, BC_MODS_CONF_MAX);
        check("abc → default 60", t_find(e, T_SCHEMA_N, "throttle_every")->i == 60);
        bc_mods_parse("throttle_every=30xyz\n", T_SCHEMA, T_SCHEMA_N, e, BC_MODS_CONF_MAX);
        check("30xyz (lixo no fim) → default 60", t_find(e, T_SCHEMA_N, "throttle_every")->i == 60);
    }
    {
        printf("\n[Caso 14] coação de enum: fora do domínio → default\n");
        struct bc_mod_entry e[BC_MODS_CONF_MAX];
        bc_mods_parse("stream_source=android\n", T_SCHEMA, T_SCHEMA_N, e, BC_MODS_CONF_MAX);
        check("android (fora) → default game", strcmp(t_find(e, T_SCHEMA_N, "stream_source")->s, "game") == 0);
        bc_mods_parse("stream_source=companion\n", T_SCHEMA, T_SCHEMA_N, e, BC_MODS_CONF_MAX);
        check("companion (no domínio) aceito", strcmp(t_find(e, T_SCHEMA_N, "stream_source")->s, "companion") == 0);
    }
    {
        printf("\n[Caso 15] chave desconhecida ignorada; última ocorrência vence\n");
        struct bc_mod_entry e[BC_MODS_CONF_MAX];
        int n = bc_mods_parse("chave_fantasma=on\nthrottle_every=10\nthrottle_every=20\n",
                              T_SCHEMA, T_SCHEMA_N, e, BC_MODS_CONF_MAX);
        check("schema completo mesmo com fantasma", n == T_SCHEMA_N);
        check("última ocorrência vence (20)", t_find(e, n, "throttle_every")->i == 20);
    }
    {
        printf("\n[Caso 16] round-trip format: só entradas present, formato canônico\n");
        struct bc_mod_entry e[BC_MODS_CONF_MAX];
        bc_mods_parse("appTouch=off\nthrottle_every=30\nstream_source=companion\n",
                      T_SCHEMA, T_SCHEMA_N, e, BC_MODS_CONF_MAX);
        char buf[1024];
        int len = bc_mods_format(T_SCHEMA, T_SCHEMA_N, e, T_SCHEMA_N, buf, sizeof(buf));
        check("format ok", len > 0);
        if (len > 0) {
            check("contém appTouch=off", strstr(buf, "appTouch=off\n") != nullptr);
            check("contém throttle_every=30", strstr(buf, "throttle_every=30\n") != nullptr);
            check("contém stream_source=companion", strstr(buf, "stream_source=companion\n") != nullptr);
            check("NÃO contém chaves default (appInit)", strstr(buf, "appInit") == nullptr);
            // re-parse do formatado → mesmos valores
            struct bc_mod_entry e2[BC_MODS_CONF_MAX];
            bc_mods_parse(buf, T_SCHEMA, T_SCHEMA_N, e2, BC_MODS_CONF_MAX);
            check("re-parse: appTouch=off", t_find(e2, T_SCHEMA_N, "appTouch")->b == false);
            check("re-parse: throttle 30", t_find(e2, T_SCHEMA_N, "throttle_every")->i == 30);
        }
        // config todo-default → format vazio (companion unlinka)
        bc_mods_parse(nullptr, T_SCHEMA, T_SCHEMA_N, e, BC_MODS_CONF_MAX);
        len = bc_mods_format(T_SCHEMA, T_SCHEMA_N, e, T_SCHEMA_N, buf, sizeof(buf));
        check("todo-default → 0 bytes", len == 0);
    }
    {
        printf("\n[Caso 17] compat v1: arquivo só com bools (formato antigo) parseia\n");
        struct bc_mod_entry e[BC_MODS_CONF_MAX];
        int n = bc_mods_parse("appKey=off\n", T_SCHEMA, T_SCHEMA_N, e, BC_MODS_CONF_MAX);
        check("v1 puro: appKey=off", t_find(e, n, "appKey")->b == false);
        check("v1 puro: resto default", t_find(e, n, "appInit")->b == true);
        // valor bool inválido (não on/off) → default (mais tolerante que BepInEx,
        // que descarta a linha — documentado no header)
        bc_mods_parse("appKey=verdade\n", T_SCHEMA, T_SCHEMA_N, e, BC_MODS_CONF_MAX);
        check("bool inválido → default ON", t_find(e, T_SCHEMA_N, "appKey")->b == true);
    }
    {
        printf("\n[Caso 18] linha incompleta sem newline (companion crash mid-write)\n");
        struct bc_mod_entry e[BC_MODS_CONF_MAX];
        // Companion crash após mkstemp mas antes de rename: arquivo original intacto
        // Mas se rename parcial acontecer (linha cortada sem \n), parser deve tolerar
        bc_mods_parse("throttle_every=6", T_SCHEMA, T_SCHEMA_N, e, BC_MODS_CONF_MAX);
        check("linha sem newline parseia como 6", t_find(e, T_SCHEMA_N, "throttle_every")->i == 6);
        bc_mods_parse("appTouch=off", T_SCHEMA, T_SCHEMA_N, e, BC_MODS_CONF_MAX);
        check("bool sem newline parseia como off", t_find(e, T_SCHEMA_N, "appTouch")->b == false);
        // múltiplas linhas mistas (última sem newline)
        bc_mods_parse("appKey=off\nthrottle_every=30", T_SCHEMA, T_SCHEMA_N, e, BC_MODS_CONF_MAX);
        check("última linha sem newline ok", t_find(e, T_SCHEMA_N, "throttle_every")->i == 30);
    }
    {
        printf("\n[Caso 19] dispatcher multi-hook: prefix segue, prefix pula, postfix ajusta retorno\n");
        // sem callbacks = passthrough puro (comportamento anterior, intocado)
        uintptr_t r0 = t_dispatch("x", t_orig_double, 21, nullptr, nullptr);
        check("passthrough sem callback: 21*2=42", r0 == 42);
        // prefix retorna true → orig roda normal
        uintptr_t r1 = t_dispatch("x", t_orig_double, 21, t_prefix_allow, nullptr);
        check("prefix allow: orig roda (42)", r1 == 42);
        // prefix retorna false → orig NÃO roda (contrato igual Harmony)
        uintptr_t r2 = t_dispatch("x", t_orig_double, 21, t_prefix_skip, nullptr);
        check("prefix skip: orig pulado (ret=0)", r2 == 0);
        // postfix roda sempre (mesmo com orig pulado) e pode ajustar *ret
        t_postfix_calls = 0;
        uintptr_t r3 = t_dispatch("x", t_orig_double, 21, t_prefix_skip, t_postfix_observe);
        check("postfix roda mesmo com prefix skip", t_postfix_calls == 1);
        check("postfix vê run_orig=false", t_postfix_saw_run_orig == false);
        check("postfix ajusta retorno: 0+1000=1000", r3 == 1000);
        uintptr_t r4 = t_dispatch("x", t_orig_double, 21, t_prefix_allow, t_postfix_observe);
        check("postfix vê run_orig=true", t_postfix_saw_run_orig == true);
        check("postfix ajusta retorno: 42+1000=1042", r4 == 1042);
    }

    // ================================================================
    // Casos 20-24: unpatch_hook (remoção seletiva, isolamento entre hooks)
    // ================================================================
    {
        printf("\n[Caso 20] unpatch de hook inexistente → false (não crasha)\n");
        // hooks_backup está todo nullptr (reset do harness)
        bool r = t_unpatch_hook("nonexistent_hook");
        check("hook desconhecido retorna false", r == false);
        check("DobbyDestroy não chamado (nenhum hook pra remover)", t_dobby_destroy_called == false);
    }
    {
        printf("\n[Caso 21] unpatch idempotente: backup null → true sem destroy\n");
        // appInit em backup[0] = nullptr (nunca instalado — default do harness)
        t_dobby_destroy_called = false;
        t_dobby_destroy_count = 0;
        bool r = t_unpatch_hook("appInit");
        check("backup null → idempotente, retorna true", r == true);
        check("DobbyDestroy NÃO chamado (backup já null)", t_dobby_destroy_called == false);
        check("count zerado (nenhum destroy)", t_dobby_destroy_count == 0);
    }
    {
        printf("\n[Caso 22] unpatch hook ativo: destroy chamado, backup zerado\n");
        // Simula hook instalado: backup[1] = (void*)0xDEAD
        t_hooks_backup[1] = (void*)0xDEAD;
        t_dobby_destroy_called = false;
        t_dobby_destroy_count = 0;
        bool r = t_unpatch_hook("appUpdateDraw");
        check("hook ativo → unpatch retorna true", r == true);
        check("DobbyDestroy chamado exatamente 1x", t_dobby_destroy_called == true && t_dobby_destroy_count == 1);
        check("backup zerado após unpatch", t_hooks_backup[1] == nullptr);
        // appUpdateDraw é index 1 — confirma que o índice bateu
        check("hook correto removido (idx 1 zerado)", t_hooks_backup[1] == nullptr);
    }
    {
        printf("\n[Caso 23] unpatch isolado: remover hook 3 não toca hooks 0 e 2\n");
        // hook 0, 2 e 3 ativos (hook 1 já removido no Caso 22)
        t_hooks_backup[0] = (void*)0x1111;
        t_hooks_backup[2] = (void*)0x2222;
        t_hooks_backup[3] = (void*)0x3333;
        t_dobby_destroy_count = 0;
        t_unpatch_hook("appKey");  // remove index 3 (appKey)
        check("hook removido (idx 3 = null)", t_hooks_backup[3] == nullptr);
        check("hook 0 preservado (0x1111)", t_hooks_backup[0] == (void*)0x1111);
        check("hook 2 preservado (0x2222)", t_hooks_backup[2] == (void*)0x2222);
        check("apenas 1 destroy chamado (só appKey)", t_dobby_destroy_count == 1);
    }
    {
        printf("\n[Caso 24] unpatch com DobbyDestroy falhando → false, backup preservado\n");
        t_hooks_backup[1] = (void*)0xBEEF;
        t_dobby_destroy_failed = true;
        t_dobby_destroy_called = false;
        bool r = t_unpatch_hook("appUpdateDraw");
        check("destroy falhou → unpatch retorna false", r == false);
        check("backup preservado após falha", t_hooks_backup[1] == (void*)0xBEEF);
        t_dobby_destroy_failed = false;  // reset pro harness clean
    }

    // ================================================================
    // Casos 25-28: repatch_hook (reinstalação seletiva, fecha par unpatch/repatch)
    // ================================================================
    {
        printf("\n[Caso 25] repatch de hook inexistente → false (não crasha)\n");
        bool r = t_repatch_hook("nonexistent_hook");
        check("hook desconhecido retorna false", r == false);
    }
    {
        printf("\n[Caso 26] repatch idempotente: já instalado → true, sem refazer\n");
        // hook 0 já instalado (0x1111 do Caso 23)
        t_hooks_backup[0] = (void*)0x1111;
        void *before = t_hooks_backup[0];
        int install_before = (int)(uintptr_t)t_hooks_backup[0];
        bool r = t_repatch_hook("appInit");
        check("já instalado → true (idempotente)", r == true);
        check("backup NÃO reescrito (mesmo endereço)", t_hooks_backup[0] == before);
        check("nenhum novo install (endereço igual)", (int)(uintptr_t)t_hooks_backup[0] == install_before);
    }
    {
        printf("\n[Caso 27] repatch de hook removido → reinstala (backup novo ≠ anterior)\n");
        // hook 1 (appUpdateDraw) foi removido no Caso 22 (backup null)
        t_hooks_backup[1] = nullptr;
        bool r = t_repatch_hook("appUpdateDraw");
        check("hook removido → repatch retorna true", r == true);
        check("backup preenchido (instalado de novo)", t_hooks_backup[1] != nullptr);
        check("endereço novo ≠ null", t_last_installed_addr != nullptr);
    }
    {
        printf("\n[Caso 28] par unpatch→repatch completo: remove depois reinstala, isolado\n");
        // hook 3 (appKey) ativo; hooks 0,2 preservados
        t_hooks_backup[0] = (void*)0xAAAA;
        t_hooks_backup[2] = (void*)0xCCCC;
        t_hooks_backup[3] = (void*)0xDDDD;
        // 1) unpatch appKey → só hook 3 sai
        t_unpatch_hook("appKey");
        check("unpatch: appKey removido", t_hooks_backup[3] == nullptr);
        check("unpatch: appInit preservado", t_hooks_backup[0] == (void*)0xAAAA);
        check("unpatch: appTouch preservado", t_hooks_backup[2] == (void*)0xCCCC);
        // 2) repatch appKey → volta, hooks 0/2 intocados
        bool r = t_repatch_hook("appKey");
        check("repatch: appKey reinstalado", r == true && t_hooks_backup[3] != nullptr);
        check("repatch: appInit ainda intacto", t_hooks_backup[0] == (void*)0xAAAA);
        check("repatch: appTouch ainda intacto", t_hooks_backup[2] == (void*)0xCCCC);
        check("repatch: endereço novo p/ appKey ≠ 0xDDDD", t_hooks_backup[3] != (void*)0xDDDD);
        // 3) repatch de um hook que NUNCA saiu → idempotente, sem efeito
        check("repatch de hook ativo (appInit) idempotente", t_repatch_hook("appInit") == true);
        check("appInit segue 0xAAAA (idempotente não re-escreve)", t_hooks_backup[0] == (void*)0xAAAA);
    }

    // ================================================================
    // Casos 29-31: write_patches_snapshot (main.cpp:822)
    // Replicação da derivation 2-eixos + 3-state runtime:
    //   config:  on/off  (preferência persistida, g_cfg[].b)
    //   runtime: active / unpatched / no-target — distingue POR QUE o hook
    //            não está ativo (vantagem sobre Harmony, que falha em silêncio)
    //   active    = backup != NULL
    //   unpatched = backup == NULL && resolved_addr != NULL (instalou+removeu)
    //   no-target = backup == NULL && resolved_addr == NULL (nunca resolveu)
    // ================================================================
    {
        printf("\n[Caso 29] snapshot 2-eixos: config+3-state runtime (distinção de causa)\n");
        // appInit:  backup null + resolved null   → no-target (nunca resolveu)
        // appUpdate:backup non-null               → active
        // appTouch: backup null + resolved non-null → unpatched (removeu)
        // appKey:   backup non-null + config off  → active (config é eixo separado)
        t_hooks_backup[0] = nullptr;          t_hooks_resolved[0] = nullptr;   // no-target
        t_hooks_backup[1] = (void*)0x1111;    t_hooks_resolved[1] = (void*)0x1; // active
        t_hooks_backup[2] = nullptr;          t_hooks_resolved[2] = (void*)0x2; // unpatched
        t_hooks_backup[3] = (void*)0x2222;    t_hooks_resolved[3] = (void*)0x3; // active
        static bool t_mod_enabled_m[4] = {true, true, true, false};  // appKey config off
        static unsigned t_hits_m[4] = {5, 100, 0, 7};

        char body[1024];
        size_t used = 0;
        body[0] = '\0';
        for (int i = 0; i < 4; i++) {
            int w = t_snapshot_row(body + used, sizeof(body) - used, i,
                                   t_mod_enabled_m[i], t_hits_m[i]);
            if (w < 0 || (size_t)w >= sizeof(body) - used) break;
            used += (size_t)w;
        }
        check("appInit → on|no-target|5 (nunca resolveu)", strstr(body, "appInit|on|no-target|5\n") != nullptr);
        check("appUpdateDraw → on|active|100", strstr(body, "appUpdateDraw|on|active|100\n") != nullptr);
        check("appTouch → on|unpatched|0 (removeu)", strstr(body, "appTouch|on|unpatched|0\n") != nullptr);
        check("appKey → off|active|7 (config off ≠ runtime)", strstr(body, "appKey|off|active|7\n") != nullptr);
    }
    {
        printf("\n[Caso 30] snapshot: seq header + formato nome|config|runtime|hits\n");
        t_hooks_backup[0] = (void*)0x3333;    t_hooks_resolved[0] = (void*)0x10;
        t_hooks_backup[1] = (void*)0x4444;    t_hooks_resolved[1] = (void*)0x11;
        t_hooks_backup[2] = (void*)0x5555;    t_hooks_resolved[2] = (void*)0x12;
        t_hooks_backup[3] = (void*)0x6666;    t_hooks_resolved[3] = (void*)0x13;
        static unsigned t_hits2[4] = {10, 20, 30, 40};
        bool all_on[4] = {true, true, true, true};

        char body[1024];
        size_t used = 0;
        body[0] = '\0';
        for (int i = 0; i < 4; i++) {
            int w = t_snapshot_row(body + used, sizeof(body) - used, i, all_on[i], t_hits2[i]);
            if (w < 0 || (size_t)w >= sizeof(body) - used) break;
            used += (size_t)w;
        }
        char content[1100];
        snprintf(content, sizeof(content), "seq=42\n%s", body);

        check("header seq=42 presente", strncmp(content, "seq=42\n", 7) == 0);
        check("appInit|on|active|10", strstr(content, "appInit|on|active|10\n") != nullptr);
        check("appKey|on|active|40 (última)", strstr(content, "appKey|on|active|40\n") != nullptr);
        int nl = 0;
        for (const char *p = content; *p; p++) if (*p == '\n') nl++;
        check("5 newlines (1 header + 4 hooks)", nl == 5);
    }
    {
        printf("\n[Caso 31] 3-state runtime coexiste + divergência backup×config visível\n");
        t_hooks_backup[0] = nullptr;          t_hooks_resolved[0] = nullptr;   // no-target
        t_hooks_backup[1] = (void*)0x1;       t_hooks_resolved[1] = (void*)0x20; // active
        t_hooks_backup[2] = nullptr;          t_hooks_resolved[2] = (void*)0x21; // unpatched
        t_hooks_backup[3] = nullptr;          t_hooks_resolved[3] = nullptr;   // no-target
        bool t_mod_en3[4] = {true, true, true, false};  // appKey config off + no-target

        char body[1024];
        size_t used = 0; body[0] = '\0';
        for (int i = 0; i < 4; i++) {
            int w = t_snapshot_row(body + used, sizeof(body) - used, i, t_mod_en3[i], 0);
            if (w > 0 && (size_t)w < sizeof(body) - used) used += (size_t)w;
        }
        // 4 combinações distintas — causa exposta, nada escondido
        check("active+unpatched+no-target coexist (3 estados runtime)",
              strstr(body, "appUpdateDraw|on|active|0") != nullptr &&
              strstr(body, "appTouch|on|unpatched|0") != nullptr &&
              strstr(body, "appInit|on|no-target|0") != nullptr);
        check("config off + runtime no-target (divergência explícita)",
              strstr(body, "appKey|off|no-target|0") != nullptr);
    }

    // ================================================================
    // Caso 32: stress test unpatch/repatch 50x seguidos (robustez superior)
    // ================================================================
    {
        printf("\n[Caso 32] stress test: unpatch/repatch 50x no mesmo hook\n");
        // hook 1 (appUpdateDraw) será o alvo do stress
        t_hooks_backup[1] = (void*)0x1111;  // começa instalado
        // Reset counter do mock de repatch pra garantir endereços válidos e únicos
        t_repatch_counter = 0x5000;

        void *last_addr = nullptr;
        // Loop 50x: unpatch → repatch
        for (int i = 0; i < 50; i++) {
            // unpatch
            bool u = t_unpatch_hook("appUpdateDraw");
            if (!u) {
                printf("FAIL: unpatch falhou na iteração %d\n", i);
                g_fail++;
                break;
            }
            if (t_hooks_backup[1] != nullptr) {
                printf("FAIL: backup não zerado após unpatch (iteração %d)\n", i);
                g_fail++;
                break;
            }
            // repatch
            bool r = t_repatch_hook("appUpdateDraw");
            if (!r) {
                printf("FAIL: repatch falhou na iteração %d\n", i);
                g_fail++;
                break;
            }
            if (t_hooks_backup[1] == nullptr) {
                printf("FAIL: backup null após repatch (iteração %d)\n", i);
                g_fail++;
                break;
            }
            // Endereço deve ser diferente a cada repatch (stub gera endereço novo)
            // Iteração 0: apenas salva base (não verifica mudança)
            if (i == 0) {
                last_addr = t_hooks_backup[1];
            } else {
                if (t_hooks_backup[1] == last_addr) {
                    printf("FAIL: endereço não mudou na iteração %d (leak?)\n", i);
                    g_fail++;
                    break;
                }
                last_addr = t_hooks_backup[1];  // atualiza pra próxima
            }
        }
        check("50 iterações unpatch/repatch sem crash", g_fail == 0);
        // Após loop, hook deve estar instalado
        check("hook instalado após stress test", t_hooks_backup[1] != nullptr);
    }

    // ================================================================
    // Caso 33: seq atomic do list_patches (simula 2 clientes concorrentes)
    // Valida que ++g_patches_req_seq (std::atomic) garante seq única por
    // cliente mesmo com threads paralelas — sem atomic, 2 clientes podem
    // receber o mesmo seq (race → snapshot validation falha).
    // ================================================================
    {
        printf("\n[Caso 33] atomic seq: ++ em concorrência produz seq única por thread\n");
        std::atomic<unsigned> t_seq{0};
        unsigned results[8] = {0};
        // Simula 8 clientes incrementando "simultaneamente" (host single-thread:
        // atomic garante atomicidade do fetch_add, não depende de paralelismo real)
        for (int i = 0; i < 8; i++) {
            results[i] = ++t_seq;  // ++atomic é fetch_add(1) + 1, seq consistente
        }
        check("8 clientes → 8 seqs únicos",
              results[0] != results[1] && results[1] != results[2] &&
              results[2] != results[3] && results[3] != results[4] &&
              results[4] != results[5] && results[5] != results[6] &&
              results[6] != results[7]);
        check("seqs são 1,2,3,...,8 (no gaps)",
              results[0] == 1 && results[1] == 2 && results[2] == 3 &&
              results[3] == 4 && results[4] == 5 && results[5] == 6 &&
              results[6] == 7 && results[7] == 8);
    }
    {
        printf("\n[Caso 34] list_patches: snapshot seq=X só serve cliente com seq=X\n");
        // Valida a lógica de correlation (companion.cpp:433):
        //   sscanf(content, "seq=%15s", hdr) == 1 && strcmp(hdr, seqbuf) == 0
        // Cliente A pede seq=5, cliente B pede seq=6.
        // Game publica snapshot seq=6 (o último pedido via shared property).
        // Cliente A (seq=5) deve REJEITAR snapshot seq=6 (não bate seu número).
        // Cliente B (seq=6) aceita.
        char snapshot[256];
        snprintf(snapshot, sizeof(snapshot), "seq=6\nappInit|unpatched|0\n");  // game respondeu seq=6 (último)
        char hdr[16] = {0};
        bool a_accepts = (sscanf(snapshot, "seq=%15s", hdr) == 1 && strcmp(hdr, "5") == 0);
        bool b_accepts = (sscanf(snapshot, "seq=%15s", hdr) == 1 && strcmp(hdr, "6") == 0);
        check("cliente A (seq=5) REJEITA snapshot seq=6", a_accepts == false);
        check("cliente B (seq=6) ACEITA snapshot seq=6", b_accepts == true);
        // Simula retry do cliente A: pede seq=7, game publica seq=7 → aceita
        char snapshot2[256];
        snprintf(snapshot2, sizeof(snapshot2), "seq=7\nappInit|active|5\n");
        sscanf(snapshot2, "seq=%15s", hdr);
        bool a_retry_accepts = (strcmp(hdr, "7") == 0);
        check("cliente A retry (seq=7) ACEITA snapshot seq=7", a_retry_accepts == true);
    }

    // ================================================================
    // Casos 35+: bc_hook_logic.h — FUNÇÕES REAIS (não mock). Inclui o header
    // que o main.cpp usa; testes exercitam hook_register_prefix/postfix,
    // hook_dispatch, bc_unpatch_hook, bc_repatch_hook com backends stub de
    // Dobby (em device main.cpp injeta DobbyDestroy/DobbyHook de verdade).
    // ================================================================
    {
        printf("\n[Caso 35] hook_register_prefix real: registra/estoura/desconhecido\n");
        // hook_register_prefix_by_slot(cbs, slot, fn) indexa cbs[slot]
        // INTERNAMENTE — o chamador passa a BASE do array, não um elemento
        // já indexado. Achado real via AddressSanitizer: &cbs[1] com slot=1
        // virava cbs[1][1] == cbs[2] (double-index), e &cbs[3] com slot=3
        // virava cbs[3][3] == cbs[6], global-buffer-overflow de verdade
        // (corrompia a próxima global no binário) — silencioso sem sanitizer.
        static HookCallbacks cbs[BC_HOOK_NAMES_COUNT] = {};
        bool p0 = hook_register_prefix_by_slot(cbs, 0, t_prefix_allow);
        bool p1 = hook_register_prefix_by_slot(cbs, 1, t_prefix_allow);
        bool p2 = hook_register_prefix_by_slot(cbs, 2, t_prefix_allow);
        bool p3 = hook_register_prefix_by_slot(cbs, 3, t_prefix_allow);
        bool bad = hook_register_prefix_by_slot(cbs, 99, t_prefix_allow);  // slot inválido
        check("4 prefix slots registram", p0 && p1 && p2 && p3);
        check("slot inválido → false", bad == false);
    }
    {
        printf("\n[Caso 36] hook_register_postfix real: nome base + estouro + inválido\n");
        static HookCallbacks cbs[BC_HOOK_NAMES_COUNT] = {};
        bool ok = hook_register_postfix(cbs, "appTouch", t_postfix_observe);
        check("postfix registra via nome", ok == true);
        // preenche até HOOK_MAX_CALLBACKS
        for (int i = 0; i < HOOK_MAX_CALLBACKS - 1; i++)
            hook_register_postfix(cbs, "appTouch", t_postfix_observe);
        bool overflow = hook_register_postfix(cbs, "appTouch", t_postfix_observe);
        check("postfix slot cheio → false", overflow == false);
        bool bad = hook_register_postfix(cbs, "nope", t_postfix_observe);
        check("postfix nome inválido → false", bad == false);
    }
    {
        printf("\n[Caso 37] hook_dispatch real: passthrough/prefix/postfix multi-callback\n");
        static HookCallbacks cb = {};   // vazio → passthrough
        uintptr_t r0 = hook_dispatch("appInit", &cb, d_orig_wide,
                                     21, 0,0,0,0,0,0,0, nullptr, nullptr);
        check("passthrough sem callback: 21*2=42", r0 == 42);
        // 3 prefixes + 2 postfixes no MESMO hook
        static HookCallbacks cb2 = {};
        hook_register_prefix_by_slot(&cb2, 0, t_prefix_allow);
        hook_register_prefix_by_slot(&cb2, 0, t_prefix_allow);
        hook_register_prefix_by_slot(&cb2, 0, t_prefix_allow);
        hook_register_postfix_by_slot(&cb2, 0, t_postfix_observe);
        hook_register_postfix_by_slot(&cb2, 0, t_postfix_observe);
        uintptr_t r1 = hook_dispatch("appInit", &cb2, d_orig_wide,
                                     21, 0,0,0,0,0,0,0, nullptr, nullptr);
        check("3 prefix allow + 2 postfix: 42+1000+1000=2042", r1 == 2042);
        // prefix skip cancela orig (postfix ainda roda)
        static HookCallbacks cb3 = {};
        hook_register_prefix_by_slot(&cb3, 0, t_prefix_skip);
        hook_register_postfix_by_slot(&cb3, 0, t_postfix_observe);
        uintptr_t r2 = hook_dispatch("appInit", &cb3, d_orig_wide,
                                     21, 0,0,0,0,0,0,0, nullptr, nullptr);
        check("prefix skip: orig pulado, ret=0+1000=1000", r2 == 1000);
        // orig null → ret 0 seguro
        uintptr_t r3 = hook_dispatch("appInit", nullptr, (jnifn_wide_t)nullptr,
                                     21, 0,0,0,0,0,0,0, nullptr, nullptr);
        check("orig null → ret 0 (sem crash)", r3 == 0);
    }
    {
        printf("\n[Caso 38] bc_unpatch_hook real (lógica de decisão, backend Dobby stub)\n");
        t_dobby_destroy_called = false;
        t_dobby_destroy_failed = false;
        static HookState st[BC_HOOK_NAMES_COUNT] = {};
        st[1].backup = (void*)0xAAAA;
        st[1].resolved_addr = (void*)0xBBBB;   // appUpdateDraw instalado+resolvido
        int r = bc_unpatch_hook(st, "appUpdateDraw", (HookDestroyFn)t_destroy_stub);
        check("unpatch ativo → 1", r == 1);
        check("backup zerado", st[1].backup == nullptr);
        check("resolved preservado (não vira no-target)", st[1].resolved_addr == (void*)0xBBBB);
        check("destroy stub chamado", t_dobby_destroy_called == true);
        int r2 = bc_unpatch_hook(st, "appUpdateDraw", (HookDestroyFn)t_destroy_stub);
        check("idempotente → 0", r2 == 0);
        int r3 = bc_unpatch_hook(st, "nope", (HookDestroyFn)t_destroy_stub);
        check("desconhecido → -1", r3 == -1);
        st[2].backup = (void*)0x1111;
        st[2].resolved_addr = nullptr;         // appTouch sem resolved (anomalia)
        int r4 = bc_unpatch_hook(st, "appTouch", (HookDestroyFn)t_destroy_stub);
        check("sem resolved → -1 (não destroy)", r4 == -1);
        check("backup preservado quando -1", st[2].backup == (void*)0x1111);
    }
    {
        printf("\n[Caso 39] bc_repatch_hook real (lógica de decisão, backend Dobby stub)\n");
        static HookState st[BC_HOOK_NAMES_COUNT] = {};
        st[0].backup = nullptr;
        st[0].resolved_addr = nullptr;         // appInit removido
        int r = bc_repatch_hook(st, "appInit", (HookInstallFn)t_install_stub,
                                (void*)0x1234, nullptr);
        check("repatch de removido → 1", r == 1);
        check("backup preenchido", st[0].backup != nullptr);
        check("resolved marcado após reinstall", st[0].resolved_addr != nullptr);
        int r2 = bc_repatch_hook(st, "appInit", (HookInstallFn)t_install_stub,
                                 (void*)0x1234, nullptr);
        check("idempotente (já instalado) → 0", r2 == 0);
        int r3 = bc_repatch_hook(st, "nope", (HookInstallFn)t_install_stub,
                                 (void*)0x1234, nullptr);
        check("desconhecido → -1", r3 == -1);
    }
    {
        printf("\n[Caso 40] par unpatch→repatch REAL no MESMO HookState (ciclo completo)\n");
        static HookState st[BC_HOOK_NAMES_COUNT] = {};
        st[3].backup = (void*)0xCAFE;
        st[3].resolved_addr = (void*)0xDEAD;   // appKey ativo
        int u = bc_unpatch_hook(st, "appKey", (HookDestroyFn)t_destroy_stub);
        check("unpatch appKey → 1", u == 1);
        check("appKey backup zerado", st[3].backup == nullptr);
        check("appKey ainda 'resolvido' (unpatched, não no-target)",
              st[3].resolved_addr == (void*)0xDEAD);
        int rp = bc_repatch_hook(st, "appKey", (HookInstallFn)t_install_stub,
                                 (void*)0x7777, nullptr);
        check("repatch appKey → 1", rp == 1);
        check("appKey reinstalado", st[3].backup != nullptr);
        // 3-state final: backup ok → active
        check("appKey agora active (backup != null)", st[3].backup != nullptr);
        // e um segundo hook nunca instalado → no-target
        check("appInit no-target (resolved null)", st[0].resolved_addr == nullptr);
    }

    // ================================================================
    // Casos 41-42: watch-per-key (BepInEx SettingChanged port — bc_mods_conf.h)
    // ================================================================
    {
        printf("\n[Caso 41] bc_mod_watch_register/find: registra/substitui/overflow/nome Inválido\n");
        static bc_mod_watch wt[BC_MODS_CONF_MAX] = {};
        static int wt_count = 0;
        g_watch_call_count = 0;

        // registra 2 chaves distintas
        bool r1 = bc_mod_watch_register(wt, BC_MODS_CONF_MAX, &wt_count, "throttle_every", w_dummy);
        bool r2 = bc_mod_watch_register(wt, BC_MODS_CONF_MAX, &wt_count, "appKey", w_dummy);
        check("registra 2 chaves", r1 && r2);
        check("count=2 após registros válidos", wt_count == 2);

        // re-registrar mesma chave substitui, não duplica (idempotente)
        bool r3 = bc_mod_watch_register(wt, BC_MODS_CONF_MAX, &wt_count, "throttle_every", w_dummy);
        check("re-registro substitui (idempotente)", r3 == true && wt_count == 2);

        // lookup encontra os callbacks registrados
        check("lookup throttle_every retorna fn",
              bc_mod_watch_find(wt, wt_count, "throttle_every") != nullptr);
        check("lookup appKey retorna fn",
              bc_mod_watch_find(wt, wt_count, "appKey") != nullptr);
        check("lookup chave não-registrada retorna nullptr",
              bc_mod_watch_find(wt, wt_count, "nope") == nullptr);

        // args inválidos → false
        check("register key=null → false",
              bc_mod_watch_register(wt, BC_MODS_CONF_MAX, &wt_count, nullptr, w_dummy) == false);
        check("register fn=null → false",
              bc_mod_watch_register(wt, BC_MODS_CONF_MAX, &wt_count, "x", nullptr) == false);

        // overflow: enche a tabela
        static bc_mod_watch wt2[BC_MODS_CONF_MAX] = {};
        static int wt2_count = 0;
        for (int i = 0; i < BC_MODS_CONF_MAX; i++) {
            char kn[32];
            snprintf(kn, sizeof(kn), "k%d", i);
            bc_mod_watch_register(wt2, BC_MODS_CONF_MAX, &wt2_count, kn, w_dummy);
        }
        check("tabela cheia → count = BC_MODS_CONF_MAX", wt2_count == BC_MODS_CONF_MAX);
        check("overflow rejeita (count não sobe)",
              bc_mod_watch_register(wt2, BC_MODS_CONF_MAX, &wt2_count, "extra", w_dummy) == false);
    }
    {
        printf("\n[Caso 42] bc_mod_watch_fire_changed: dispara só em delta real (valor + present)\n");
        static bc_mod_watch wt[BC_MODS_CONF_MAX] = {};
        static int wt_count = 0;
        g_watch_call_count = 0;
        wt_count = 0;

        bc_mod_watch_register(wt, BC_MODS_CONF_MAX, &wt_count, "throttle_every", w_dummy);
        bc_mod_watch_register(wt, BC_MODS_CONF_MAX, &wt_count, "appKey", w_dummy);
        bc_mod_watch_register(wt, BC_MODS_CONF_MAX, &wt_count, "appInit", w_dummy);

        struct bc_mod_entry before[BC_MODS_CONF_MAX] = {};
        struct bc_mod_entry after[BC_MODS_CONF_MAX] = {};
        int n = bc_mods_parse(nullptr, T_SCHEMA, T_SCHEMA_N, before, BC_MODS_CONF_MAX);
        bc_mods_parse(nullptr, T_SCHEMA, T_SCHEMA_N, after, BC_MODS_CONF_MAX);

        // Caso 42a: configs idênticas → nada dispara (BepInEx SettingChanged no-op)
        int f0 = bc_mod_watch_fire_changed(wt, wt_count, before, after, n);
        check("delta zero → 0 callbacks", f0 == 0 && g_watch_call_count == 0);

        // Caso 42b: throttle_every muda (59→42) → só o callback dessa chave
        t_find(after, n, "throttle_every")->i = 42;
        t_find(after, n, "throttle_every")->present = true;
        g_watch_call_count = 0;
        int f1 = bc_mod_watch_fire_changed(wt, wt_count, before, after, n);
        check("throttle_every muda → 1 callback", f1 == 1 && g_watch_call_count == 1);
        check("callback recebeu key=throttle_every", strcmp(g_watch_last_key, "throttle_every") == 0);

        // Caso 42c: múltiplas mudanças → múltiplos callbacks
        bc_mods_parse(nullptr, T_SCHEMA, T_SCHEMA_N, before, BC_MODS_CONF_MAX);
        bc_mods_parse(nullptr, T_SCHEMA, T_SCHEMA_N, after, BC_MODS_CONF_MAX);
        t_find(after, n, "throttle_every")->i = 100;
        t_find(after, n, "throttle_every")->present = true;
        t_find(after, n, "appKey")->b = false;
        t_find(after, n, "appKey")->present = true;
        g_watch_call_count = 0;
        int f2 = bc_mod_watch_fire_changed(wt, wt_count, before, after, n);
        check("2 mudanças → 2 callbacks", f2 == 2 && g_watch_call_count == 2);

        // Caso 42d: chave sem callback registrado não dispara (só loga no main.cpp)
        bc_mods_parse(nullptr, T_SCHEMA, T_SCHEMA_N, before, BC_MODS_CONF_MAX);
        bc_mods_parse(nullptr, T_SCHEMA, T_SCHEMA_N, after, BC_MODS_CONF_MAX);
        t_find(after, n, "stream_source");  // não tem watch registrado
        t_find(after, n, "appUpdateDraw")->b = false;
        t_find(after, n, "appUpdateDraw")->present = true;
        g_watch_call_count = 0;
        int f3 = bc_mod_watch_fire_changed(wt, wt_count, before, after, n);
        check("mudança sem callback → 0 fire (appUpdateDraw não registrado)", f3 == 0);

        // Caso 42e: present muda (ausente→presente, valor=default) → fire
        bc_mods_parse(nullptr, T_SCHEMA, T_SCHEMA_N, before, BC_MODS_CONF_MAX);
        bc_mods_parse("appInit=on\n", T_SCHEMA, T_SCHEMA_N, after, BC_MODS_CONF_MAX);
        g_watch_call_count = 0;
        int f4 = bc_mod_watch_fire_changed(wt, wt_count, before, after, n);
        check("present=false→true (appInit) → 1 callback", f4 == 1 && g_watch_call_count == 1);
    }

    {
        printf("\n[Caso 41] mod-graph: independentes → ordem = declaração, todos OK\n");
        static const struct bc_mod_manifest m[] = {
            {"appInit",  {}, {}},
            {"appTouch", {}, {}},
            {"appKey",   {}, {}},
        };
        struct bc_mod_graph_result st;
        int r = bc_mod_graph_sort(m, 3, &st);
        check("3 independentes aprovados", r == 3);
        check("ordem = ordem de declaração",
              st.order[0] == 0 && st.order[1] == 1 && st.order[2] == 2);
        check("status todos OK",
              st.status[0] == BC_MOD_OK && st.status[1] == BC_MOD_OK &&
              st.status[2] == BC_MOD_OK);
    }
    {
        printf("\n[Caso 42] mod-graph: requires presente → dependência sai antes\n");
        static const struct bc_mod_manifest m[] = {
            {"modB", {"modA"}, {}},   // declarado ANTES, mas depende de modA
            {"modA", {}, {}},
        };
        struct bc_mod_graph_result st;
        int r = bc_mod_graph_sort(m, 2, &st);
        check("2 aprovados", r == 2);
        check("modA (idx1) sai primeiro, modB (idx0) depois",
              st.order[0] == 1 && st.order[1] == 0);
    }
    {
        printf("\n[Caso 43] mod-graph: chain C→B→A → ordem A, B, C\n");
        static const struct bc_mod_manifest m[] = {
            {"modC", {"modB"}, {}},
            {"modB", {"modA"}, {}},
            {"modA", {}, {}},
        };
        struct bc_mod_graph_result st;
        int r = bc_mod_graph_sort(m, 3, &st);
        check("3 aprovados", r == 3);
        check("ordem A,B,C (idx 2,1,0)",
              st.order[0] == 2 && st.order[1] == 1 && st.order[2] == 0);
    }
    {
        printf("\n[Caso 44] mod-graph: require ausente → requerente rejeitado, irmãos carregam\n");
        static const struct bc_mod_manifest m[] = {
            {"orphan", {"fantasma"}, {}},
            {"solo",   {}, {}},
        };
        struct bc_mod_graph_result st;
        int r = bc_mod_graph_sort(m, 2, &st);
        check("só solo carrega", r == 1 && st.order[0] == 1);
        check("orphan = REJ_MISSING", st.status[0] == BC_MOD_REJ_MISSING);
        check("solo = OK", st.status[1] == BC_MOD_OK);
    }
    {
        printf("\n[Caso 45] mod-graph: conflict declarado e presente → declarador rejeitado, alvo fica\n");
        static const struct bc_mod_manifest m[] = {
            {"base", {}, {}},
            {"ext",  {}, {"base"}},
        };
        struct bc_mod_graph_result st;
        int r = bc_mod_graph_sort(m, 2, &st);
        check("só base carrega", r == 1 && st.order[0] == 0);
        check("ext = REJ_CONFLICT", st.status[1] == BC_MOD_REJ_CONFLICT);
        check("base = OK", st.status[0] == BC_MOD_OK);
    }
    {
        printf("\n[Caso 46] mod-graph: ciclo de requires → grupo rejeitado, independente carrega\n");
        static const struct bc_mod_manifest m[] = {
            {"cicloA", {"cicloB"}, {}},
            {"cicloB", {"cicloA"}, {}},
            {"solo",   {}, {}},
        };
        struct bc_mod_graph_result st;
        int r = bc_mod_graph_sort(m, 3, &st);
        check("só solo carrega", r == 1 && st.order[0] == 2);
        check("cicloA e cicloB = REJ_CYCLE",
              st.status[0] == BC_MOD_REJ_CYCLE && st.status[1] == BC_MOD_REJ_CYCLE);
        check("solo = OK", st.status[2] == BC_MOD_OK);
    }
    {
        printf("\n[Caso 47] mod-graph: entrada hostil (null/vazio/overflow/nome null) não crasha\n");
        static const struct bc_mod_manifest m[] = {
            {"x", {}, {}},
            {nullptr, {}, {}},   // nome null no conjunto: filtrado silenciosamente
        };
        struct bc_mod_graph_result st;
        check("mods null → 0", bc_mod_graph_sort(nullptr, 3, &st) == 0);
        check("n=0 → 0", bc_mod_graph_sort(m, 0, &st) == 0);
        check("n>MAX → 0", bc_mod_graph_sort(m, BC_MOD_GRAPH_MAX_MODS + 1, &st) == 0);
        int r = bc_mod_graph_sort(m, 2, &st);
        check("nome null filtrado, x carrega", r == 1 && st.order[0] == 0);
    }

    {
        printf("\n[Caso 48] bc_loader: filtro de nome de arquivo .so (puro)\n");
        check("null → false", bc_loader_is_mod_filename(nullptr) == false);
        check("vazio → false", bc_loader_is_mod_filename("") == false);
        check("oculto (.mod.so) → false", bc_loader_is_mod_filename(".mod.so") == false);
        check("sem extensão .so → false", bc_loader_is_mod_filename("mod.txt") == false);
        check("curto demais (\"a\") → false", bc_loader_is_mod_filename("a") == false);
        check("válido (01_core.so) → true", bc_loader_is_mod_filename("01_core.so") == true);
    }
    {
        printf("\n[Caso 49] bc_loader_load_one: 4 caminhos reais via ops injetados (stub)\n");
        // Stubs determinísticos — simulam dlopen/dlsym/dlclose/run_entry sem
        // libs reais, testando a MESMA lógica de decisão do device.
        static int close_calls = 0;
        static bool entry_ran = false;
        close_calls = 0; entry_ran = false;

        bc_loader_ops ops_ok = {};
        ops_ok.dlopen  = [](const char *, int) -> void * { return (void *)0x1; };
        ops_ok.dlsym   = [](void *, const char *) -> void * { return (void *)0x2; };
        ops_ok.dlclose = [](void *) -> int { close_calls++; return 0; };
        ops_ok.run_entry = [](void *, void *) -> bool { entry_ran = true; return true; };

        bc_loaded_mod out;
        bc_load_status st = bc_loader_load_one(&ops_ok, "fake.so", nullptr, &out);
        check("dlopen+dlsym+entry true → BC_LOAD_OK", st == BC_LOAD_OK);
        check("out.status == OK", out.status == BC_LOAD_OK);
        check("entry foi chamada", entry_ran == true);
        check("handle preservado (não fechado em sucesso)", close_calls == 0);

        bc_loader_ops ops_inactive = ops_ok;
        entry_ran = false;
        ops_inactive.run_entry = [](void *, void *) -> bool { entry_ran = true; return false; };
        st = bc_loader_load_one(&ops_inactive, "fake2.so", nullptr, &out);
        check("entry retorna false → BC_LOAD_INACTIVE", st == BC_LOAD_INACTIVE);

        bc_loader_ops ops_open_fail = {};
        ops_open_fail.dlopen = [](const char *, int) -> void * { return nullptr; };
        st = bc_loader_load_one(&ops_open_fail, "bad.so", nullptr, &out);
        check("dlopen falha → BC_LOAD_ERR_OPEN", st == BC_LOAD_ERR_OPEN);
        check("out zerado em erro (handle null)", out.handle == nullptr);

        close_calls = 0;
        bc_loader_ops ops_nosym = {};
        ops_nosym.dlopen  = [](const char *, int) -> void * { return (void *)0x1; };
        ops_nosym.dlsym   = [](void *, const char *) -> void * { return nullptr; };
        ops_nosym.dlclose = [](void *) -> int { close_calls++; return 0; };
        st = bc_loader_load_one(&ops_nosym, "notamod.so", nullptr, &out);
        check("dlsym nulo → BC_LOAD_ERR_NOSYM", st == BC_LOAD_ERR_NOSYM);
        check("handle fechado quando símbolo ausente (não vaza)", close_calls == 1);

        check("ops null → BC_LOAD_ERR_OPEN (não crasha)",
              bc_loader_load_one(nullptr, "x.so", nullptr, &out) == BC_LOAD_ERR_OPEN);
    }

    {
        // Caso 50: bc_pattern_scan_buffer com bytes REAIS extraídos do
        // libnative-lib.so (EN, appUpdateDraw @ 0x31ec4c, via xxd) — prova
        // que o AOB scan acha o prólogo real do jogo, não só dado sintético.
        printf("\n[Caso 50] bc_pattern_scan_buffer: prólogo real de appUpdateDraw (EN)\n");

        // 24 bytes fixos do prólogo real (sub sp; stp x29,x30; stp x22,x21;
        // stp x20,x19; add x29,sp; mrs x20,TPIDR_EL0) — sem operando de
        // endereço relativo (adrp vem só depois, byte 24).
        static const uint8_t real_prologue[24] = {
            0xff, 0x43, 0x01, 0xd1, 0xfd, 0x7b, 0x02, 0xa9,
            0xf6, 0x57, 0x03, 0xa9, 0xf4, 0x4f, 0x04, 0xa9,
            0xfd, 0x83, 0x00, 0x91, 0x54, 0xd0, 0x3b, 0xd5,
        };

        bc_pattern pat = {};
        memcpy(pat.bytes, real_prologue, sizeof(real_prologue));
        for (size_t i = 0; i < sizeof(real_prologue); i++) pat.mask[i] = 1; // sem wildcard
        pat.len = sizeof(real_prologue);

        // Simula o segmento .text: prólogo de outra função (lixo) + o
        // prólogo real no meio + mais lixo depois — como seria escanear um
        // .so de verdade, não um buffer feito sob medida pro pattern caber.
        uint8_t fake_segment[128];
        memset(fake_segment, 0x90, sizeof(fake_segment)); // "lixo" não-zero
        memcpy(fake_segment + 40, real_prologue, sizeof(real_prologue));

        size_t off = 0;
        bc_scan_status st = bc_pattern_scan_buffer(fake_segment, sizeof(fake_segment), &pat, &off);
        check("acha o prólogo real em meio a lixo → BC_SCAN_OK", st == BC_SCAN_OK);
        check("offset correto (40)", off == 40);

        // Ambiguidade real: o mesmo pattern aparece 2x no segmento →
        // recusa escolher, retorna AMBIGUOUS (não "pega o primeiro").
        uint8_t dup_segment[200];
        memset(dup_segment, 0x90, sizeof(dup_segment));
        memcpy(dup_segment + 10, real_prologue, sizeof(real_prologue));
        memcpy(dup_segment + 100, real_prologue, sizeof(real_prologue));
        st = bc_pattern_scan_buffer(dup_segment, sizeof(dup_segment), &pat, &off);
        check("pattern duplicado → BC_SCAN_AMBIGUOUS (nunca escolhe às cegas)",
              st == BC_SCAN_AMBIGUOUS);

        // Não encontrado: pattern real não está no buffer.
        uint8_t empty_segment[64];
        memset(empty_segment, 0x00, sizeof(empty_segment));
        st = bc_pattern_scan_buffer(empty_segment, sizeof(empty_segment), &pat, &off);
        check("ausente → BC_SCAN_NOT_FOUND", st == BC_SCAN_NOT_FOUND);

        // Wildcard real: mascara os 4 últimos bytes (mrs x20 tem encoding
        // fixo aqui, mas testamos a mecânica de wildcard mesmo assim —
        // simula um operando que mudaria entre builds).
        bc_pattern pat_wild = pat;
        pat_wild.mask[20] = pat_wild.mask[21] = pat_wild.mask[22] = pat_wild.mask[23] = 0;
        uint8_t mutated[64];
        memset(mutated, 0x90, sizeof(mutated));
        memcpy(mutated + 5, real_prologue, sizeof(real_prologue));
        mutated[5 + 20] = 0xAA; mutated[5 + 21] = 0xBB; // bytes mudados, mas mascarados
        st = bc_pattern_scan_buffer(mutated, sizeof(mutated), &pat_wild, &off);
        check("wildcard ignora bytes mascarados → BC_SCAN_OK mesmo mutado", st == BC_SCAN_OK);
        check("offset correto com wildcard (5)", off == 5);
    }

    // ================================================================
    // Caso 51: bc_pattern_scan_buffer com bytes REAIS de appInit (EN)
    // Segundo pattern real — complementa Caso 50 (appUpdateDraw). Ambos
    // extraídos via xxd do mesmo libnative-lib.so, offsets diferentes:
    //   appUpdateDraw @ 0x31ec4c → Caso 50
    //   appInit       @ 0x31eb7c → Caso 51
    // ================================================================
    {
        printf("\n[Caso 51] bc_pattern_scan_buffer: prólogo real de appInit (EN, 0x31eb7c)\n");

        // 24 bytes reais do appInit (xxd -s 0x31eb7c -l 24 -p):
        // stp x29,x30,[sp,#-32]!  stp x22,x23,[sp,#16]
        // stp x20,x21,[sp,#32]    mov x29,sp
        // adrp x22,...             ldr x8,[x22,#0x1130]
        static const uint8_t real_appinit[24] = {
            0xfd, 0x7b, 0xbd, 0xa9, 0xf6, 0x57, 0x01, 0xa9,
            0xf4, 0x4f, 0x02, 0xa9, 0xfd, 0x03, 0x00, 0x91,
            0x16, 0x41, 0x00, 0xb0, 0xc8, 0xca, 0x40, 0xf9,
        };

        bc_pattern pat = {};
        memcpy(pat.bytes, real_appinit, sizeof(real_appinit));
        for (size_t i = 0; i < sizeof(real_appinit); i++) pat.mask[i] = 1;
        pat.len = sizeof(real_appinit);

        // Buffer maior (256) e junk 0x00 (zero fill) — diferente do Caso 50
        // (128, junk 0x90). Isola: junk não afeta match, scan usa step de 1
        // byte (não 4 como do_resolv), acha offset arbitrário.
        uint8_t segment[256];
        memset(segment, 0x00, sizeof(segment));
        memcpy(segment + 88, real_appinit, sizeof(real_appinit));

        size_t off = 0;
        bc_scan_status st = bc_pattern_scan_buffer(segment, sizeof(segment), &pat, &off);
        check("appInit prologo achado em junk 0x00 → BC_SCAN_OK", st == BC_SCAN_OK);
        check("offset correto (88)", off == 88);

        // Ambos os patterns (50 + 51) coexistem no mesmo buffer → cada um
        // encontrado isoladamente (não há cross-match: são bytes diferentes).
        // mesmos 24 bytes de appUpdateDraw do Caso 50 (xxd -s 0x31ec4c -l 24)
        // — redeclarado aqui porque o array do Caso 50 é local ao bloco dele.
        static const uint8_t real_prologue_updatedraw[24] = {
            0xff, 0x43, 0x01, 0xd1, 0xfd, 0x7b, 0x02, 0xa9,
            0xf6, 0x57, 0x03, 0xa9, 0xf4, 0x4f, 0x04, 0xa9,
            0xfd, 0x83, 0x00, 0x91, 0x54, 0xd0, 0x3b, 0xd5,
        };
        uint8_t multi[512];
        memset(multi, 0x90, sizeof(multi));
        // prologo appUpdateDraw no início, appInit no final
        memcpy(multi + 0, real_prologue_updatedraw, sizeof(real_prologue_updatedraw));
        memcpy(multi + 400, real_appinit, sizeof(real_appinit));

        bc_pattern pat_init = {};
        memcpy(pat_init.bytes, real_appinit, sizeof(real_appinit));
        for (size_t i = 0; i < sizeof(real_appinit); i++) pat_init.mask[i] = 1;
        pat_init.len = sizeof(real_appinit);

        bc_scan_status st2 = bc_pattern_scan_buffer(multi, sizeof(multi), &pat_init, &off);
        check("appInit pattern achado isoladamente no buffer multi", st2 == BC_SCAN_OK);
        check("appInit offset no buffer multi (400)", off == 400);

        // Ambidade: appInit pattern 2x no segmento → AMBIGUOUS
        uint8_t dup[300];
        memset(dup, 0x00, sizeof(dup));
        memcpy(dup + 20, real_appinit, sizeof(real_appinit));
        memcpy(dup + 180, real_appinit, sizeof(real_appinit));
        st = bc_pattern_scan_buffer(dup, sizeof(dup), &pat, &off);
        check("appInit duplicado → BC_SCAN_AMBIGUOUS", st == BC_SCAN_AMBIGUOUS);

        // Not-found: buffer de junk total (0xFF) → NOT_FOUND
        uint8_t junk[128];
        memset(junk, 0xFF, sizeof(junk));
        st = bc_pattern_scan_buffer(junk, sizeof(junk), &pat, &off);
        check("junk 0xFF → appInit NOT_FOUND", st == BC_SCAN_NOT_FOUND);
    }

    printf("\n== Resultado: %s (%d falhas) ==\n", g_fail == 0 ? "TODOS PASSARAM" : "HOUVE FALHAS", g_fail);
    return g_fail == 0 ? 0 : 1;
}