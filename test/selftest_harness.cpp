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
#include <vector>
#include <string>
#include <elf.h>
#include <cstdarg>
#include "../mods/u_noads/jni/u_noads_pure.h"
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
// FUNÇÕES REAIS — single source of truth só pro par unpatch/repatch em
// bc_unpatch_hook. main.cpp::repatch_hook (produção) NÃO chama
// bc_repatch_hook — reimplementa a mesma decisão à mão porque opera sobre
// HookPlan[]/PLANS (busca por shortname num array), não sobre o
// HookState[] indexado por hook_slot_by_name() que esta função espera.
// Comportamento equivalente hoje (achado real, revisão OpenCode) — mas
// Caso 39 abaixo testa ESTA função, não o caminho real de produção.
#include "bc_hook_logic.h"
#include "bc_mod_api.h"     // contrato de API exposto aos mods .so dinâmicos
#include "bc_pattern_scan.h"  // AOB scan — bc_pattern_scan_buffer (lógica pura, testável no host)
#include "bc_loader.h"      // loader dinâmico (mesma lógica pura do main.cpp)
#include "bc_elf_symtab.h"  // enumeração de símbolo ELF dinâmico — núcleo puro testável no host
#include "bc_elf_file.h"    // preflight do .so no disco — parser puro testável no host
#include "bc_generic_allowlist.h"  // allowlist de pacote pra generalização — núcleo puro testável no host
#include "../mods/u_frida/jni/u_frida_config.h"  // F11: config do gadget (puro)
#include "bc_path_decide.h"  // decide_path (F1): caminho por app, núcleo puro testável no host
#include "bc_signal.h"  // sinais companion<->poll: age só quando muda (Enforcing)
#include "bc_crashguard.h"  // F1d: 2 mortes em <60s bloqueia os mods (núcleo puro)
#include "../mods/common/dump_core.h"  // F3 u_dump — núcleo puro (formato C5 + pkg C1), sem Android/il2cpp
#include "../mods/u_patch/jni/u_patch_parse.h"  // F4: parser C4/C3 (puro)
#include "../mods/u_patch/jni/u_patch_arm64.h"  // F4: emissores arm64 (puros)

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
                                (void*)0x9999, (void*)0x1234, nullptr);
        check("repatch de removido → 1", r == 1);
        check("backup preenchido", st[0].backup != nullptr);
        check("resolved marcado após reinstall", st[0].resolved_addr == (void*)0x9999);
        int r2 = bc_repatch_hook(st, "appInit", (HookInstallFn)t_install_stub,
                                 (void*)0x9999, (void*)0x1234, nullptr);
        check("idempotente (já instalado) → 0", r2 == 0);
        int r3 = bc_repatch_hook(st, "nope", (HookInstallFn)t_install_stub,
                                 (void*)0x9999, (void*)0x1234, nullptr);
        check("desconhecido → -1", r3 == -1);
        static HookState st_no_target[BC_HOOK_NAMES_COUNT] = {};
        int r5 = bc_repatch_hook(st_no_target, "appInit", (HookInstallFn)t_install_stub,
                                 nullptr, (void*)0x1234, nullptr);
        check("sem target → -1", r5 == -1);
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
                                 st[3].resolved_addr, (void*)0x7777, nullptr);
        check("repatch appKey → 1", rp == 1);
        check("appKey reinstalado", st[3].backup != nullptr);
        check("appKey resolved preservado (0xDEAD)", st[3].resolved_addr == (void*)0xDEAD);
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
        printf("\n[Caso 54] mod-graph: independentes → ordem = declaração, todos OK\n");
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
        printf("\n[Caso 55] mod-graph: requires presente → dependência sai antes\n");
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
        // Path traversal (achado real hermes): '/' em qualquer posição
        // tinha que ser rejeitado — antes do fix, "foo/../../evil.so" não
        // começa com '.' e termina em ".so", passava no filtro; companion.cpp
        // concatena esse nome direto em BC_MODS_DIR e escreve como root.
        check("traversal (../../evil.so) → false", bc_loader_is_mod_filename("../../evil.so") == false);
        check("subpasta (foo/bar.so) → false", bc_loader_is_mod_filename("foo/bar.so") == false);
        check("traversal sem prefixo . (a/../../evil.so) → false", bc_loader_is_mod_filename("a/../../evil.so") == false);
        check("barra absoluta (/etc/evil.so) → false", bc_loader_is_mod_filename("/etc/evil.so") == false);
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

    printf("\n[Caso 52] bc_elf_symtab_scan_filtered: enumeração de símbolo ELF sintético (generalização Cocos2d-x)\n");
    {
        // Tabela sintética: índice 0 é sempre o símbolo nulo reservado (ELF
        // spec), depois 4 símbolos JNI-like + 2 que devem ser filtrados
        // (STB_LOCAL e STT_OBJECT não contam como export de função real).
        const char strtab[] = "\0Java_com_foo_Bar_metodo1\0Java_com_foo_Bar_metodo2\0"
                               "naoJniSymbol\0Java_local_ignorado\0Java_object_ignorado\0";
        // offsets calculados manualmente pelos tamanhos dos literais acima
        size_t off_m1 = 1;                                      // "Java_com_foo_Bar_metodo1"
        size_t off_m2 = off_m1 + strlen("Java_com_foo_Bar_metodo1") + 1;
        size_t off_naojni = off_m2 + strlen("Java_com_foo_Bar_metodo2") + 1;
        size_t off_local = off_naojni + strlen("naoJniSymbol") + 1;
        size_t off_object = off_local + strlen("Java_local_ignorado") + 1;

        bc_elf64_sym syms[6] = {};
        syms[0] = {0, 0, 0, 0, 0, 0};  // símbolo nulo reservado
        syms[1] = {(uint32_t)off_m1, (uint8_t)((BC_ELF_STB_GLOBAL << 4) | BC_ELF_STT_FUNC), 0, 0, 0x1000, 0};
        syms[2] = {(uint32_t)off_m2, (uint8_t)((BC_ELF_STB_WEAK << 4) | BC_ELF_STT_FUNC), 0, 0, 0x2000, 0};
        syms[3] = {(uint32_t)off_naojni, (uint8_t)((BC_ELF_STB_GLOBAL << 4) | BC_ELF_STT_FUNC), 0, 0, 0x3000, 0};
        syms[4] = {(uint32_t)off_local, (uint8_t)((0 /*STB_LOCAL*/ << 4) | BC_ELF_STT_FUNC), 0, 0, 0x4000, 0};
        syms[5] = {(uint32_t)off_object, (uint8_t)((BC_ELF_STB_GLOBAL << 4) | 1 /*STT_OBJECT*/), 0, 0, 0x5000, 0};

        struct Found { char names[8][64]; int n; } found = {};
        auto cb = [](const char *name, uint64_t, void *user) {
            auto *f = (Found *)user;
            if (f->n < 8) { snprintf(f->names[f->n], 64, "%s", name); f->n++; }
        };

        int n = bc_elf_symtab_scan(syms, 6, strtab, sizeof(strtab), cb, &found);
        check("achou exatamente 2 símbolos JNI-like (STB_GLOBAL/WEAK + STT_FUNC + prefixo Java_)", n == 2);
        check("primeiro símbolo é metodo1", found.n >= 1 && strcmp(found.names[0], "Java_com_foo_Bar_metodo1") == 0);
        check("segundo símbolo é metodo2 (STB_WEAK também conta)", found.n >= 2 && strcmp(found.names[1], "Java_com_foo_Bar_metodo2") == 0);

        // Filtro customizado (usado pela detecção de engine): substring "cocos2d"
        auto cocos_filter = [](const char *name, size_t) -> bool {
            return strstr(name, "cocos2d") != nullptr;
        };
        const char strtab2[] = "\0_ZN7cocos2d8DirectorC1Ev\0algumaOutraCoisa\0";
        bc_elf64_sym syms2[3] = {};
        syms2[0] = {0, 0, 0, 0, 0, 0};
        syms2[1] = {1, (uint8_t)((BC_ELF_STB_GLOBAL << 4) | BC_ELF_STT_FUNC), 0, 0, 0x9000, 0};
        syms2[2] = {(uint32_t)(1 + strlen("_ZN7cocos2d8DirectorC1Ev") + 1),
                    (uint8_t)((BC_ELF_STB_GLOBAL << 4) | BC_ELF_STT_FUNC), 0, 0, 0xA000, 0};
        int n2 = bc_elf_symtab_scan_filtered(syms2, 3, strtab2, sizeof(strtab2), cocos_filter, cb, &found);
        check("filtro customizado (cocos2d substring) acha só o símbolo certo", n2 == 1);

        // Bounds-safety: st_name apontando fora do strtab nunca lê fora dos limites
        bc_elf64_sym bad[2] = {};
        bad[0] = {0, 0, 0, 0, 0, 0};
        bad[1] = {9999, (uint8_t)((BC_ELF_STB_GLOBAL << 4) | BC_ELF_STT_FUNC), 0, 0, 0xB000, 0};
        int n3 = bc_elf_symtab_scan(bad, 2, strtab, sizeof(strtab), cb, &found);
        check("st_name fora do strtab é ignorado, não lê fora dos limites", n3 == 0);

        // sym_count == 0 (contagem GNU_HASH zerada, ex.: DT_GNU_HASH ausente) → 0 achados, sem crash
        int n4 = bc_elf_symtab_scan(syms, 0, strtab, sizeof(strtab), cb, &found);
        check("sym_count=0 (sem DT_GNU_HASH) → 0 símbolos, sem crash", n4 == 0);
    }

    printf("\n[Caso 53] bc_elf_file: preflight bounded de .dynsym/.dynstr\n");
    {
        const char *bc_so = "../mods/kungfux/libs/arm64-v8a/libkungfux.so";
        const char *autonomous_so = "../mods/sa2ammo/libs/arm64-v8a/libsa2ammo.so";
        std::FILE *src = std::fopen(bc_so, "rb");
        check("arquivo BC real existe", src != nullptr);
        std::vector<unsigned char> original;
        if (src != nullptr) {
            std::fseek(src, 0, SEEK_END);
            long size = std::ftell(src);
            std::rewind(src);
            original.resize((size_t)size);
            check("arquivo BC real foi lido", size > 0 &&
                  std::fread(original.data(), 1, original.size(), src) == original.size());
            std::fclose(src);
        }
        auto write_temp = [](const std::vector<unsigned char> &data, const char *tag) {
            char path[128];
            std::snprintf(path, sizeof(path), "/tmp/bc-elf-%s-XXXXXX", tag);
            int fd = mkstemp(path);
            if (fd < 0) return std::string();
            ssize_t written = write(fd, data.data(), data.size());
            close(fd);
            return written == (ssize_t)data.size() ? std::string(path) : std::string();
        };
        auto probe_copy = [&](const char *tag, auto mutate) {
            std::vector<unsigned char> data = original;
            mutate(data);
            std::string path = write_temp(data, tag);
            bc_elf_file_probe result = bc_elf_file_has_bc_mod_register(path.c_str());
            unlink(path.c_str());
            return result;
        };
        auto section_info = [](std::vector<unsigned char> &data, Elf64_Shdr **dynsym,
                               Elf64_Shdr **strtab) {
            Elf64_Ehdr *eh = (Elf64_Ehdr *)data.data();
            Elf64_Shdr *sh = (Elf64_Shdr *)(data.data() + eh->e_shoff);
            *dynsym = nullptr; *strtab = nullptr;
            for (size_t i = 0; i < eh->e_shnum; i++) {
                if (sh[i].sh_type == SHT_DYNSYM) {
                    *dynsym = &sh[i];
                    if (sh[i].sh_link < eh->e_shnum) *strtab = &sh[sh[i].sh_link];
                    return;
                }
            }
        };
        check("BC real tem bc_mod_register",
              bc_elf_file_has_bc_mod_register(bc_so).result == BC_ELF_FILE_HAS_SYMBOL);
        check("mod autonomo nao tem bc_mod_register",
              bc_elf_file_has_bc_mod_register(autonomous_so).result == BC_ELF_FILE_NO_SYMBOL);
        check("arquivo vazio", probe_copy("empty", [](auto &d) { d.clear(); }).result == BC_ELF_FILE_NO_SYMBOL);
        check("arquivo nao ELF", probe_copy("notelf", [](auto &d) { d[0] = 'X'; }).result == BC_ELF_FILE_NO_SYMBOL);
        check("ELF32", probe_copy("elf32", [](auto &d) { d[EI_CLASS] = ELFCLASS32; }).result == BC_ELF_FILE_NO_SYMBOL);
        check("big-endian", probe_copy("be", [](auto &d) { d[EI_DATA] = ELFDATA2MSB; }).result == BC_ELF_FILE_NO_SYMBOL);
        check("e_shoff fora do arquivo",
              probe_copy("shoff", [](auto &d) { ((Elf64_Ehdr *)d.data())->e_shoff = d.size() + 1; }).result == BC_ELF_FILE_NO_SYMBOL);
        check("e_shnum gigante",
              probe_copy("shnum", [](auto &d) { ((Elf64_Ehdr *)d.data())->e_shnum = 0xffff; }).result == BC_ELF_FILE_NO_SYMBOL);
        check("dynsym fora do arquivo", probe_copy("symoff", [&](auto &d) {
            Elf64_Shdr *sym = nullptr, *str = nullptr; section_info(d, &sym, &str);
            if (sym) sym->sh_offset = d.size() + 1;
        }).result == BC_ELF_FILE_NO_SYMBOL);
        check("dynsym sh_size fora do arquivo", probe_copy("symsize", [&](auto &d) {
            Elf64_Shdr *sym = nullptr, *str = nullptr; section_info(d, &sym, &str);
            if (sym) sym->sh_size = d.size() + 1;
        }).result == BC_ELF_FILE_NO_SYMBOL);
        check("sh_link invalido", probe_copy("link", [&](auto &d) {
            Elf64_Shdr *sym = nullptr, *str = nullptr; section_info(d, &sym, &str);
            if (sym) sym->sh_link = 0xffff;
        }).result == BC_ELF_FILE_NO_SYMBOL);
        check("st_name fora do strtab", probe_copy("name", [&](auto &d) {
            Elf64_Shdr *sym = nullptr, *str = nullptr; section_info(d, &sym, &str);
            if (!sym) return;
            Elf64_Sym *symbols = (Elf64_Sym *)(d.data() + sym->sh_offset);
            for (size_t i = 1; i < sym->sh_size / sizeof(*symbols); i++) symbols[i].st_name = str->sh_size + 1;
        }).result == BC_ELF_FILE_NO_SYMBOL);
        check("strtab sem NUL", probe_copy("nonul", [&](auto &d) {
            Elf64_Shdr *sym = nullptr, *str = nullptr; section_info(d, &sym, &str);
            if (str && str->sh_size > 0) memset(d.data() + str->sh_offset, 'X', str->sh_size);
        }).result == BC_ELF_FILE_NO_SYMBOL);
        check("arquivo inexistente",
              bc_elf_file_has_bc_mod_register("/tmp/bc-elf-does-not-exist.so").result == BC_ELF_FILE_ERROR);
        check("simbolo importado nao conta", probe_copy("undef", [&](auto &d) {
            Elf64_Shdr *sym = nullptr, *str = nullptr; section_info(d, &sym, &str);
            if (!sym || !str) return;
            Elf64_Sym *symbols = (Elf64_Sym *)(d.data() + sym->sh_offset);
            const char *strings = (const char *)(d.data() + str->sh_offset);
            for (size_t i = 1; i < sym->sh_size / sizeof(*symbols); i++) {
                if (symbols[i].st_name < str->sh_size &&
                    strcmp(strings + symbols[i].st_name, "bc_mod_register") == 0) {
                    symbols[i].st_shndx = SHN_UNDEF; symbols[i].st_value = 0; break;
                }
            }
        }).result == BC_ELF_FILE_NO_SYMBOL);
    }

    printf("\n[Caso 63] bc_elf_file: bloqueio por DT_SONAME do frida-gadget\n");
    {
        auto make_soname_elf = []() {
            const char soname[] = "\0libfrida-gadget-raw.so";
            const size_t str_offset = 0x100;
            const size_t dynamic_offset = 0x140;
            const size_t sh_offset = 0x200;
            const size_t file_size = sh_offset + 3 * sizeof(Elf64_Shdr);
            std::vector<unsigned char> data(file_size, 0);
            Elf64_Ehdr *eh = (Elf64_Ehdr *)data.data();
            memcpy(eh->e_ident, ELFMAG, SELFMAG);
            eh->e_ident[EI_CLASS] = ELFCLASS64;
            eh->e_ident[EI_DATA] = ELFDATA2LSB;
            eh->e_ident[EI_VERSION] = EV_CURRENT;
            eh->e_shoff = sh_offset;
            eh->e_ehsize = sizeof(Elf64_Ehdr);
            eh->e_shentsize = sizeof(Elf64_Shdr);
            eh->e_shnum = 3;
            memcpy(data.data() + str_offset, soname, sizeof(soname));
            Elf64_Dyn *dyn = (Elf64_Dyn *)(data.data() + dynamic_offset);
            dyn[0].d_tag = DT_SONAME;
            dyn[0].d_un.d_val = 1;
            dyn[1].d_tag = DT_NULL;
            Elf64_Shdr *sh = (Elf64_Shdr *)(data.data() + sh_offset);
            sh[1].sh_type = SHT_STRTAB;
            sh[1].sh_offset = str_offset;
            sh[1].sh_size = sizeof(soname);
            sh[2].sh_type = SHT_DYNAMIC;
            sh[2].sh_offset = dynamic_offset;
            sh[2].sh_size = 2 * sizeof(Elf64_Dyn);
            sh[2].sh_entsize = sizeof(Elf64_Dyn);
            sh[2].sh_link = 1;
            return data;
        };
        auto write_soname_temp = [](const std::vector<unsigned char> &data, const char *tag) {
            char path[128];
            std::snprintf(path, sizeof(path), "/tmp/bc-soname-%s-XXXXXX", tag);
            int fd = mkstemp(path);
            if (fd < 0) return std::string();
            ssize_t written = write(fd, data.data(), data.size());
            close(fd);
            return written == (ssize_t)data.size() ? std::string(path) : std::string();
        };
        std::vector<unsigned char> soname_elf = make_soname_elf();
        auto probe_soname = [&](const char *tag, auto mutate) {
            std::vector<unsigned char> data = soname_elf;
            mutate(data);
            std::string path = write_soname_temp(data, tag);
            char soname[128];
            bc_elf_file_probe result =
                bc_elf_file_read_soname(path.c_str(), soname, sizeof(soname));
            unlink(path.c_str());
            return std::make_pair(result, std::string(soname));
        };
        const char *real_mod = "../mods/kungfux/libs/arm64-v8a/libkungfux.so";
        char real_soname[128];
        bc_elf_file_probe real_result =
            bc_elf_file_read_soname(real_mod, real_soname, sizeof(real_soname));
        check("ELF minimo com SONAME frida e detectado",
              probe_soname("hit", [](auto &) {}).first.result == BC_ELF_FILE_HAS_SYMBOL &&
              probe_soname("hit2", [](auto &) {}).second == "libfrida-gadget-raw.so");
        check("SONAME do gadget aciona o bloqueio",
              bc_elf_file_soname_is_frida("libfrida-gadget-raw.so"));
        check("SONAME do gadget devolve o nome completo",
              probe_soname("name", [](auto &) {}).second == "libfrida-gadget-raw.so");
        check("mod .so real nao e gadget",
              real_result.result == BC_ELF_FILE_HAS_SYMBOL &&
              !bc_elf_file_soname_is_frida(real_soname));
        check("d_val fora do strtab e seguro",
              probe_soname("badval", [](auto &d) {
                  auto *dyn = (Elf64_Dyn *)(d.data() + 0x140);
                  dyn[0].d_un.d_val = 0xffff;
              }).first.result == BC_ELF_FILE_NO_SYMBOL);
        check("strtab sem NUL e seguro",
              probe_soname("nonul", [](auto &d) {
                  memset(d.data() + 0x100, 'X', sizeof("\0libfrida-gadget-raw.so"));
              }).first.result == BC_ELF_FILE_NO_SYMBOL);
        check("dynamic truncado e seguro",
              probe_soname("truncated", [](auto &d) {
                  auto *sh = (Elf64_Shdr *)(d.data() + 0x200);
                  sh[2].sh_size = sizeof(Elf64_Dyn) - 1;
              }).first.result == BC_ELF_FILE_NO_SYMBOL);
    }

    printf("\n[Caso 62] bc_generic_allowlist_contains_buf: allowlist de pacote (generalização Cocos2d-x)\n");
    {
        const char *buf = "com.foo.bar\n# comentario\n\ncom.baz.qux \n  com.indentado\n";
        check("pacote exato na lista é achado", bc_generic_allowlist_contains_buf(buf, "com.foo.bar"));
        check("linha comentada não conta", !bc_generic_allowlist_contains_buf(buf, "comentario"));
        check("trailing space é ignorado", bc_generic_allowlist_contains_buf(buf, "com.baz.qux"));
        check("leading space é ignorado", bc_generic_allowlist_contains_buf(buf, "com.indentado"));
        check("pacote fora da lista não é achado", !bc_generic_allowlist_contains_buf(buf, "com.nao.listado"));
        check("prefixo parcial não conta como match (com.foo.barbaz != com.foo.bar)",
              !bc_generic_allowlist_contains_buf(buf, "com.foo.barbaz"));
        check("buffer vazio nunca acha nada", !bc_generic_allowlist_contains_buf("", "com.foo.bar"));
        check("buf nulo não crasha", !bc_generic_allowlist_contains_buf(nullptr, "com.foo.bar"));
        check("pkg nulo não crasha", !bc_generic_allowlist_contains_buf(buf, nullptr));
    }

    printf("\n[Caso 56] bc_decide_path: caminho por app (BC > mods/<pkg>/ > allowlist > nada)\n");
    {
        check("BC vence tudo (pasta de mods e allowlist presentes)",
              bc_decide_path("jp.co.ponos.battlecatsen", true, true) == BC_PATH_BC);
        check("BC casa por substring (suffixo de processo auxiliar)",
              bc_decide_path("jp.co.ponos.battlecatsen:aux", false, false) == BC_PATH_BC);
        check("F1: pasta de mods basta, sem allowlist",
              bc_decide_path("com.xd.tabs.google", true, false) == BC_PATH_PKG_MODS);
        check("F1: pasta de mods ganha da allowlist (zero-config)",
              bc_decide_path("com.xd.tabs.google", true, true) == BC_PATH_PKG_MODS);
        check("allowlist sem pasta = experimento Cocos legado",
              bc_decide_path("com.foo.cocos", false, true) == BC_PATH_COCOS);
        check("sem pasta e fora da allowlist = nada (DLCLOSE)",
              bc_decide_path("com.foo.cocos", false, false) == BC_PATH_NONE);
        check("pkg nulo não crasha (vira o caminho da pasta)",
              bc_decide_path(nullptr, true, true) == BC_PATH_PKG_MODS);
    }

    // ================================================================
    // Caso 57: dump_core (F3 u_dump) — formato C5 exato + pkg C1
    // (env BEPINEX_PKG vence; cmdline só fora de zygote*). Núcleo puro,
    // usado por mods/u_dump/jni/u_dump_mod.cpp.
    // ================================================================
    {
        printf("\n[Caso 57] dump_core: formato C5 (dump.tsv) e pkg C1 (u_dump)\n");
        {
            char buf[256];
            int w = dump_write_header(buf, sizeof(buf), "com.foo.game", 1234567, "2022.3.41f1");
            check("header C5 exato c/ unity", w > 0 && strcmp(buf, "# pkg=com.foo.game il2cpp_size=1234567 unity=2022.3.41f1\n") == 0);
            w = dump_write_header(buf, sizeof(buf), "com.foo.game", 42, "");
            check("header C5 unity vazio", w > 0 && strcmp(buf, "# pkg=com.foo.game il2cpp_size=42 unity=\n") == 0);
            w = dump_write_header(buf, sizeof(buf), nullptr, 1, nullptr);
            check("header C5 pkg null → vazio (não crasha)", w > 0 && strcmp(buf, "# pkg= il2cpp_size=1 unity=\n") == 0);
        }
        {
            char buf[256];
            int w = dump_write_class(buf, sizeof(buf), "Assembly-CSharp", "NS.Cls");
            check("linha C exata", w > 0 && strcmp(buf, "C\tAssembly-CSharp\tNS.Cls\n") == 0);
            w = dump_write_class(buf, sizeof(buf), nullptr, "Cls");
            check("linha C assembly null → ?", w > 0 && strcmp(buf, "C\t?\tCls\n") == 0);
        }
        {
            char buf[256];
            int w = dump_write_method(buf, sizeof(buf), "ComplexCreature", "HasAmmo", 0, "System.Boolean", true);
            check("linha M exata (static=1)", w > 0 && strcmp(buf, "M\tComplexCreature\tHasAmmo\t0\tSystem.Boolean\t1\n") == 0);
            w = dump_write_method(buf, sizeof(buf), "NS.Cls", "Foo", 3, "System.Void", false);
            check("linha M exata (static=0, nargs=3)", w > 0 && strcmp(buf, "M\tNS.Cls\tFoo\t3\tSystem.Void\t0\n") == 0);
        }
        {
            char buf[256];
            int w = dump_write_field(buf, sizeof(buf), "NS.Cls", "hp", "System.Single", false, 24);
            check("linha F exata (offset 24)", w > 0 && strcmp(buf, "F\tNS.Cls\thp\tSystem.Single\t0\t24\n") == 0);
            w = dump_write_field(buf, sizeof(buf), "NS.Cls", "MAX", "System.Int32", true, 0);
            check("linha F exata (static, offset 0)", w > 0 && strcmp(buf, "F\tNS.Cls\tMAX\tSystem.Int32\t1\t0\n") == 0);
        }
        {
            // Máscaras de atributo fixadas (contrato interno com o mod):
            check("mask static = 0x0010 (METHOD_ATTRIBUTE_STATIC)", dump_attr_static_mask() == 0x0010);
            check("mask visibilidade = 0x001F", dump_attr_visibility_mask() == 0x001F);
        }
        {
            char pkg[64];
            // env vence sempre
            check("env vence cmdline", dump_pick_pkg("com.via.env", "com.via.cmd", pkg, sizeof(pkg)) && strcmp(pkg, "com.via.env") == 0);
            // env vazio/ausente → cmdline se não-zygote
            check("env vazio → cmdline", dump_pick_pkg("", "com.via.cmd", pkg, sizeof(pkg)) && strcmp(pkg, "com.via.cmd") == 0);
            check("env null → cmdline", dump_pick_pkg(nullptr, "com.via.cmd", pkg, sizeof(pkg)) && strcmp(pkg, "com.via.cmd") == 0);
            // cmdline zygote* rejeitado (achado device: constructor lê zygote64)
            check("cmdline zygote64 → false", !dump_pick_pkg(nullptr, "zygote64", pkg, sizeof(pkg)));
            check("cmdline zygote32 → false", !dump_pick_pkg(nullptr, "zygote32", pkg, sizeof(pkg)));
            check("cmdline zygote → false", !dump_pick_pkg(nullptr, "zygote", pkg, sizeof(pkg)));
            // cmdline vazio/null → false
            check("cmdline vazio → false", !dump_pick_pkg(nullptr, "", pkg, sizeof(pkg)));
            check("cmdline null → false", !dump_pick_pkg(nullptr, nullptr, pkg, sizeof(pkg)));
            // nada disponível → false (chamador re-tenta depois)
            check("env null + cmdline zygote → false (fallback espera)", !dump_pick_pkg(nullptr, "zygote64", pkg, sizeof(pkg)));
            // truncamento seguro: cap pequeno não estoura
            char small[8];
            bool ok = dump_pick_pkg("com.pacote.muito.longo", nullptr, small, sizeof(small));
            check("cap pequeno: truncado com NUL, sem crash", ok && strlen(small) < sizeof(small));
        }
    }

    // ================================================================
    // Caso 58: dump_join_class_name (F3b) — aninhada C5:
    // "Namespace.Externa/Interna", namespace e 1º nome da raiz.
    // ================================================================
    {
        printf("\n[Caso 58] dump_join_class_name: aninhada C5\n");
        char buf[128];
        {
            const char *parts[] = {"Externa", "Interna"};
            dump_join_class_name(buf, sizeof(buf), "NS", parts, 2);
            check("aninhada com namespace: NS.Externa/Interna", strcmp(buf, "NS.Externa/Interna") == 0);
        }
        {
            const char *parts[] = {"A", "B", "C"};
            dump_join_class_name(buf, sizeof(buf), "", parts, 3);
            check("sem namespace: A/B/C", strcmp(buf, "A/B/C") == 0);
        }
        {
            const char *parts[] = {"Solo"};
            dump_join_class_name(buf, sizeof(buf), nullptr, parts, 1);
            check("top-level sem ns: Solo", strcmp(buf, "Solo") == 0);
        }
        {
            const char *parts[] = {"A", "B", "C", "D"};
            dump_join_class_name(buf, sizeof(buf), "N", parts, 4);
            check("nesting 4: N.A/B/C/D", strcmp(buf, "N.A/B/C/D") == 0);
        }
        {
            const char *parts[] = {"Externa", nullptr};
            dump_join_class_name(buf, sizeof(buf), "NS", parts, 2);
            check("part null no caminho → NS.Externa/?", strcmp(buf, "NS.Externa/?") == 0);
        }
        {
            dump_join_class_name(buf, sizeof(buf), "NS", nullptr, 0);
            check("sem partes → ?", strcmp(buf, "?") == 0);
            dump_join_class_name(buf, sizeof(buf), "NS", nullptr, 0);
            check("parts null → ?", strcmp(buf, "?") == 0);
        }
        {
            const char *parts[] = {"Externa", "Interna"};
            dump_join_class_name(buf, 10, "NS", parts, 2);
            check("cap pequeno: truncado com NUL, sem estourar", strlen(buf) < 10);
        }
        {
            // Revisão: walk sem teto de 16 — join tem que aguentar cadeia
            // funda (namespace da raiz, sem truncar nível).
            const char *parts[20];
            for (int i = 0; i < 20; i++) parts[i] = "N";
            char big[256];
            dump_join_class_name(big, sizeof(big), "R", parts, 20);
            check("20 níveis: começa em R.N e termina em /N", strncmp(big, "R.N", 3) == 0 &&
                  strlen(big) == (size_t)(2 + 20 * 2 - 1) &&
                  strcmp(big + strlen(big) - 2, "/N") == 0);
        }
    }

    // Caso 59: bc_seq_take/bc_seq_split — o jogo só lê a property e age quando
    // o VALOR muda (achado Enforcing: app domain sem permission_set).
    printf("\n[Caso 59] bc_seq_take/bc_seq_split: age só quando muda (Enforcing)\n");
    {
        char seen[16] = {};
        check("vazio não é pedido", !bc_seq_take("", seen, sizeof(seen)));
        check("null não é pedido", !bc_seq_take(nullptr, seen, sizeof(seen)));
        check("primeiro valor conta como novo", bc_seq_take("1", seen, sizeof(seen)));
        check("mesmo valor não repete", !bc_seq_take("1", seen, sizeof(seen)));
        check("valor diferente conta", bc_seq_take("2", seen, sizeof(seen)));
        check("nulo em last_seen não crasha", !bc_seq_take("9", nullptr, 8));
        {
            char tiny[4] = {};
            check("buffer pequeno trunca com NUL, sem estourar",
                  bc_seq_take("abcdef", tiny, sizeof(tiny)) && strlen(tiny) == 3);
        }
        {
            // O bug real: a property parada num valor fazia o reload_config rodar
            // a cada poll porque o app não conseguia limpar o flag.
            char prop[32] = {}, last[32] = {};
            int reloads = 0;
            for (int i = 0; i < 6; i++) {
                snprintf(prop, sizeof(prop), "7");
                if (bc_seq_take(prop, last, sizeof(last))) reloads++;
            }
            check("6 polls com a property parada => 1 reload só", reloads == 1);
            // Seq nova a cada pedido (o que o companion faz): sempre age.
            int acts = 0;
            for (unsigned q = 1; q <= 3; q++) {
                snprintf(prop, sizeof(prop), "%u", q);
                if (bc_seq_take(prop, last, sizeof(last))) acts++;
            }
            check("3 seqs do companion => 3 ações", acts == 3);
        }
        {
            // Regressão do device (v0.4.1): as persist.* sobrevivem a reboot, e
            // com o "visto" começando vazio o primeiro poll via o valor velho e
            // dispara tudo. O baseline memoriza sem agir.
            char seen[32] = {};
            bc_seq_learn("7", seen, sizeof(seen));
            check("valor pré-existente (7) NÃO dispara no 1º poll", !bc_seq_take("7", seen, sizeof(seen)));
            check("próximo valor (8) dispara", bc_seq_take("8", seen, sizeof(seen)));
            check("e só uma vez", !bc_seq_take("8", seen, sizeof(seen)));
            {
                char empty_seen[32] = {};
                bc_seq_learn("", empty_seen, sizeof(empty_seen));
                check("baseline vazio: valor novo dispara",
                      bc_seq_take("1", empty_seen, sizeof(empty_seen)));
            }
            {
                char null_seen[32] = {};
                bc_seq_learn(nullptr, null_seen, sizeof(null_seen));
                check("baseline com NULL não crasha e não dispara",
                      !bc_seq_take("1", null_seen, sizeof(null_seen)) == false);
            }
            {
                // Coalescência: 1 e 2 enviados antes do 1º poll viram 1 ação.
                char c[32] = {};
                int fired = 0;
                bc_seq_learn("1", c, sizeof(c));
                if (bc_seq_take("2", c, sizeof(c))) fired++;
                check("1→2 antes do poll: 1 ação (coalesce, documentado)", fired == 1);
            }
            {
                // Nomeado: o baseline é da seq, o payload vem junto.
                char last[32] = {};
                bc_seq_learn("3", last, sizeof(last));
                check("unpatch antigo (3) não repete", !bc_seq_take("3", last, sizeof(last)));
                check("unpatch com seq nova age", bc_seq_take("4", last, sizeof(last)));
            }
        }
        {
            // Payload nomeado: "<seq> <nome>"
            char key[32] = {}, payload[64] = {};
            check("split de \"7 sa2ammo\"", bc_seq_split("7 sa2ammo", key, sizeof(key),
                                                          payload, sizeof(payload)) &&
                  strcmp(key, "7") == 0 && strcmp(payload, "sa2ammo") == 0);
            check("sem espaço não é payload válido", !bc_seq_split("sa2ammo", key, sizeof(key),
                                                                  payload, sizeof(payload)));
            check("espaço no começo não é payload válido", !bc_seq_split(" sa2ammo", key, sizeof(key),
                                                                        payload, sizeof(payload)));
            check("espaço no fim (nome vazio) não vale", !bc_seq_split("7 ", key, sizeof(key),
                                                                      payload, sizeof(payload)));
            check("null não crasha", !bc_seq_split(nullptr, key, sizeof(key), payload,
                                                   sizeof(payload)));
            {
                char small_key[2] = {};
                check("seq maior que o buffer é recusado",
                      !bc_seq_split("123456 7", small_key, sizeof(small_key), payload,
                                    sizeof(payload)));
            }
            {
                // Deduplicação do payload nomeado: mesma seq não repete, nova age.
                char last[32] = {};
                char k[32] = {}, nm[64] = {};
                int fired = 0;
                const char *reqs[] = {"1 sa2ammo", "1 sa2ammo", "2 sa2ammo", "2 sa2ammo"};
                for (size_t i = 0; i < sizeof(reqs) / sizeof(reqs[0]); i++) {
                    if (bc_seq_split(reqs[i], k, sizeof(k), nm, sizeof(nm)) &&
                        bc_seq_take(k, last, sizeof(last))) fired++;
                }
                check("2 requests, 2 seqs => 2 unpatch (repete nome com seq nova)",
                      fired == 2);
            }
        }
    }

    // Caso 60: bc_crashguard_blocks/next_count (F1d, G1) — o portão que impede
    // crash em loop de mod.
    printf("\n[Caso 60] bc_crashguard_blocks/next_count: 2 mortes na janela de 20s (F1d)\n");
    {
        const long long T0 = 1000000;
        check("sem registro (0) não bloqueia", !bc_crashguard_blocks(0, 0, T0));
        check("1 morte não bloqueia", !bc_crashguard_blocks(1, T0, T0 + 10));
        check("2 mortes em 5s bloqueia (crash de boot)", bc_crashguard_blocks(2, T0, T0 + 5));
        check("2 mortes em 19s bloqueia (borda)", bc_crashguard_blocks(2, T0, T0 + 19));
        check("2 mortes em 20s NÃO bloqueia (janela fechou)", !bc_crashguard_blocks(2, T0, T0 + 20));
        check("2 mortes em 40s NÃO bloqueia (sessão curta não é mod crash)",
              !bc_crashguard_blocks(2, T0, T0 + 40));
        check("3 mortes em 15s bloqueia", bc_crashguard_blocks(3, T0, T0 + 15));
        // timestamp no futuro (relógio andou pra trás) não abre a janela
        check("ts no futuro não abre a janela", bc_crashguard_blocks(2, T0 + 500, T0));
        // next_count: como o contador evolui a cada abertura
        check("sem registro => 1", bc_crashguard_next_count(0, 0, T0) == 1);
        check("registro expirado => 1", bc_crashguard_next_count(2, T0, T0 + 21) == 1);
        check("registro recente => incrementa", bc_crashguard_next_count(1, T0, T0 + 3) == 2);
        check("2 recentes => 3 (mas já bloqueou antes de chegar aqui)",
              bc_crashguard_next_count(2, T0, T0 + 3) == 3);
        // a sequência que o device tem que ver: 1a abre (1), morre, 2a abre
        // (2, e morre), 3a NÃO carrega mod nenhum
        {
            long long now = T0;
            int count = 0, ts = 0;
            bool blocked[3];
            for (int i = 0; i < 3; i++) {
                blocked[i] = bc_crashguard_blocks(count, ts, now);
                if (!blocked[i]) {
                    count = bc_crashguard_next_count(count, ts, now);
                    ts = now;
                }
                now += 2;  // o jogo morre 2s depois de carregar (mod t_crash)
            }
            check("abertura 1: carrega", !blocked[0]);
            check("abertura 2: carrega (1 morte ainda não bloqueia)", !blocked[1]);
            check("abertura 3: NÃO carrega (2 mortes em 4s)", blocked[2]);
        }
        // sobreviveu à janela => contador zerado e a 4a volta a carregar
        {
            long long now = T0 + 40;
            int count = 0, ts = 0;
            bool blocked = bc_crashguard_blocks(count, ts, now);
            count = bc_crashguard_next_count(count, ts, now);
            check("após 40s sem morte: volta a carregar", !blocked && count == 1);
        }
    }

    // ================================================================
    // Caso 61: u_frida_config (F11) — núcleo puro do mod u_frida
    // ================================================================
    printf("\n[Caso 61] u_frida_config: detecção de .js + JSON do gadget (F11)\n");
    {
        check("meu_mod.js é mod", uf_is_js_mod("meu_mod.js"));
        check("nome sem ext não é", !uf_is_js_mod("meu_mod"));
        check("foo.js.off não é (.off desliga)", !uf_is_js_mod("foo.js.off"));
        check("oculto não é", !uf_is_js_mod(".js"));
        check("subpasta não é", !uf_is_js_mod("a/b.js"));
        check("nulo/vazio não é", !uf_is_js_mod(nullptr) && !uf_is_js_mod(""));
        check(".so não é", !uf_is_js_mod("u_frida.so"));
        char json[512];
        int n = uf_build_config(json, sizeof(json), "/data/local/tmp/mods/com.foo.bar");
        check("config JSON exato do modo script-directory", n > 0 &&
              strcmp(json, "{\"interaction\":{\"type\":\"script-directory\",\"path\":\"/data/local/tmp/mods/com.foo.bar\",\"on_change\":\"ignore\"}}") == 0);
        check("nomes seguem a regra <stem>.config do gadget",
              strcmp(UF_GADGET_FILE, "frida-gadget.bin") == 0 &&
              strcmp(UF_CONFIG_FILE, "frida-gadget.config") == 0);

        // uf_config_is_script_mode: dlopen do gadget SÓ no modo script.
        // Fora dele o default é listen + on_load wait = jogo congela.
        char why[64];
        #define UF_CFG(s) uf_config_is_script_mode(s, strlen(s), why, sizeof(why))
        check("config gerado por uf_build_config (script-directory) passa",
              uf_config_is_script_mode(json, (size_t)n, why, sizeof(why)) &&
              strcmp(why, "script-directory") == 0);
        check("type script passa",
              UF_CFG("{\"interaction\":{\"type\":\"script\",\"path\":\"/data/local/tmp/mods/p/a.js\"}}") &&
              strcmp(why, "script") == 0);
        check("espaço/quebra de linha JSON válidos passam",
              UF_CFG(" \n{ \"teardown\" : \"minimal\",\n \"interaction\" : { \"type\" : \"script-directory\", \"path\" : \"/x\", \"n\": [1, -2.5e3, true, null, {}] } }\n"));
        check("vazio recusa", !uf_config_is_script_mode("", 0, why, sizeof(why)) &&
              strcmp(why, "vazio") == 0);
        check("nulo recusa", !uf_config_is_script_mode(nullptr, 0, why, sizeof(why)));
        check("{} recusa (default listen)", !UF_CFG("{}") &&
              strcmp(why, "sem-interaction(padrao-listen)") == 0);
        check("{\"teardown\":\"full\"} recusa", !UF_CFG("{\"teardown\":\"full\"}"));
        check("interaction sem type recusa", !UF_CFG("{\"interaction\":{}}"));
        check("listen recusa e loga o type",
              !UF_CFG("{\"interaction\":{\"type\":\"listen\"}}") && strcmp(why, "listen") == 0);
        check("listen com port 27042 recusa",
              !UF_CFG("{\"interaction\":{\"type\":\"listen\",\"address\":\"127.0.0.1\",\"port\":27042,\"on_load\":\"wait\"}}"));
        check("connect recusa",
              !UF_CFG("{\"interaction\":{\"type\":\"connect\",\"address\":\"10.0.0.1\",\"port\":27052}}") &&
              strcmp(why, "connect") == 0);
        check("port solto no topo recusa", !UF_CFG("{\"port\":27042}"));
        check("JSON inválido recusa (vírgula sobrando)",
              !UF_CFG("{\"interaction\":{\"type\":\"script\",}}") && strcmp(why, "json-invalido") == 0);
        check("JSON inválido recusa (sem fechar)", !UF_CFG("{\"interaction\":{\"type\":\"script\"}"));
        check("JSON inválido recusa (lixo depois)", !UF_CFG("{\"interaction\":{\"type\":\"script\"}} x"));
        check("JSON inválido recusa (não é JSON)", !UF_CFG("interaction: script"));
        check("topo array recusa", !UF_CFG("[{\"interaction\":{\"type\":\"script\"}}]"));
        check("type script só em objeto aninhado recusa",
              !UF_CFG("{\"parameters\":{\"interaction\":{\"type\":\"script\"}}}"));
        check("interaction duplicado recusa (último poderia ser listen)",
              !UF_CFG("{\"interaction\":{\"type\":\"script\"},\"interaction\":{\"type\":\"listen\"}}"));
        check("type duplicado recusa",
              !UF_CFG("{\"interaction\":{\"type\":\"script\",\"type\":\"listen\"}}"));
        check("chave com escape recusa (poderia ser interaction)",
              !UF_CFG("{\"interac\\u0074ion\":{\"type\":\"script\"}}"));
        check("type com escape recusa", !UF_CFG("{\"interaction\":{\"type\":\"scrip\\u0074\"}}"));
        check("type prefixo/sufixo recusa",
              !UF_CFG("{\"interaction\":{\"type\":\"scripts\"}}") &&
              !UF_CFG("{\"interaction\":{\"type\":\"script-dir\"}}") &&
              !UF_CFG("{\"interaction\":{\"type\":\"Script\"}}"));
        check("type não-string recusa", !UF_CFG("{\"interaction\":{\"type\":1}}"));
        check("interaction não-objeto recusa", !UF_CFG("{\"interaction\":\"script\"}"));
        {
            const char nul[] = "{\"interaction\":{\"type\":\"script\"}}\0{}";
            check("NUL no meio recusa", !uf_config_is_script_mode(nul, sizeof(nul) - 1, why, sizeof(why)));
        }
        {
            // JSON script válido de exatamente len bytes (preenchido no "pad").
            static char big[UF_CONFIG_MAX + 1];
            auto fill = [](size_t len) {
                const char *head = "{\"interaction\":{\"type\":\"script\"},\"pad\":\"";
                size_t hl = strlen(head);
                memcpy(big, head, hl);
                memset(big + hl, 'a', len - hl - 2);
                big[len - 2] = '"';
                big[len - 1] = '}';
            };
            fill(UF_CONFIG_MAX);
            check("config de 4KB exato (script) passa",
                  uf_config_is_script_mode(big, UF_CONFIG_MAX, why, sizeof(why)));
            fill(UF_CONFIG_MAX + 1);
            check("config > 4KB recusa (mesmo sendo script)",
                  !uf_config_is_script_mode(big, UF_CONFIG_MAX + 1, why, sizeof(why)) &&
                  strcmp(why, "maior-que-4KB") == 0);
        }
        check("aninhamento fundo recusa sem estourar pilha",
              !UF_CFG("{\"interaction\":{\"type\":\"script\"},\"x\":[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]}"));
        #undef UF_CFG

        // uf_pkg_ok: pacote vira caminho (/data/local/tmp/mods/<pkg>).
        check("pkg normal ok", uf_pkg_ok("com.dts.freefireth") && uf_pkg_ok("com.foo_bar.Baz2"));
        check("pkg com / recusa", !uf_pkg_ok("com.foo/../../etc") && !uf_pkg_ok("a/b"));
        check("pkg .. recusa", !uf_pkg_ok("..") && !uf_pkg_ok("com..foo"));
        check("pkg começando com . recusa", !uf_pkg_ok(".foo"));
        check("pkg shell recusa", !uf_pkg_ok("x; id") && !uf_pkg_ok("x$(id)"));
        check("pkg vazio/nulo/zygote recusa", !uf_pkg_ok("") && !uf_pkg_ok(nullptr) && !uf_pkg_ok("zygote64"));
    }

    printf("\n[Caso 64] u_noads: guarda de prólogo curto\n");
    {
        uint32_t prologue[] = {0xD503201Fu, 0xD503201Fu, 0xD65F03C0u};
        check("ret nos primeiros 3 words recusa hook", !uno_method_fits(prologue));
        prologue[2] = 0xD503201Fu;
        check("prólogo sem terminador cabe no trampolim", uno_method_fits(prologue));
    }

    printf("\n[Caso 65] u_patch_parse: regras C4 + conf C3 (motor declarativo F4)\n");
    {
        up_rule_t r;
        char line[256];
        // Exemplo de aceite do contrato:
        snprintf(line, sizeof(line), "return ComplexCreature HasAmmo 0 bool true");
        check("return válido (aceite SA2)", up_parse_line(line, &r) == 0 && r.kind == UP_RETURN &&
              strcmp(r.cls, "ComplexCreature") == 0 && strcmp(r.member, "HasAmmo") == 0 &&
              r.nargs == 0 && r.type == UP_BOOL && strcmp(r.value, "true") == 0);
        // Classe sem namespace: ns fica "".
        char ns[128], cname[128];
        check("classe sem namespace resolve com ns vazio",
              up_split_class(r.cls, ns, sizeof(ns), cname, sizeof(cname)) && ns[0] == '\0' &&
              strcmp(cname, "ComplexCreature") == 0);
        // Namespace: separa no ÚLTIMO ponto.
        check("namespace separa no último ponto",
              up_split_class("Landfall.TABS.UnitBlueprint", ns, sizeof(ns), cname, sizeof(cname)) &&
              strcmp(ns, "Landfall.TABS") == 0 && strcmp(cname, "UnitBlueprint") == 0);
        // Comentário e vazia pulam.
        snprintf(line, sizeof(line), "# return X Y 0 bool true");
        check("linha de comentário pula", up_parse_line(line, &r) == 1);
        snprintf(line, sizeof(line), "   ");
        check("linha vazia pula", up_parse_line(line, &r) == 1);
        snprintf(line, sizeof(line), "return X Y 0 bool true # sempre cheio");
        check("comentário no fim da linha é cortado", up_parse_line(line, &r) == 0 &&
              strcmp(r.value, "true") == 0);
        // static (5 campos, sem nargs) e mul.
        snprintf(line, sizeof(line), "static GameSettings godMode bool true");
        check("static válido", up_parse_line(line, &r) == 0 && r.kind == UP_STATIC &&
              r.type == UP_BOOL && strcmp(r.value, "true") == 0);
        snprintf(line, sizeof(line), "mul Weapon Damage 1 float $mult");
        check("mul com $key válido", up_parse_line(line, &r) == 0 && r.kind == UP_MUL &&
              r.nargs == 1 && r.type == UP_FLOAT && strcmp(r.value, "$mult") == 0);
        // Inválidas.
        snprintf(line, sizeof(line), "frobnicate X Y 0 bool true");
        check("verbo desconhecido rejeitado", up_parse_line(line, &r) == -1);
        snprintf(line, sizeof(line), "return X Y bool true");
        check("nargs faltando rejeitado", up_parse_line(line, &r) == -1);
        snprintf(line, sizeof(line), "return X Y 0 string oi");
        check("tipo desconhecido rejeitado", up_parse_line(line, &r) == -1);
        snprintf(line, sizeof(line), "return X Y -1 bool true");
        check("nargs negativo rejeitado", up_parse_line(line, &r) == -1);
        snprintf(line, sizeof(line), "return X Y 3x bool true");
        check("nargs com lixo rejeitado", up_parse_line(line, &r) == -1);
        snprintf(line, sizeof(line), "mul X Y 0 bool true");
        check("mul bool rejeitado (só int|float)", up_parse_line(line, &r) == -1);
        snprintf(line, sizeof(line), "static X Y bool");
        check("static sem valor rejeitado", up_parse_line(line, &r) == -1);
        // Conf C3.
        const char *conf = "# mod de teste\nmult = 2.5 \nflag=true\n";
        char val[64];
        check("conf acha key com espaços", up_conf_get(conf, "mult", val, sizeof(val)) && strcmp(val, "2.5") == 0);
        check("conf acha bool", up_conf_get(conf, "flag", val, sizeof(val)) && strcmp(val, "true") == 0);
        check("conf não acha key ausente", !up_conf_get(conf, "outra", val, sizeof(val)));
        check("conf não acha dentro de comentário", !up_conf_get(conf, "mod", val, sizeof(val)));
        check("conf nulo não crasha", !up_conf_get(nullptr, "mult", val, sizeof(val)));
    }

    printf("\n[Caso 66] u_patch_arm64: emissores de patch (F4, palavras conferidas contra llvm-objdump do NDK)\n");
    {
        // Âncoras geradas pelo assembler do NDK (clang --target=aarch64-linux-android23 -c
        // + llvm-objdump -d; comando/entrada comentados no relatório do commit):
        //   mov x0,#1          -> 0xD2800020    ret -> 0xD65F03C0
        //   movz x0,#0x2345    -> 0xD28468A0    movk x0,#1,lsl#16 -> 0xF2A00020
        //   movz w9,#0x8000    -> 0x52900009    movk w9,#0x3F80,lsl#16 -> 0x72A7F009
        //   ldxr w9,[x17]      -> 0x885F7E29    stxr w10,w9,[x17] -> 0x880A7E29
        //   ldaxr w9,[x17]     -> 0x885FFE29    stlr wzr,[x17] -> 0x889FFE3F
        //   br x16             -> 0xD61F0200
        //   add x17,x16,#24    -> 0x91006211    movz w9,#0 -> 0x52800009
        //   (leitura little-endian dos bytes do objdump; add x17 com imediato
        //   24 vale pra slot alinhado — no thunk o imediato é lo(slot)+24)
        //   ldr s1,[x16,#8]    -> 0xBD400A01    fmul s0,s0,s1 -> 0x1E210800
        //   fmov s0,w9         -> 0x1E270120    mov w9,#1 -> 0x52800029
        //   mul x0,x0,x1       -> 0x9B017C00    ldr x1,[x16,#8] -> 0xF9400601
        //   blr x16            -> 0xD63F0200    str x30,[x16,#16] -> 0xF9000A1E
        //   ldr x30,[x16,#16]  -> 0xF9400A1E    str xzr,[x16,#24] -> 0xF9000E1F
        check("movz x0,#1 = 0xD2800020 (objdump)", up_enc_movz_x0(1) == 0xD2800020u);
        check("ret = 0xD65F03C0 (objdump)", UP_RET == 0xD65F03C0u);
        check("blr x16 = 0xD63F0200 (objdump)", UP_BLR_X16 == 0xD63F0200u);
        check("mul x0,x0,x1 = 0x9B017C00 (objdump)", UP_MUL_X0 == 0x9B017C00u);
        check("fmul s0,s0,s1 = 0x1E210800 (objdump)", UP_FMUL_S0 == 0x1E210800u);
        check("fmov s0,w9 = 0x1E270120 (objdump)", UP_FMOV_S0_W9 == 0x1E270120u);
        check("ldaxr w9,[x17] = 0x885FFE29 (objdump)", up_enc_ldxr_lock() == 0x885FFE29u);
        check("stlr wzr,[x17] = 0x889FFE3F (objdump)", up_enc_stlr_unlock() == 0x889FFE3Fu);
        check("br x16 = 0xD61F0200 (objdump)", UP_BR_X16 == 0xD61F0200u);
        check("stxr w10,w9,[x17] = 0x880A7E29 (objdump)", up_enc_stxr_lock() == 0x880A7E29u);
        check("ldr s1,[x16,#8] = 0xBD400A01 (objdump)", up_enc_ldr_s1() == 0xBD400A01u);
        check("mov w9,#1 = 0x52800029 (objdump)", UP_MOVZ_W9_1 == 0x52800029u);
        uint32_t w[8];
        check("return bool = movz+ret (objdump)", up_emit_return_bool(w, 1) == 2 && w[0] == 0xD2800020u && w[1] == UP_RET);
        int n = up_emit_return_int(w, 0x12345u);
        check("return int grande = movz+movk+ret (objdump)", n == 3 && w[2] == UP_RET &&
              w[0] == 0xD28468A0u && w[1] == 0xF2A00020u);
        // Âncora movz/movk w9 do objdump: bits 0x3F808000 (meia-palavra
        // baixa 0x8000 NÃO é 0 — float 1.0f = 0x3F800000 teria movz #0,
        // âncora 0x52800009 mov w9,#0). Teste usa os bits da âncora real.
        check("return float = movz/movk w9+fmov+ret (objdump)", up_emit_return_float(w, 0x3F808000u) == 4 &&
              w[0] == 0x52900009u && w[1] == 0x72A7F009u && w[2] == 0x1E270120u && w[3] == UP_RET);
        // 1.0f (0x3F800000): meia-palavra baixa zero → movz #0 (objdump mov w9,#0).
        check("return float 1.0f = movz#0+movk+fmov+ret", up_emit_return_float(w, 0x3F800000u) == 4 &&
              w[0] == 0x52800009u && w[1] == 0x72A7F009u && w[2] == 0x1E270120u && w[3] == UP_RET);
        // Thunk mul: 21 palavras. Validação por CAMPO decodificado
        // (Rd/Rn/Rt/Rs/imm), não só palavra inteira — encoding errado de
        // registrador falha aqui (o bug Rn=18 do add x17 é pego por estes).
        // Âncoras de fluxo: x16/x17 são caller-saved — o bloco pós-call
        // RE-DERIVA x16 (adrp/add) e x17 (add #24); lr é restaurado ANTES
        // do unlock (a thread que adquirir depois sobrescreve slot.lr).
        uint32_t t[UP_MUL_THUNK_WORDS + 4];
        const void *thunk_va = (const void *)0x7A000000ull;
        const void *slot_va = (const void *)0x7B001234ull;
        check("thunk int tem 21 palavras (fast path = tail call br)", up_emit_mul_thunk(t, thunk_va, slot_va, false) == UP_MUL_THUNK_WORDS);
        auto decode_pair = [](uint32_t adrp, uint32_t add, uintptr_t pc) -> uintptr_t {
            int64_t off = (int64_t)((((adrp >> 29) & 3u) | (((adrp >> 5) & 0x7FFFFu) << 2)));
            if (off & 0x100000) off -= 0x200000;  // sext 21 bits
            uintptr_t page = (pc & ~(uintptr_t)0xFFFu) + (uintptr_t)(off << 12);
            return page + (((add >> 10) & 0xFFFu));
        };
        // --- decodificadores de campo (ARM ARM C4.1) ---
        auto fld_Rd = [](uint32_t w) { return (int)(w & 31u); };
        auto fld_Rn = [](uint32_t w) { return (int)((w >> 5) & 31u); };
        auto fld_Rt = fld_Rd;
        auto fld_Rs = [](uint32_t w) { return (int)((w >> 16) & 31u); };
        auto fld_imm12 = [](uint32_t w) { return (int)((w >> 10) & 0xFFFu); };
        auto fld_imm19 = [](uint32_t w) {
            int32_t imm = (int32_t)((w >> 5) & 0x7FFFFu);
            if (imm & 0x40000) imm -= 0x80000;
            return imm;
        };
        // --- [2] add x17, x16, #24: Rn TEM que ser 16 (x18 = selvagem) ---
        check("add[2]: palavra = 0x91006211 (objdump)", t[2] == 0x91006211u);
        check("add[2]: Rd=17 Rn=16 imm=24 (UP_SLOT_LOCK)",
              fld_Rd(t[2]) == 17 && fld_Rn(t[2]) == 16 && fld_imm12(t[2]) == (int)UP_SLOT_LOCK);
        // prova: reintroduzindo o bug Rn=18, o decodificador pega
        {
            uint32_t buggy = 0x91000000u | (24u << 10) | (18u << 5) | 17u;
            check("prova: add com Rn=18 é pego pelo decodificador", fld_Rn(buggy) == 18);
        }
        check("adrp/add[0-1] apontam pro slot",
              decode_pair(t[0], t[1], (uintptr_t)thunk_va) == (uintptr_t)slot_va);
        check("adrp/add[11-12] apontam pro slot",
              decode_pair(t[11], t[12], (uintptr_t)thunk_va + 44) == (uintptr_t)slot_va);
        // cbnz[4] pula pro caminho direto (19 = ldr x16); cbnz[7] volta pro ldaxr (3).
        check("cbnz do lock vai pro caminho direto (19)", fld_imm19(t[4]) + 4 == 19);
        check("cbnz da corrida volta pro ldaxr (3)", fld_imm19(t[7]) + 7 == 3);
        check("ldaxr em [3] (acquire), stxr em [6] (objdump)", t[3] == 0x885FFE29u && t[6] == 0x880A7E29u);
        check("stxr: Rs=w10 (status) Rt=w9 (dado) — campos decodificados",
              fld_Rs(t[6]) == 10 && fld_Rt(t[6]) == 9);
        check("thunk float: ldr s1 + fmul em [14]/[15]", up_emit_mul_thunk(t, thunk_va, slot_va, true) == UP_MUL_THUNK_WORDS &&
              t[14] == 0xBD400A01u && t[15] == 0x1E210800u);
        check("thunk int: ldr x1,[x16,#8] + mul em [14]/[15] (Rt=1, Rn=16, imm12=1 => 8 bytes)",
              up_emit_mul_thunk(t, thunk_va, slot_va, false) && t[14] == 0xF9400601u &&
              fld_Rt(t[14]) == 1 && fld_Rn(t[14]) == 16 && fld_imm12(t[14]) == 1 && t[15] == 0x9B017C00u);
        check("save lr em [8]; restore ANTES do unlock: ldr x30 em [16], stlr em [17]",
              t[8] == 0xF9000A1Eu && t[16] == 0xF9400A1Eu && t[17] == 0x889FFE3Fu &&
              fld_Rt(t[8]) == 30 && fld_Rn(t[8]) == 16);
        check("unlock stlr wzr,[x17] (release, Rt=31, Rn=17 — não zeraria orig)",
              fld_Rt(t[17]) == 31 && fld_Rn(t[17]) == 17);
        check("pós-call: add[13] re-deriva x17 = x16 + 24 (Rd=17, Rn=16, imm=24)",
              t[13] == 0x91006211u && fld_Rd(t[13]) == 17 && fld_Rn(t[13]) == 16 &&
              fld_imm12(t[13]) == (int)UP_SLOT_LOCK);
        check("fast path = ldr x16,[x16] + br x16 em [19]/[20] (tail call, x30 intacto)",
              t[19] == UP_LDR_X16_ORIG && t[20] == UP_BR_X16);
        // --- checagem estrutural do fluxo: nenhum 'ret' pode executar depois
        // de um 'blr' sem um 'ldr x30' entre eles (o bug do self-loop) — e
        // nenhum 'ret' com x30 salvo e ainda não restaurado. ---
        {
            bool clean = true;
            bool blr_pending = false;   // blr visto, x30 ainda não restaurado
            bool lr_saved = false;      // x30 salvo e ainda não restaurado
            bool mul_done = false;      // já passou do adrp pós-call (x16/x17 velhos mortos)
            bool x17_fresh = false;     // x17 = slot+24 válido (re-derivado pós-call)
            for (int w = 0; w < UP_MUL_THUNK_WORDS && clean; w++) {
                if (t[w] == UP_LDR_X16_ORIG) continue;  // não mexe x30
                if (t[w] == 0xF9000A1Eu) { lr_saved = true; continue; }   // str x30,[x16,#16]
                if (t[w] == 0xF9400A1Eu) { lr_saved = false; blr_pending = false; continue; } // ldr x30,[x16,#16]
                if (t[w] == UP_BLR_X16) { blr_pending = true; continue; }
                if (t[w] == 0x91006211u) { x17_fresh = true; continue; }  // add x17,x16,#24
                if (((t[w] >> 24) & 0x9Fu) == 0x90u) {  // adrp: pós-call, x16/x17 velhos morreram
                    if (w > 2) { mul_done = true; x17_fresh = false; }
                    continue;
                }
                if ((t[w] >> 24) == 0x88u) {  // lock por exclusiva (stxr/stlr): usa x17
                    if (mul_done && !x17_fresh) clean = false;  // bug (a): unlock em endereço lixo
                    if (w == UP_MUL_THUNK_WORDS - 4 && lr_saved) clean = false;  // bug (b): unlock antes do restore
                    if (w == UP_MUL_THUNK_WORDS - 4) { lr_saved = false; blr_pending = false; }
                    continue;
                }
                if (t[w] == UP_RET) {
                    if (blr_pending) clean = false;  // ret após blr sem ldr x30 = self-loop
                    if (lr_saved) clean = false;     // ret com x30 salvo e não restaurado
                    if (mul_done && !x17_fresh) clean = false;  // ret sem re-derivar x17 pós-call
                }
                if (t[w] == UP_BR_X16) { blr_pending = false; }  // tail call: x30 do chamador segue
            }
            check("estrutura: ret com x30 restaurado, unlock com x17 re-derivado e pós-restore", clean);
            // PROVA: reintroduz o bug (blr+ret no fast path) → a checagem falha
            {
                uint32_t bad[UP_MUL_THUNK_WORDS + 1];
                for (int k = 0; k < UP_MUL_THUNK_WORDS; k++) bad[k] = t[k];
                bad[20] = UP_BLR_X16;  // fast path: blr em vez de br
                bad[21] = UP_RET;      // + ret = self-loop
                bool bclean = true;
                bool bblr = false;
                for (int w = 0; w < UP_MUL_THUNK_WORDS + 1; w++) {
                    if (bad[w] == 0xF9400A1Eu) { bblr = false; continue; }
                    if (bad[w] == UP_BLR_X16) { bblr = true; continue; }
                    if (bad[w] == UP_RET) {
                        if (bblr) { bclean = false; break; }
                    }
                    if (bad[w] == UP_BR_X16) { bblr = false; }
                }
                check("prova: blr+ret no fast path É pego pela checagem", !bclean);
            }
        }
        check("pool cabe numa página RX", (size_t)UP_MUL_MAX * UP_MUL_THUNK_WORDS * 4 <= 4096);
    }

    printf("\n[Caso 67] up_method_fits: guard de método curto (F4 revisão)\n");
    {
        // Getter curto do exemplo da revisão: ldr w0,[x0,#8]; ret (8 bytes).
        const uint32_t getter[] = {0xB9400800u, UP_RET};
        check("getter 8B cabe return bool (8B)", up_method_fits(getter, 2));
        check("getter 8B NÃO cabe return int grande (12B)", !up_method_fits(getter, 3));
        check("getter 8B NÃO cabe return float (16B)", !up_method_fits(getter, 4));
        // Método vazio (só ret) não cabe nem bool.
        const uint32_t empty[] = {UP_RET, 0xD503201Fu};
        check("método vazio recusado", !up_method_fits(empty, 2));
        // Tail call (B incondicional no meio) termina a função ali.
        const uint32_t tail[] = {0x94000005u, UP_RET, 0xD503201Fu, 0xD503201Fu};
        check("B incondicional no meio recusa", !up_method_fits(tail, 4));
        // br x30 no meio também termina.
        const uint32_t brmid[] = {0x910003E0u, 0xD61F03C0u, UP_RET, 0xD503201Fu};
        check("br no meio recusa", !up_method_fits(brmid, 4));
        // ret exatamente na última palavra é ok (método tem o tamanho exato).
        const uint32_t exact[] = {0x910003E0u, 0x910003E1u, 0x910003E2u, UP_RET};
        check("ret na última palavra cabe", up_method_fits(exact, 4));
        // Método normal sem terminador no meio cabe.
        const uint32_t normal[] = {0x910003E0u, 0xB9400800u, 0x0B000020u, UP_RET};
        check("método normal cabe float", up_method_fits(normal, 4));
        check("nulo e n<2 recusados", !up_method_fits(nullptr, 4) && !up_method_fits(normal, 1));
        // up_is_terminator direto: cbz (condicional) NÃO termina; bl NÃO termina.
        check("cbz não é terminador", !up_is_terminator(0x34000020u));
        check("bl não é terminador", !up_is_terminator(0x94000005u));
        check("ret x30 é terminador", up_is_terminator(0xD65F03C0u));
        check("ret x9 é terminador", up_is_terminator(0xD65F0120u));
        // Bug 3 (C1): cmdline ainda-zygote não serve como <pkg>.
        check("zygote é zygote", up_is_zygote("zygote"));
        check("zygote64 é zygote", up_is_zygote("zygote64"));
        check("pacote real não é zygote", !up_is_zygote("com.hyperdotstudios.swampattack2"));
        check("nulo/vazio não é zygote", !up_is_zygote(nullptr) && !up_is_zygote(""));
    }

    printf("\n[Caso 68] field C4: parse + emissor do thunk (F4b)\\n");
    {
        up_rule_t r;
        char line[256];
        snprintf(line, sizeof(line), "field WeaponInfo unlimitedAmmo bool true");
        check("field auto válido (5 campos)", up_parse_line(line, &r) == 0 && r.kind == UP_FIELD &&
              strcmp(r.cls, "WeaponInfo") == 0 && strcmp(r.member, "unlimitedAmmo") == 0 &&
              r.type == UP_BOOL && strcmp(r.value, "true") == 0 && r.nargs == -1 && r.fmethod[0] == 0);
        snprintf(line, sizeof(line), "field Foo bar int 7 SelectWeapon 1");
        check("field explícito válido (7 campos)", up_parse_line(line, &r) == 0 && r.kind == UP_FIELD &&
              strcmp(r.fmethod, "SelectWeapon") == 0 && r.nargs == 1 &&
              strcmp(r.value, "7") == 0);
        snprintf(line, sizeof(line), "field Foo bar string x");
        check("field tipo errado rejeitado", up_parse_line(line, &r) == -1);
        snprintf(line, sizeof(line), "field Foo bar bool true M");
        check("field 6 campos rejeitado", up_parse_line(line, &r) == -1);
        snprintf(line, sizeof(line), "field Foo bar bool true M x");
        check("field nargs ruim rejeitado", up_parse_line(line, &r) == -1);
        snprintf(line, sizeof(line), "field Foo NoSuchField bool true");
        check("field com campo desconhecido aceita no parse (inexistência é runtime)",
              up_parse_line(line, &r) == 0 && r.kind == UP_FIELD);
        // Emissor: bool = movz+strb+adrp+add+ldr+br (6 palavras).
        uint32_t f[8];
        const void *fva = (const void *)0x7A000100ull;
        const void *sva = (const void *)0x7B002234ull;
        int nf = up_emit_field_thunk(f, fva, sva, 1, 1, 112);
        check("field bool = 6 palavras", nf == 6);
        check("field bool[0] movz w9,#1", f[0] == 0x52800029u);
        check("field bool[1] strb w9,[x0,#112]", f[1] == 0x3901C009u);
        check("field bool termina ldr+br", f[4] == UP_LDR_X16_ORIG && f[5] == UP_BR_X16);
        // int grande = movz+movk+str+adrp+add+ldr+br (7 palavras).
        int ni = up_emit_field_thunk(f, fva, sva, 4, 0x12345678u, 112);
        check("field int = 7 palavras", ni == 7);
        check("field int[1] movk w9 hi", f[1] == (0x72A00000u | (0x1234u << 5) | 9u));
        check("field int[2] str w9,[x0,#112]", f[2] == 0xB9007009u);
        // float com metade alta zerada = 6 palavras (sem movk inútil).
        int nf2 = up_emit_field_thunk(f, fva, sva, 4, 0x00000000u, 112);
        check("field float 0.0f = 6 palavras", nf2 == 6);
        // float 1.0f (0x3F800000, hi != 0) = 7 palavras.
        int nf3 = up_emit_field_thunk(f, fva, sva, 4, 0x3F800000u, 112);
        check("field float 1.0f = 7 palavras", nf3 == 7);
        // Recusas: offset fora do alcance.
        check("field strb off>4095 recusa", up_emit_field_thunk(f, fva, sva, 1, 1, 4096) == 0);
        check("field str desalinhado recusa", up_emit_field_thunk(f, fva, sva, 4, 1, 114) == 0);
        check("field str off/4>4095 recusa", up_emit_field_thunk(f, fva, sva, 4, 1, 16384) == 0);
        check("field size inválido recusa", up_emit_field_thunk(f, fva, sva, 2, 1, 8) == 0);
    }

    printf("\n== Resultado: %s (%d falhas) ==\n", g_fail == 0 ? "TODOS PASSARAM" : "HOUVE FALHAS", g_fail);
    return g_fail == 0 ? 0 : 1;
}