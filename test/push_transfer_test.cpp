// test/push_transfer_test.cpp — o transporte do push_mod no socket de
// VERDADE: exato → ok, excesso → recusa, a menos → recusa, sem half-close →
// recusa. Nada pendura (alarm + SO_RCVTIMEO curto no socket).
//
// O bc_push_recv_exact é o MESMO código que o companion (root) usa no
// handle_push_mod — o transporte não pode divergir entre teste e produção.

#include <errno.h>
#include <fcntl.h>
#include <cstdlib>
#include <signal.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

#include "../jni/bc_push_io.h"

static int g_fail = 0;
static void check(const char *name, bool ok) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) g_fail++;
}

static void on_alarm(int) {
    static const char msg[] = "  [FAIL] ALARME: o transporte pendurou\n";
    ssize_t r = write(2, msg, sizeof(msg) - 1);
    (void)r;
    _exit(3);
}

// server side: chama o bc_push_recv_exact com SO_RCVTIMEO curto (como o
// accept loop do companion arma 3s; aqui 200ms bastam).
struct arg_t { int sock; int out; long size; int rc; char why[256]; };

static void *server_fn(void *a) {
    struct arg_t *t = (struct arg_t *)a;
    struct timeval tv = { .tv_sec = 0, .tv_usec = 200 * 1000 };
    setsockopt(t->sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    t->rc = bc_push_recv_exact(t->sock, t->out, t->size, t->why, sizeof(t->why));
    return nullptr;
}

static bool roda_caso(long size, long envia, bool fecha, const char **want_sub,
                      int *rc_out, char *why_out) {
    char tmpl[] = "/tmp/push_transfer_test.XXXXXX";
    int out = mkstemp(tmpl);
    if (out < 0) return false;
    unlink(tmpl);

    int sp[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) != 0) { close(out); return false; }

    struct arg_t t = { sp[0], out, size, 1, {0} };
    pthread_t th;
    if (pthread_create(&th, nullptr, server_fn, &t) != 0) { close(out); close(sp[0]); close(sp[1]); return false; }

    // cliente: envia `envia` bytes do payload (padrão 0xAB) e (talvez) fecha
    static char payload[64 * 1024];
    memset(payload, 0xAB, sizeof(payload));
    long left = envia > 0 ? envia : 0;
    while (left > 0) {
        ssize_t w = write(sp[1], payload, left > 4096 ? 4096 : left);
        if (w <= 0) break;
        left -= w;
    }
    if (fecha) shutdown(sp[1], SHUT_WR);

    pthread_join(th, nullptr);
    close(sp[1]);
    close(sp[0]);
    close(out);
    *rc_out = t.rc;
    snprintf(why_out, 256, "%s", t.why);
    if (want_sub != nullptr) {
        // caso de RECUSA: rc == -1 e o motivo diz o porquê
        return t.rc == -1 && strstr(t.why, *want_sub) != nullptr;
    }
    return t.rc == 0;
}

int main() {
    signal(SIGALRM, on_alarm);
    alarm(5);
    printf("== push_transfer: exato | excesso | a menos | sem half-close ==\n");

    int rc;
    char why[256];

    // 1. EXATO + half-close: ok, e os bytes chegaram ao arquivo.
    {
        char tmpl[] = "/tmp/push_transfer_out.XXXXXX";
        int out = mkstemp(tmpl);
        unlink(tmpl);
        int sp[2];
        socketpair(AF_UNIX, SOCK_STREAM, 0, sp);
        struct arg_t t = { sp[0], out, 1000, 1, {0} };
        pthread_t th;
        pthread_create(&th, nullptr, server_fn, &t);
        static char payload[1000];
        memset(payload, 0xCD, sizeof(payload));
        long left = 1000;
        while (left > 0) {
            ssize_t w = write(sp[1], payload, left);
            if (w <= 0) break;
            left -= w;
        }
        shutdown(sp[1], SHUT_WR);
        pthread_join(th, nullptr);
        close(sp[1]); close(sp[0]);
        lseek(out, 0, SEEK_SET);
        char got[1000] = {};
        ssize_t n = read(out, got, sizeof(got));
        bool ok = t.rc == 0 && n == 1000 && memcmp(got, payload, 1000) == 0;
        check("exato + half-close: ok, 1000 bytes no arquivo", ok);
        close(out);
    }

    // 2. EXCESSO + half-close: recusa, e o motivo diz o excesso.
    {
        bool ok = roda_caso(1000, 1000 + 32, true,
                            (const char *[]){"ALEM do anunciado"}, &rc, why);
        check("excesso (+32 bytes): recusado com o excesso no motivo", ok);
        if (!ok) printf("    (rc=%d why=%s)\n", rc, why);
    }

    // 3. A MENOS + half-close: recusa sem travar.
    {
        bool ok = roda_caso(1000, 900, true,
                            (const char *[]){"transfer incomplete"}, &rc, why);
        check("a menos (-100 bytes): recusado, sem travar", ok);
        if (!ok) printf("    (rc=%d why=%s)\n", rc, why);
    }

    // 4. EXATO SEM half-close: recusa pelo SO_RCVTIMEO (200ms), não espera
    //    para sempre — cliente velho ganha erro acionável.
    {
        bool ok = roda_caso(1000, 1000, false,
                            (const char *[]){"nao fechou o lado de escrita"}, &rc, why);
        check("sem half-close: recusado em <=200ms (contrato do emissor)", ok);
        if (!ok) printf("    (rc=%d why=%s)\n", rc, why);
    }

    alarm(0);
    printf("== Resultado: %s (%d falhas) ==\n",
           g_fail == 0 ? "TODOS PASSARAM" : "HOUVE FALHAS", g_fail);
    return g_fail == 0 ? 0 : 1;
}
