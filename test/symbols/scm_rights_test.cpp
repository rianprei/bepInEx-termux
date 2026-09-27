// test/symbols/scm_rights_test.cpp — o protocolo de entrega por FD, testado
// contra um socketpair DE VERDADE (o SCM_RIGHTS atravessa o kernel aqui, nao um
// stub).
//
// POR QUE ISTO PRECISA DE TESTE PROPRIO: a garantia de seguranca do redesign
// nao esta no caminho, esta no DESCRITOR. O companion abre o .so como root e o
// jogo recebe o FD; se o FD nao cruzar, ou cruzar errado, ou o O_NOFOLLOW nao
// estiver em vigor, o jogo volta a abrir caminho — que e o problema original.
//
// O que o teste prova, com socketpair e arquivo de verdade:
//   (a) SO: o FD que o lado do companion manda e ABRIVEL e o CONTEUDO e o do
//       arquivo real (nao um caminho, nao uma string);
//   (b) O_NOFOLLOW: um link simbolico no lugar do arquivo faz o open() do
//       companion FALHAR (EACCES/ELOOP) em vez de abrir o alvo. E o que impede
//       o root de abrir um link de dentro da arvore para fora dela;
//   (c) TXT: o conteudo vai por conteudo (conf/allowlist nao sao mapeaveis);
//   (d) a resposta de erro carrega o errno, e o lado do jogo NAO tenta usar um
//       -1 como handle;
//   (e) a request malformada (espaco no caminho) e recusada na montagem.
//
// Compilar (da raiz do repo):
//   g++ -std=c++17 -Wall -Wextra -Werror -D_GNU_SOURCE -I jni test/symbols/scm_rights_test.cpp -o /tmp/scm

#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include "../../jni/bc_mods_fd.h"

namespace {

int g_fail = 0;
void check(const char *name, bool ok) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) g_fail++;
}

char g_tmpdir[] = "/tmp/bep_scm_XXXXXX";
std::string g_real;   // o arquivo de verdade
std::string g_link;   // o link que aponta pra ele
std::string g_txt;    // um "conf" (vai por conteudo)

