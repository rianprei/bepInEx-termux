// bc_mods_fd.h — entrega de arquivo do companion (root) para o jogo, por FD.
//
// ============================================================================
// POR QUE ISTO EXISTE (revisao de seguranca do freebuff, severidade ALTA)
// ============================================================================
// A arvore de mods saiu de /data/local/tmp para /data/adb/bepinex porque o
// PAI antigo nao era controlado pelo root: /data/local/tmp e 0777 e
// /data/local e 0771 shell:shell, entao o shell — e qualquer appuid, inclusive
// o proprio jogo — podia trocar o diretorio por um link simbolico antes de o
// root.stat()/.chcon() nele. Toda operacao de root naquela arvore podia ser
// desviada.
//
// Em /data/adb o processo do jogo NAO tem acesso (root:root 0700) — e nao deve
// ter. Entao o jogo deixa de abrir caminho: quem abre e o companion, como root,
// e o que CRUZA a fronteira e o descritor de arquivo.
//
// MECANISMO:
//   1. o jogo pede um arquivo pelo socket que ja existe (g_stream_fd, main.cpp);
//   2. o companion abre o caminho com O_RDONLY|O_NOFOLLOW|O_CLOEXEC e
//      devolve o FD com sendmsg(SCM_RIGHTS);
//   3. o jogo recebe o FD e abre a biblioteca com
//      android_dlopen_ext(nome, ATilde, &envinfo, fd, ANDROID_DLEXT_USE_LIBRARY_FD)
//      — o carregador usa o FD, não o caminho.
//
// POR QUE O FD E NÃO O CONTEUDO DO .so: o .so tem que ficar MAPEÁVEL no
// processo do jogo. Copiar os bytes e escrever num arquivo exigiria que o
// processo do jogo CRIVESSE em /data/adb — o problema original de novo. O FD
// atravessa a fronteira do kernel sem o jogo tocar no caminho.
//
// POR QUE O_NOFOLLOW no companion: sem ele, um link dentro da arvore apontando
// para fora faria o root abrir o alvo. A arvore e root-only e o migrador nao
// segue link, mas o cheque de open() e a ultima linha de defesa (TOCTOU).
//
// CONF/AALLOWLIST (arquivos pequenos, nao mapeaveis) vaem por CONTEUDO no
// mesmo socket: o jogo so precisa do texto. Ver bc_mods_fd_send_content().
//
// Este header e a PARTE TESTAVEL: as funcoes de socket sao injetaveis
// (bc_fd_ops), o que deixa o par companion<->jogo testado no host com um
// socketpair() de verdade — incluindo o SCM_RIGHTS atravessando o kernel.

#ifndef BC_MODS_FD_H
#define BC_MODS_FD_H

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>   // snprintf
#include <stdlib.h> // atoi
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

// O_NOFOLLOW e o que impede o companion (root) de abrir um link de dentro da
// arvore para fora dela. E MSG_CMSG_CLOEXEC, o que impede o FD de vazar em
// fork/exec do jogo. Os dois sao extensao Linux e o glibc do HOST so os
// expoe com _GNU_SOURCE (o Bionic do device ja e o caso).
//
// ERRO DE COMPILACAO, e nao um #ifndef com fallback: definir O_NOFOLLOW como
// 0 compila, roda, e DESLIGA a protecao sem avisar — exatamente a classe de
// bug que a revisao pegou. Melhor quebrar o build do que linkar sem o flag.
#ifndef O_NOFOLLOW
#error "O_NOFOLLOW indisponivel: compilar com -D_GNU_SOURCE (ou em Bionic). Sem ele o companion abriria link simbolico como root."
#endif
#ifndef MSG_CMSG_CLOEXEC
#error "MSG_CMSG_CLOEXEC indisponivel: compilar com -D_GNU_SOURCE (ou em Bionic)."
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Versão do protocolo. O jogo manda isto primeiro; o companion recusa
// versão desconhecida em vez de interpretar bytes como comando.
#define BC_FD_PROTO 1

// Verbo: "SO" quer o FD, "TXT" quer o conteúdo.
#define BC_FD_VERB_SO "SO"
#define BC_FD_VERB_TXT "TXT"

// Linha de comando: "<proto> <verbo> <caminho>\n". Sem espaços no caminho
// (é um nome sob uma raiz fixa, e o parser é deliberadamente burro).
#define BC_FD_REQ_MAX 512

// Teto do conteúdo inline. Acima disso é arquivo grande demais para TXT; o
// chamador tem que pedir SO.
#define BC_FD_TXT_MAX (256 * 1024)

// Resposta de erro: "<n>\n" onde n é o errno. O companion envia isso em vez de
// mandar FD, para o jogo não tentar usar um -1.
#define BC_FD_ERR_MAX 32

// --- o par injetável -------------------------------------------------------
// No device, sendmsg/recvmsg/open reais. No host, stubs. A separação é o que
// permite testar o protocolo sem root e sem /data/adb.
typedef struct bc_fd_ops {
    int (*open_ro)(const char *path);                 // abre O_RDONLY|O_NOFOLLOW|O_CLOEXEC
    ssize_t (*send_fd)(int sock, int fd, const void *data, size_t len); // sendmsg+SCM_RIGHTS
    ssize_t (*recv_cmd)(int sock, char *buf, size_t cap);              // recv ate '\n'
    ssize_t (*send_data)(int sock, const void *data, size_t len);      // write simples
    ssize_t (*recv_fd)(int sock, void *data, size_t cap, int *out_fd); // recvmsg
} bc_fd_ops;

