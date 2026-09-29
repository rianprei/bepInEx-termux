// test/req_session_test.cpp — o canal REQ como recurso limitado (P1 da
// pré-revisão), com as PROVAS do adendo:
//
//   T-P1a  teto GLOBAL com uids DISTINTOS: a 17ª conexão (16 = teto) é
//          recusada SEM criar thread — o spawn conta, e quem confere é o
//          que o SERVIDOR respondeu (E<EMFILE>), não o que o cliente deixou
//          de ver.
//   T-P1b  userId != 0 recusado NA ABERTURA: uid 1010123 (usuário 10) de
//          pacote SERVÁVEL leva E<EPERM> no gate de abertura.
//   T-P1c  o contador volta a 0 em cada CICLO DE VIDA enumerado: sucesso,
//          recusa não-servível, EOF no meio da linha, verbo desconhecido,
//          pthread_create FALHANDO (o slot volta ANTES do close) e o par
//          aceito+recusado no topo.
//
// Tudo com a camada pura (bc_req_session.h) — a MESMA que o companion
// chama com SO_PEERCRED/packages.list de verdade — e as pontas INJETADAS:
// uid e pacotes de fixture, spawn contado (e sabotável de propósito).

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

#include "../jni/bc_mods_fd.h"
#include "../jni/bc_req_channel.h"
#include "../jni/bc_req_dispatch.h"
#include "../jni/bc_req_session.h"

static int g_fail = 0;
static void check(const char *name, bool ok) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) g_fail++;
}

static void on_alarm(int) {
    static const char msg[] = "  [FAIL] ALARME: pendurou (sessão/thread não saiu)\n";
    ssize_t r = write(2, msg, sizeof(msg) - 1);
    (void)r;
    _exit(3);
}

// ---- pontas injetáveis -------------------------------------------------------
// Modo do spawn: REAL (pthread de verdade, contando), FAIL (conta e recusa —
// a injeção da falha do pthread_create por costura de teste).
static int g_spawn_count = 0;
static int g_spawn_mode = 0;  // 0 = REAL, 1 = FAIL

static int fake_spawn(void *(*body)(void *), void *arg) {
    g_spawn_count++;
    if (g_spawn_mode == 1) return -1;
    pthread_t t;
    if (pthread_create(&t, nullptr, body, arg) != 0) return -1;
    pthread_detach(t);
    return 0;
}

// uid do peer e pacotes: globais de fixture (o driver é sequencial)
static uid_t g_peer_uid = 10000;
static const char *g_peer_pkgs[4] = {"com.jogo.servivel", nullptr, nullptr, nullptr};
static bool g_pkg_dir = true;      // pacote servível por padrão (pasta própria)
static bool g_pkg_allow = false;

static int fake_peer_uid(int fd, uid_t *out) {
    (void)fd;
    *out = g_peer_uid;
    return 0;
}

static int fake_resolve_pkgs(int fd, uid_t uid, char pkgs[][BC_REQ_SESSION_PKG_CAP], int max) {
    (void)fd; (void)uid;
    int n = 0;
    for (int i = 0; i < 4 && g_peer_pkgs[i] != nullptr && n < max; i++) {
        snprintf(pkgs[n], BC_REQ_SESSION_PKG_CAP, "%s", g_peer_pkgs[i]);
        n++;
    }
    return n;
}

static bool fake_has_dir(const char *pkg) {
    (void)pkg;
    return g_pkg_dir;
}

static bool fake_in_allow(const char *pkg) {
    (void)pkg;
    return g_pkg_allow;
}

// handler de fixture: lista vazia ("0\n") — só para o ciclo de vida ter tráfego
static void t_empty_list(int fd) {
    bc_fd_send_data(fd, "0\n", 2);
}
static void t_noop_so(int fd, const char *a, const char *b) { (void)a; (void)b; (void)fd; }
static void t_noop_txt(int fd, const char *a, const char *b) { (void)a; (void)b; (void)fd; }
static void t_noop_list(int fd, const char *a) { (void)a; (void)fd; }
static void t_noop_bc_so(int fd, const char *a) { (void)a; (void)fd; }
static void t_noop_bc_conf(int fd, const char *a) { (void)a; (void)fd; }

static const struct bc_req_handlers fixture_handlers = {
    t_noop_so, t_noop_txt, t_noop_list, t_noop_bc_so, t_empty_list, t_noop_bc_conf,
};