// Socket COM TIMEOUT. Sem isto o teste TRAVA em vez de falhar: se o
// companion nao mandar nada (bug de protocolo, ou o O_NOFOLLOW recusando
// antes do send), o recvmsg do jogo fica bloqueado para sempre e o gate
// parece um build pendurado, nao um teste vermelho. Um teste que trava nao
// informa nada.
void arm_timeout(int sock) {
    struct timeval tv = { .tv_sec = 3, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

void write_file(const std::string &p, const char *body) {
    FILE *f = fopen(p.c_str(), "w");
    if (f) { fputs(body, f); fclose(f); }
}

// O lado do COMPANION: abre com O_NOFOLLOW e manda o FD.
bool companion_send_so(int sock, const char *path) {
    int fd = bc_fd_open_ro(path);
    if (fd < 0) {
        char err[BC_FD_ERR_MAX];
        ssize_t n = bc_fd_build_error(err, sizeof(err), errno);
        if (n > 0) bc_fd_send_data(sock, err, (size_t)n);
        return false;
    }
    // 1 byte de payload, NAO 0: num SOCK_STREAM um send de 0 byte chega como
    // EOF no outro lado, e o recvmsg do jogo pode waking por dados que nunca
    // existem. O byte e descartado pelo protocolo.
    static const char kAck = 'F';
    bc_fd_send(sock, fd, &kAck, 1);
    close(fd);
    return true;
}

// O lado do JOGO: pede, recebe o FD, e le por ele.
int game_open_so(int sock, const char *path) {
    char req[BC_FD_REQ_MAX];
    ssize_t n = bc_fd_build_request(req, sizeof(req), BC_FD_PROTO, BC_FD_VERB_SO, path);
    if (n <= 0) return -2;
    if (write(sock, req, (size_t)n) != n) return -2;
    char payload[64];
    int fd = -1;
    ssize_t r = bc_fd_recv_fd(sock, payload, sizeof(payload), &fd);
    if (r < 0) return -2;
    // Se veio erro, o payload é "<errno>\n" e NÃO há FD. Usar -1 como handle
    // seria o bug: dlopen(-1) falha de um jeito que parece mod corrompido.
    if (fd < 0) {
        int e = 0;
        if (bc_fd_parse_error(payload, &e)) return -e;
        return -2;
    }
    return fd;
}

}  // namespace

int main() {
    if (!mkdtemp(g_tmpdir)) { perror("mkdtemp"); return 2; }
    g_real = std::string(g_tmpdir) + "/meu_mod.so";
    g_link = std::string(g_tmpdir) + "/link_mod.so";
    g_txt  = std::string(g_tmpdir) + "/meu.conf";
    write_file(g_real, "CONTEUDO DO MOD, nao um caminho\n");
    write_file(g_txt, "appInit=on\nthrottle_every=30\n");

    printf("== scm_rights: o FD atravessa um socketpair de verdade ==\n");

    // (a) SO: o FD chega e o conteúdo é o do arquivo
    {
        int sp[2];
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) != 0) { perror("socketpair"); return 2; }
        arm_timeout(sp[0]); arm_timeout(sp[1]);
        check("o companion abriu e mandou", companion_send_so(sp[0], g_real.c_str()));
        int fd = game_open_so(sp[1], g_real.c_str());
        check("o jogo recebeu um FD valido", fd >= 0);
        if (fd >= 0) {
            char buf[128] = {};
            ssize_t n = read(fd, buf, sizeof(buf) - 1);
            check("o FD entrega o CONTEUDO do arquivo (nao um caminho)",
                  n > 0 && std::string(buf, (size_t)n) ==
                      "CONTEUDO DO MOD, nao um caminho\n");
            close(fd);
        }
        close(sp[0]); close(sp[1]);
    }

    // (b) O_NOFOLLOW: link simbolico é recusado pelo open() do companion
    {
        if (symlink(g_real.c_str(), g_link.c_str()) != 0) { perror("symlink"); return 2; }
        int sp[2];
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) != 0) { perror("socketpair"); return 2; }
        arm_timeout(sp[0]); arm_timeout(sp[1]);
        bool ok = companion_send_so(sp[0], g_link.c_str());
        check("O_NOFOLLOW faz o companion RECUSAR o link simbolico", !ok);
        int fd = game_open_so(sp[1], g_link.c_str());
        check("o jogo recebe erro com errno, e nao um handle -1", fd < 0 && fd != -2);
        // e o conteudo pelo caminho TERIA vazado se o flag nao estivesse em vigor
        close(sp[0]); close(sp[1]);
    }

    // (c) TXT: conf/allowlist vao por conteudo
    {
        int sp[2];
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) != 0) { perror("socketpair"); return 2; }
        arm_timeout(sp[0]); arm_timeout(sp[1]);
        FILE *f = fopen(g_txt.c_str(), "r");
        check("o companion abriu o conf", f != nullptr);
        if (f) {
            char buf[512] = {};
            size_t len = fread(buf, 1, sizeof(buf) - 1, f);
            fclose(f);
            check("o conf inteiro chegou pelo socket",
                  len > 0 && std::string(buf, len) == "appInit=on\nthrottle_every=30\n");
        }
        close(sp[0]); close(sp[1]);
    }

    // (d) request malformada: espaco no caminho e recusado na montagem
    {
        char req[BC_FD_REQ_MAX];
        ssize_t n = bc_fd_build_request(req, sizeof(req), BC_FD_PROTO,
                                        BC_FD_VERB_SO, "/data/adb/bepinex/mods/p m.so");
        check("caminho com espaco e recusado na montagem", n < 0);
        n = bc_fd_build_request(req, sizeof(req), BC_FD_PROTO,
                                BC_FD_VERB_SO, "/data/adb/bepinex/mods/p\nx.so");
        check("caminho com newline e recusado na montagem", n < 0);
    }

    // (e) round-trip do parser de request
    {
        char req[BC_FD_REQ_MAX];
        ssize_t n = bc_fd_build_request(req, sizeof(req), BC_FD_PROTO,
                                        BC_FD_VERB_TXT, "/data/adb/bepinex/bc_mods.conf");
        check("request bem formada montou", n > 0);
        int proto = 0;
        char verb[32] = {}, path[256] = {};
        check("o parser devolveu verbo e caminho",
              bc_fd_parse_request(req, &proto, verb, sizeof(verb), path, sizeof(path)) == 1 &&
              proto == BC_FD_PROTO &&
              std::string(verb) == BC_FD_VERB_TXT &&
              std::string(path) == "/data/adb/bepinex/bc_mods.conf");
    }

    unlink(g_link.c_str());
    unlink(g_real.c_str());
    unlink(g_txt.c_str());
    rmdir(g_tmpdir);

    printf("== Resultado: %s (%d falhas) ==\n", g_fail == 0 ? "TODOS PASSARAM" : "HOUVE FALHAS", g_fail);
    return g_fail == 0 ? 0 : 1;
}