// As reais. Definem-se aqui para o device não precisar de um segundo arquivo.
static inline int bc_fd_open_ro(const char *path) {
    // O_NOFOLLOW e o que impede o root de abrir um link de dentro da arvore
    // para fora dela. O_CLOEXEC para o FD não vazar em fork/exec do jogo.
    return open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
}

static inline ssize_t bc_fd_send(int sock, int fd, const void *data, size_t len) {
    struct msghdr msg;
    struct iovec iov;
    union {
        struct cmsghdr align;
        char buf[CMSG_SPACE(sizeof(int))];
    } cmsgu;
    memset(&msg, 0, sizeof(msg));
    memset(&cmsgu, 0, sizeof(cmsgu));
    iov.iov_base = (void *)data;
    iov.iov_len = len;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = cmsgu.buf;
    msg.msg_controllen = sizeof(cmsgu.buf);
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    if (cmsg == NULL) return -1;
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(cmsg), &fd, sizeof(int));
    return sendmsg(sock, &msg, MSG_NOSIGNAL);
}

static inline ssize_t bc_fd_recv_fd(int sock, void *data, size_t cap, int *out_fd) {
    struct msghdr msg;
    struct iovec iov;
    union {
        struct cmsghdr align;
        char buf[CMSG_SPACE(sizeof(int))];
    } cmsgu;
    char name[CMSG_SPACE(sizeof(int))];
    struct cmsghdr *cmsg;
    memset(&msg, 0, sizeof(msg));
    memset(&cmsgu, 0, sizeof(cmsgu));
    memset(name, 0, sizeof(name));
    *out_fd = -1;
    iov.iov_base = data;
    iov.iov_len = cap;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = cmsgu.buf;
    msg.msg_controllen = sizeof(cmsgu.buf);
    ssize_t r = recvmsg(sock, &msg, MSG_CMSG_CLOEXEC);
    if (r <= 0) return r;
    for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
        if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS &&
            cmsg->cmsg_len == CMSG_LEN(sizeof(int))) {
            memcpy(out_fd, CMSG_DATA(cmsg), sizeof(int));
        }
    }
    return r;
}

// Envio simples, sem FD: o que o protocolo TXT usa (conf/allowlist, que
// não são mapeáveis — o jogo só precisa do texto).
static inline ssize_t bc_fd_send_data(int sock, const void *data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = write(sock, (const char *)data + sent, len - sent);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        sent += (size_t)n;
    }
    return (ssize_t)sent;
}

// Monta o pedido. Devolve o tamanho, ou -1 se não coube / tem caractere ruim.
static inline ssize_t bc_fd_build_request(char *buf, size_t cap, int proto,
                                         const char *verb, const char *path) {
    size_t vl = strlen(verb);
    size_t pl = strlen(path);
    // Rejeita espaço/newline no caminho: o parser é linha-a-linha, e um
    // caminho com espaço viraria dois campos silenciosamente.
    if (memchr(path, '\n', pl) != NULL || memchr(path, ' ', pl) != NULL) return -1;
    int n = snprintf(buf, cap, "%d %s %s\n", proto, verb, path);
    if (n < 0 || (size_t)n >= cap) return -1;
    (void)vl;
    return (ssize_t)n;
}

// Lê o pedido que o outro lado mandou. Devolve 1=ok, 0=nada ainda, -1=malformado.
static inline int bc_fd_parse_request(const char *line, int *proto,
                                      char *verb, size_t verbcap, char *path,
                                      size_t pathcap) {
    if (line == NULL) return -1;
    size_t len = strnlen(line, BC_FD_REQ_MAX);
    if (len == 0) return 0;
    if (line[len - 1] == '\n') len--;
    if (len == 0) return 0;
    char work[BC_FD_REQ_MAX];
    if (len >= sizeof(work)) return -1;
    memcpy(work, line, len);
    work[len] = '\0';
    char *sp1 = strchr(work, ' ');
    if (sp1 == NULL) return -1;
    *sp1 = '\0';
    char *sp2 = strchr(sp1 + 1, ' ');
    if (sp2 == NULL) return -1;
    *sp2 = '\0';
    const char *p = sp1 + 1;
    size_t vl = strlen(p);
    if (vl == 0 || vl >= verbcap) return -1;
    memcpy(verb, p, vl + 1);
    const char *q = sp2 + 1;
    size_t ql = strlen(q);
    if (ql == 0 || ql >= pathcap) return -1;
    memcpy(path, q, ql + 1);
    *proto = atoi(work);
    return 1;
}

// Monta a resposta de erro: "<errno>\n".
static inline ssize_t bc_fd_build_error(char *buf, size_t cap, int err) {
    int n = snprintf(buf, cap, "%d\n", err);
    if (n < 0 || (size_t)n >= cap) return -1;
    return (ssize_t)n;
}

// Lê a resposta de erro: 1=é erro (errno em *out), 0=não é (é o ack do FD).
static inline int bc_fd_parse_error(const char *line, int *out) {
    if (line == NULL || line[0] < '0' || line[0] > '9') return 0;
    *out = atoi(line);
    return 1;
}

#ifdef __cplusplus
}
#endif

#endif // BC_MODS_FD_H