static struct bc_req_server_ops make_ops(void) {
    struct bc_req_server_ops ops;
    ops.peer_uid = fake_peer_uid;
    ops.resolve_pkgs = fake_resolve_pkgs;
    ops.pkg_has_dir = fake_has_dir;
    ops.pkg_in_allowlist = fake_in_allow;
    ops.handlers = &fixture_handlers;
    ops.spawn = fake_spawn;
    return ops;
}

static struct bc_req_slots g_slots;
static struct bc_req_slots *g_slots_current = &g_slots;

// abre UMA conexão de teste com o "REQ\n" e devolve o socket do cliente
static int open_req_channel(void) {
    int sp[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) != 0) return -1;
    if (write(sp[0], BC_REQ_HELLO, strlen(BC_REQ_HELLO)) !=
        (ssize_t)strlen(BC_REQ_HELLO)) {
        close(sp[0]); close(sp[1]);
        return -1;
    }
    struct bc_req_server_ops ops = make_ops();
    bc_req_serve_connection(sp[1], BC_REQ_HELLO, (ssize_t)strlen(BC_REQ_HELLO),
                            &ops, g_slots_current);
    return sp[0];  // se recusou, o serve já fechou o sp[1]; o cliente lê o E<errno>
}

// lê UMA linha do socket (o que o SERVIDOR respondeu)
static bool read_reply_line(int sock, char *out, size_t cap) {
    struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ssize_t n = bc_fd_read_line(sock, out, cap);
    return n > 0;
}

// espera active chegar a 0 (drain de thread)
static bool drain_to_zero(struct bc_req_slots *s, int timeout_ms) {
    for (int waited = 0; waited < timeout_ms; waited += 10) {
        if (bc_req_slot_active(s) == 0) return true;
        usleep(10 * 1000);
    }
    return bc_req_slot_active(s) == 0;
}

