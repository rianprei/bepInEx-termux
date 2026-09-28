// test/bc_fd_harden_test.cpp — O1-O3 da pré-revisão: o fd e a linha do
// canal de pedidos não aceitam o que o protocolo não manda.
//
//   O1: bc_fd_open_ro só entrega arquivo REGULAR (diretório fecha e erra).
//   O2: bc_fd_recv_fd com 2+ SCM_RIGHTS fecha TODOS os extras e devolve
//       erro — nenhum fd vaza (contado em /proc/self/fd).
//   O3: bc_fd_read_line com linha maior que o buffer descarta até o '\n' e
//       ERRA; a linha VÁLIDA que vem depois é processada inteira. EOF no
//       meio da linha também é erro (linha truncada não é linha).

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

#include "../jni/bc_mods_fd.h"

static int g_fail = 0;
static void check(const char *name, bool ok) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) g_fail++;
}

static int count_open_fds(void) {
    DIR *d = opendir("/proc/self/fd");
    if (d == nullptr) return -1;
    int n = 0;
    struct dirent *de;
    while ((de = readdir(d)) != nullptr) {
        if (de->d_name[0] < '0' || de->d_name[0] > '9') continue;
        n++;
    }
    closedir(d);
    return n;  // inclui o fd do próprio opendir — constante entre medições
}

int main() {
    printf("== bc_fd_harden: O1 regular | O2 cmsg extra | O3 linha longa ==\n");

    // ---- O1: arquivo regular abre; diretório NÃO ---------------------------
    {
        char dir[] = "/tmp/bc_fd_harden_dir.XXXXXX";
        if (mkdtemp(dir) == nullptr) { perror("mkdtemp"); return 1; }
        char file[] = "/tmp/bc_fd_haren_file.XXXXXX";
        int f = mkstemp(file);
        if (f < 0) { perror("mkstemp"); return 1; }
        (void)write(f, "x", 1);
        close(f);

        int reg = bc_fd_open_ro(file);
        check("O1: arquivo regular abre", reg >= 0);
        if (reg >= 0) close(reg);
        int dirfd = bc_fd_open_ro(dir);
        check("O1: diretorio RECUSADO (errno EISDIR)",
              dirfd < 0 && errno == EISDIR);
        if (dirfd >= 0) close(dirfd);
        unlink(file);
        rmdir(dir);
    }

    // ---- O2: 2+ SCM_RIGHTS: fecha todos, erro, zero vazamento --------------
    {
        // sender manual com DOIS cmsgs no mesmo sendmsg
        int sp[2];
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) != 0) { perror("socketpair2"); return 1; }
        int fa = open("/dev/null", O_RDONLY);
        int fb = open("/dev/null", O_RDONLY);
        struct msghdr msg;
        memset(&msg, 0, sizeof(msg));
        char payload = 'X';
        struct iovec iov = { &payload, 1 };
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        char ctrl[CMSG_SPACE(sizeof(int)) * 2];
        msg.msg_control = ctrl;
        msg.msg_controllen = sizeof(ctrl);
        struct cmsghdr *c1 = CMSG_FIRSTHDR(&msg);
        c1->cmsg_level = SOL_SOCKET;
        c1->cmsg_type = SCM_RIGHTS;
        c1->cmsg_len = CMSG_LEN(sizeof(int));
        memcpy(CMSG_DATA(c1), &fa, sizeof(int));
        struct cmsghdr *c2 = CMSG_NXTHDR(&msg, c1);
        c2->cmsg_level = SOL_SOCKET;
        c2->cmsg_type = SCM_RIGHTS;
        c2->cmsg_len = CMSG_LEN(sizeof(int));
        memcpy(CMSG_DATA(c2), &fb, sizeof(int));
        msg.msg_controllen = CMSG_SPACE(sizeof(int)) * 2;  // os DOIS cmsgs inteiros
        ssize_t sent = sendmsg(sp[1], &msg, 0);
        check("O2 setup: sendmsg com 2 cmsgs saiu", sent == 1);

        int before = count_open_fds();   // fa/fb já abertos: entram nas DUAS contagens
        int got = -1;
        char buf[8];
        ssize_t r = bc_fd_recv_fd(sp[0], buf, sizeof(buf), &got);
        // os 2 fds que materializaram no recvmsg têm que estar FECHADOS pelo
        // bc_fd_recv_fd — a contagem volta exatamente ao que era.
        int leaked = count_open_fds() - before;
        check("O2: 2 cmsgs -> erro (-1)", r == -1);
        check("O2: nenhum fd vaza (contagem igual)", leaked == 0);
        close(fa); close(fb); close(sp[0]); close(sp[1]);
    }

    // ---- O3: linha longa descarta até '\n' e ERRA; a válida seguinte boa ----
    {
        int sp[2];
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) != 0) { perror("socketpair3"); return 1; }
        // linha MAIOR que o buffer + linha válida em seguida
        char big[300];
        memset(big, 'z', sizeof(big));
        big[0] = 'S'; big[299] = '\n';  // sem '\n' no meio
        if (write(sp[1], big, sizeof(big)) != (ssize_t)sizeof(big)) { perror("write big"); return 1; }
        if (write(sp[1], "ok\n", 3) != 3) { perror("write ok"); return 1; }

        char line[64];
        errno = 0;
        ssize_t r = bc_fd_read_line(sp[0], line, sizeof(line));
        check("O3: linha maior que o buffer -> ERRO (EMSGSIZE)",
              r == -1 && errno == EMSGSIZE);
        errno = 0;
        r = bc_fd_read_line(sp[0], line, sizeof(line));
        check("O3: a linha VÁLIDA seguinte chega inteira",
              r == 2 && strcmp(line, "ok") == 0);

        // EOF no meio da linha: erro, não linha truncada
        if (write(sp[1], "poca", 4) != 4) { perror("write poca"); return 1; }
        shutdown(sp[1], SHUT_WR);
        errno = 0;
        r = bc_fd_read_line(sp[0], line, sizeof(line));
        check("O3: EOF no meio da linha -> ERRO (EPROTO)",
              r == -1 && errno == EPROTO);
        // e EOF limpo no começo: 0 (a conexão acabou, sem meia-linha)
        r = bc_fd_read_line(sp[0], line, sizeof(line));
        check("O3: EOF limpo -> 0 (sem linha fantasma)", r == 0);
        close(sp[0]); close(sp[1]);
    }

    printf("== Resultado: %s (%d falhas) ==\n",
           g_fail == 0 ? "TODOS PASSARAM" : "HOUVE FALHAS", g_fail);
    return g_fail == 0 ? 0 : 1;
}
