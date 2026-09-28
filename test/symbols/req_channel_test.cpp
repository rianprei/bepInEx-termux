// test/symbols/req_channel_test.cpp — PONTA A PONTA: o cliente REAL de um lado,
// o despachante REAL do outro, num socketpair de verdade.
//
// ============================================================================
// POR QUE ESTE TESTE EXISTE (achado BLOQUEANTE do OpenCode em c242d5f)
// ============================================================================
// Os testes anteriores mediam as PECAS: o cliente montava a request certo, os
// validadores recusavam certo, o SO_PEERCRED mapeava certo. E nada disso
// importava, porque o FIO nao existia:
//
//   - o jogo mandava mod_fd/mod_txt/mod_list pelo g_stream_fd (STREAMING);
//   - o unico leitor desse socket, stream_socket_reader, faz BROADCAST;
//   - os handlers so eram alcancados pelo termux_accept_loop, que o JOGO nunca
//     conecta.
//
// Em runtime: todo pedido dava timeout de 5s e NENHUM mod carregava — e a
// suite inteira passava. Um teste que so verifica pecas nao verifica o sistema.
//
// Aqui o socket e o mesmo dos dois lados: o cliente REAL (bc_fd_build_request2 +
// o parse da resposta, os mesmos do jogo) e o DESPACHANTE REAL, que e o nucleo
// puro de bc_req_dispatch.h — o mesmo que o companion.cpp chama. O FD volta e
// ABRE o .so.
//
// O alarm(2) e o ponto: se o despacho sumir, o pedido fica sem resposta e o
// teste FALHA em 2s em vez de virar um build pendurado — que seria o mesmo modo
// de falha que a revisao achou.
//
// Compilar: g++ -std=c++17 -Wall -Wextra -Werror -D_GNU_SOURCE -I jni
//   test/symbols/req_channel_test.cpp -o /tmp/req_channel_test

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "../../jni/bc_mods_fd.h"
#include "../../jni/bc_req_channel.h"
#include "../../jni/bc_req_dispatch.h"

namespace {

int g_fail = 0;
void check(const char *name, bool ok) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) g_fail++;
}

void on_alarm(int) {
    static const char msg[] =
        "  [FAIL] ALARME: o pedido ficou pendurado — o despacho nao respondeu\n";
    ssize_t r = write(2, msg, sizeof(msg) - 1);
    (void)r;
    _exit(3);
}

const char *g_root = nullptr;

// Handlers de teste, com o MESMO contrato dos de root. O despacho e o de
// producao; so a ponta "root" e substituida.
void t_so(int fd, const char *pkg, const char *name) {
    char path[4096];
    snprintf(path, sizeof(path), "%.900s/%.400s/%.400s", g_root, pkg, name);
    int f = open(path, O_RDONLY);
    if (f < 0) { bc_fd_send_data(fd, "0\n", 2); return; }
    static const char kAck = 'F';
    bc_fd_send(fd, f, &kAck, 1);
    close(f);
}

void t_txt(int fd, const char *pkg, const char *name) {
    char path[4096];
    snprintf(path, sizeof(path), "%.900s/%.400s/%.400s", g_root, pkg, name);
    FILE *f = fopen(path, "rb");
    if (f == nullptr) { bc_fd_send_data(fd, "0\n", 2); return; }
    char buf[512];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    char head[32];
    int hn = snprintf(head, sizeof(head), "%zu\n", n);
    bc_fd_send_data(fd, head, (size_t)hn);
    bc_fd_send_data(fd, buf, n);
}

void t_list(int fd, const char *pkg) {
    (void)pkg;
    bc_fd_send_data(fd, "1\nmod.so\n", 10);
}

struct serve_arg { int fd; const struct bc_req_handlers *h; };

void *serve_loop(void *arg) {
    struct serve_arg *a = (struct serve_arg *)arg;
    for (;;) {
        if (bc_req_dispatch_one(a->fd, a->h) <= 0) break;
    }
    return nullptr;
}