int main() {
    signal(SIGALRM, on_alarm);
    alarm(30);
    printf("== req_session: teto | gate de abertura | contador por saída ==\n");

    bc_req_slots_init(&g_slots);

    // ---- T-P1a: teto GLOBAL com uids DISTINTOS (16 ok, 17ª recusada) ------
    {
        int keep[BC_REQ_MAX_GLOBAL];
        int before = g_spawn_count;
        for (int i = 0; i < BC_REQ_MAX_GLOBAL; i++) {
            g_peer_uid = (uid_t)(20000 + i);  // distintos, todos user 0
            keep[i] = open_req_channel();
        }
        check("T-P1a: 16 canais de uids distintos abrem",
              g_spawn_count - before == BC_REQ_MAX_GLOBAL);

        int before17 = g_spawn_count;
        g_peer_uid = 39999;  // uid NOVO, abaixo do teto por-uid (1º dele)
        int refused = open_req_channel();
        char reply[64] = {0};
        bool got_reply = read_reply_line(refused, reply, sizeof(reply));
        check("T-P1a: 17ª conexão recusada SEM criar thread (spawn count igual)",
              g_spawn_count == before17);
        check("T-P1a: o servidor RESPONDEU E<EMFILE> na cara",
              got_reply && strcmp(reply, "E24") == 0);  // EMFILE=24
        close(refused);

        // fecha os 16 ANTES do teste por-uid: o 5º do mesmo uid tem que
        // colidir com o TETO POR-UID, não com o global ainda cheio (na ordem
        // errada o 5º media o errno errado — achado debugando o próprio teste).
        for (int i = 0; i < BC_REQ_MAX_GLOBAL; i++) close(keep[i]);
        check("T-P1a: 16 fechados, global drena a 0", drain_to_zero(&g_slots, 5000));

        // per-uid: o 5º canal do MESMO uid é recusado com E<EAGAIN>
        int same[BC_REQ_MAX_PER_UID];
        for (int i = 0; i < BC_REQ_MAX_PER_UID; i++) {
            g_peer_uid = 50000;
            same[i] = open_req_channel();
        }
        int before5 = g_spawn_count;
        g_peer_uid = 50000;
        int fifth = open_req_channel();
        got_reply = read_reply_line(fifth, reply, sizeof(reply));
        check("T-P1a: 5º canal do mesmo uid recusado sem thread",
              g_spawn_count == before5);
        check("T-P1a: ...com E<EAGAIN> respondido", got_reply && strcmp(reply, "E11") == 0);
        close(fifth);
        for (int i = 0; i < BC_REQ_MAX_PER_UID; i++) close(same[i]);
        check("T-P1a: contador global volta a 0 com tudo fechado",
              drain_to_zero(&g_slots, 5000));
    }

    // ---- T-P1b: userId != 0 recusado NA ABERTURA -----------------------------
    {
        bc_req_slots_init(&g_slots);
        g_peer_uid = 1010123;   // usuário 10, pacote SERVÁVEL
        g_pkg_dir = true;
        int before = g_spawn_count;
        int sock = open_req_channel();
        char reply[64] = {0};
        bool got_reply = read_reply_line(sock, reply, sizeof(reply));
        check("T-P1b: uid 1010123 (user 10) recusado na abertura, sem thread",
              g_spawn_count == before);
        check("T-P1b: ...com E<EPERM> respondido",
              got_reply && strcmp(reply, "E1") == 0);
        close(sock);
    }

    // ---- T-P1c: cada ciclo de vida devolve o slot (active == 0) --------------
    {
        bc_req_slots_init(&g_slots);
        g_peer_uid = 10000;
        g_pkg_dir = true;

        // 1. sucesso: BL + close
        int sock = open_req_channel();
        check("T-P1c: canal de sucesso abriu", sock >= 0 && bc_req_slot_active(&g_slots) == 1);
        close(sock);
        check("T-P1c(1) sucesso: slot devolvido", drain_to_zero(&g_slots, 5000));

        // 2. recusa não-servível: nunca adquire slot
        g_peer_pkgs[0] = "com.nada.aqui";
        g_pkg_dir = false; g_pkg_allow = false;
        sock = open_req_channel();
        char reply[64] = {0};
        bool got = read_reply_line(sock, reply, sizeof(reply));
        check("T-P1c(2) não-servível: recusado no topo (E<EACCES>)",
              got && strcmp(reply, "E13") == 0 && bc_req_slot_active(&g_slots) == 0);
        close(sock);
        g_peer_pkgs[0] = "com.jogo.servivel";
        g_pkg_dir = true;

        // 3. EOF no meio da linha
        sock = open_req_channel();
        write(sock, "par", 3);   // sem '\n'
        close(sock);             // EOF no meio: read_line ERRA (O3), thread sai
        check("T-P1c(3) EOF no meio da linha: slot devolvido",
              drain_to_zero(&g_slots, 5000));

        // 4. verbo desconhecido + close
        sock = open_req_channel();
        write(sock, "1 ZZZ pkg\n", 10);
        got = read_reply_line(sock, reply, sizeof(reply));
        int verr = 0;
        check("T-P1c(4) setup: verbo desconhecido respondeu errno (parse_error lê)",
              got && bc_fd_parse_error(reply, &verr) && verr == EINVAL);
        close(sock);
        check("T-P1c(4) verbo desconhecido: slot devolvido",
              drain_to_zero(&g_slots, 5000));

        // 5. pthread_create FALHANDO: o slot volta ANTES do close
        g_spawn_mode = 1;
        int before = g_spawn_count;
        sock = open_req_channel();
        check("T-P1c(5) spawn-fail: contado e recusado", g_spawn_count == before + 1);
        check("T-P1c(5) spawn-fail: slot devolvido (active==0 na hora)",
              bc_req_slot_active(&g_slots) == 0);
        close(sock);
        g_spawn_mode = 0;

        // 6. par aceito + recusado no topo: o aceito segura 1, o recusado não rouba
        g_peer_uid = 20000; g_pkg_dir = true;
        int alive = open_req_channel();
        check("T-P1c(6) setup: canal aceito ativo (active==1)",
              bc_req_slot_active(&g_slots) == 1);
        g_peer_pkgs[0] = "com.nada.aqui"; g_pkg_dir = false;
        int dead = open_req_channel();
        check("T-P1c(6) recusa no topo NÃO derruba o canal vivo",
              bc_req_slot_active(&g_slots) == 1);
        close(dead);
        close(alive);
        check("T-P1c(6) par aceito+recusado: tudo devolvido no fim",
              drain_to_zero(&g_slots, 5000));
        g_peer_pkgs[0] = "com.jogo.servivel"; g_pkg_dir = true;
    }

    alarm(0);
    printf("== Resultado: %s (%d falhas) ==\n",
           g_fail == 0 ? "TODOS PASSARAM" : "HOUVE FALHAS", g_fail);
    return g_fail == 0 ? 0 : 1;
}
