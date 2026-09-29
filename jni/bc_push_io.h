// bc_push_io.h — o transporte do push_mod, puro e host-testável.
//
// ============================================================================
// POR QUE ESTE HEADER EXISTE (companion-followups #1)
// ============================================================================
// O handle_push_mod lia EXATAMENTE <size> bytes e confirmava "ok" — o que
// viesse DEPOIS ficava no socket sem dono. Um emissor bugado (ou malicioso)
// que mandasse size+N emporilhava N bytes no buffer, e a próxima linha de
// comando da mesma conexão nascia corrompida. O "ok" mentia: confirmava uma
// transferência que o protocolo não garantia completa.
//
// O contrato agora é o mesmo do emissor oficial (tools/push_mod_emit.py):
// "push_mod <nome> <size>\n" + EXATAMENTE size bytes + shutdown(SHUT_WR).
// O EOF do half-close é a PROVA de que não sobrou byte — determinístico,
// sem adivinhação de timing. Sem o half-close, o read do tail cai no
// SO_RCVTIMEO do socket (curto: o accept loop do companion arma 3s) e vira
// recusa com motivo: cliente velho que não fecha o lado de escrita escuta
// de volta um erro acionável em vez de um timeout mudo.
//
// Puro de propósito: mesmo código no companion (root) e no teste de host
// (socketpair de verdade). Não há como o transporte divergir sem o teste
// cair — foi exatamente a classe de defeito do wiring-audit.

#ifndef BC_PUSH_IO_H
#define BC_PUSH_IO_H

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

// Lê EXATAMENTE `size` bytes de `sock` gravando em `out_fd` (append), e
// depois confere o tail (EOF esperado). Devolve:
//    0  = transferência íntegra (size bytes no out_fd, EOF logo depois)
//   -1  = erro; o MOTIVO vai em `why` (string completa, pronta pro log).
//
// Depende do SO_RCVTIMEO do socket (o chamador arma): é o que transforma
// "cliente não fechou" em recusa em vez de espera infinita.
static inline int bc_push_recv_exact(int sock, int out_fd, long size,
                                     char *why, size_t whycap) {
    if (why) why[0] = '\0';
    char chunk[8192];
    long remaining = size;
    while (remaining > 0) {
        size_t want = remaining < (long)sizeof(chunk) ? (size_t)remaining : sizeof(chunk);
        ssize_t got = read(sock, chunk, want);
        if (got < 0 && errno == EINTR) continue;  // sinal não é erro de transferência
        if (got < 0) {
            snprintf(why, whycap, "read: %s", strerror(errno));
            return -1;
        }
        if (got == 0) {
            snprintf(why, whycap,
                     "transfer incomplete: %ld bytes a menos do que o anunciado",
                     remaining);
            return -1;
        }
        ssize_t w = write(out_fd, chunk, (size_t)got);
        if (w != got) {
            snprintf(why, whycap, "write: %s", strerror(errno));
            return -1;
        }
        remaining -= got;
    }
    // TAIL: o emissor fecha o lado de escrita depois do payload. O EOF é a
    // prova de que não sobrou byte no socket.
    for (;;) {
        ssize_t extra = read(sock, chunk, sizeof(chunk));
        if (extra < 0 && errno == EINTR) continue;
        if (extra < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            snprintf(why, whycap,
                     "emissor nao fechou o lado de escrita (shutdown) — sem EOF "
                     "nao ha prova de que nao sobrou byte");
            return -1;
        }
        if (extra < 0) {
            snprintf(why, whycap, "read do tail: %s", strerror(errno));
            return -1;
        }
        if (extra > 0) {
            snprintf(why, whycap,
                     "%zd bytes ALEM do anunciado — transferencia recusada",
                     extra);
            return -1;
        }
        return 0;  // EOF: exatamente size bytes, nada esquecido no socket
    }
}

#ifdef __cplusplus
}
#endif

#endif // BC_PUSH_IO_H