// cliente: monta a request com o MESMO codigo do jogo e le a resposta
bool pedir(int sock, const char *verb, const char *a, const char *b,
           int *out_fd, std::string *out_txt) {
    char req[BC_FD_REQ_MAX];
    ssize_t n = (b != nullptr)
                    ? bc_fd_build_request2(req, sizeof(req), BC_FD_PROTO, verb, a, b)
                    : bc_fd_build_request(req, sizeof(req), BC_FD_PROTO, verb, a);
    if (n <= 0) return false;
    if (write(sock, req, (size_t)n) != n) return false;
    if (out_fd != NULL) {
        int fd = -1;
        char payload[64];
        if (bc_fd_recv_fd(sock, payload, sizeof(payload), &fd) < 0) return false;
        if (fd < 0) return false;
        *out_fd = fd;
        return true;
    }
    char payload[4096];
    ssize_t r = read(sock, payload, sizeof(payload) - 1);
    if (r <= 0) return false;
    payload[r] = '\0';
    char *nl = strchr(payload, '\n');
    if (nl == NULL) return false;
    *nl = '\0';
    if (out_txt != NULL) *out_txt = std::string(nl + 1);
    return true;
}

}  // namespace

int main() {
    signal(SIGALRM, on_alarm);
    alarm(2);

    printf("== req_channel: cliente REAL <-> despacho REAL ==\n");

    g_root = getenv("BC_TEST_MODS_ROOT");
    if (g_root == NULL) {
        printf("  [FAIL] BC_TEST_MODS_ROOT ausente\n");
        return 1;
    }

    // arvore de mods de mentira, com um .so de verdade para abrir
    char pkgdir[4096], sopath[4096], confpath[4096];
    snprintf(pkgdir, sizeof(pkgdir), "%.3900s/pkg.teste", g_root);
    if (mkdir(pkgdir, 0755) != 0 && errno != EEXIST) { perror("mkdir"); return 1; }
    snprintf(sopath, sizeof(sopath), "%.3800s/mod.so", pkgdir);
    snprintf(confpath, sizeof(confpath), "%.3800s/mod.conf", pkgdir);
    FILE *f = fopen(sopath, "wb");
    if (f == NULL) { perror("criar .so"); return 1; }
    fputs("\177ELF\002\001\001\000conteudo-do-mod-para-o-teste", f);
    fclose(f);
    f = fopen(confpath, "wb");
    if (f != NULL) { fputs("appInit=on\n", f); fclose(f); }

    int sp[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) != 0) { perror("socketpair"); return 1; }
    if (write(sp[1], BC_REQ_HELLO, strlen(BC_REQ_HELLO)) < 0) return 1;

    const struct bc_req_handlers h = {t_so, t_txt, t_list};
    struct serve_arg sa = {sp[1], &h};
    pthread_t srv;
    if (pthread_create(&srv, NULL, serve_loop, &sa) != 0) { perror("pthread"); return 1; }

    // 1. mod_fd: o FD volta e ABRE o .so
    {
        int fd = -1;
        bool ok = pedir(sp[0], BC_FD_VERB_SO, "pkg.teste", "mod.so", &fd, NULL);
        check("mod_fd: o despacho respondeu com um FD", ok);
        if (ok) {
            char buf[64] = {};
            ssize_t n = read(fd, buf, sizeof(buf) - 1);
            check("mod_fd: o FD ABRE o .so (conteudo certo)",
                  n > 0 && strstr(buf, "conteudo-do-mod-para-o-teste") != NULL);
            close(fd);
        }
    }

    // 2. mod_txt: o conteudo volta
    {
        std::string txt;
        bool ok = pedir(sp[0], "TX", "pkg.teste", "mod.conf", NULL, &txt);
        check("mod_txt: o despacho devolveu conteudo", ok);
        check("mod_txt: o conteudo e o do .conf", txt.find("appInit=on") != std::string::npos);
    }

    // 3. mod_list
    {
        std::string txt;
        bool ok = pedir(sp[0], BC_FD_VERB_LS, "pkg.teste", NULL, NULL, &txt);
        check("mod_list: o despacho respondeu", ok);
        check("mod_list: o .so da arvore aparece na lista", txt.find("mod.so") != std::string::npos);
    }

    // 4. verbo desconhecido: resposta curta, sem travar
    {
        std::string txt;
        bool ok = pedir(sp[0], "ZZZ", "pkg.teste", NULL, NULL, &txt);
        check("verbo desconhecido: respondeu sem travar", ok);
    }

    close(sp[0]);
    close(sp[1]);
    pthread_join(srv, NULL);
    alarm(0);
    unlink(sopath);
    unlink(confpath);
    rmdir(pkgdir);

    printf("== Resultado: %s (%d falhas) ==\n",
           g_fail == 0 ? "TODOS PASSARAM" : "HOUVE FALHAS", g_fail);
    return g_fail == 0 ? 0 : 1;
}
