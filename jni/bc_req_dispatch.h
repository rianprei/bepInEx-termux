// bc_req_dispatch.h — o DESPACHANTE do canal de pedidos, sem Android.
//
// ============================================================================
// POR QUE ISTO E UM HEADER E NAO O CORPO DO companion.cpp (achado do OpenCode)
// ============================================================================
// O fio do canal de pedidos e o que estava quebrado, e ele nao da para ser
// testado no host: companion.cpp inclui <android/log.h> e vive inteiro no
// daemon root. Entao o despacho — a parte que decide qual handler chamar — fica
// AQUI, em um nucleo puro, e os DOIS lados usam o MESMO codigo:
//
//   - companion.cpp passa os handlers REAIS de root;
//   - o teste de host passa handlers de teste, num socketpair.
//
// Se o despacho deixar de funcionar, o teste falha. E como o mesmo header
// alimenta os dois lados, nao existe "o despacho real" que o teste nao exercise.

#ifndef BC_REQ_DISPATCH_H
#define BC_REQ_DISPATCH_H

#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "bc_mods_fd.h"
#include "bc_req_channel.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*bc_req_so_fn)(int fd, const char *pkg, const char *name);
typedef void (*bc_req_txt_fn)(int fd, const char *pkg, const char *name);
typedef void (*bc_req_list_fn)(int fd, const char *pkg);

struct bc_req_handlers {
    bc_req_so_fn so;
    bc_req_txt_fn txt;
    bc_req_list_fn list;
};

// Le UMA request e despacha. Devolve 1 se atendeu, 0 se recusou.
//
// "<proto> <verbo> <a> [b]" — o mesmo formato que bc_fd_build_request2 monta.
// Verbo desconhecido, proto errado ou linha malformada viram recusa, nunca um
// silencio: silencio no canal de pedidos e o sintoma que a revisiao achou.
static inline int bc_req_dispatch_one(int fd, const struct bc_req_handlers *h) {
    char buf[4096];
    ssize_t n = bc_fd_read_line(fd, buf, sizeof(buf));
    if (n <= 0) return 0;
    int proto = 0;
    char verb[32] = {0};
    char arg1[320] = {0};
    char arg2[320] = {0};
    char *p1 = strchr(buf, ' ');
    if (p1 == nullptr) return 0;
    *p1 = '\0';
    proto = atoi(buf);
    char *rest = p1 + 1;
    char *p2 = strchr(rest, ' ');
    if (p2 != nullptr) { *p2 = '\0'; }
    snprintf(verb, sizeof(verb), "%s", rest);
    char *a = p2 ? p2 + 1 : rest + strlen(rest);
    char *sp = strchr(a, ' ');
    if (sp != nullptr) { *sp = '\0'; snprintf(arg1, sizeof(arg1), "%s", a); snprintf(arg2, sizeof(arg2), "%s", sp + 1); }
    else { snprintf(arg1, sizeof(arg1), "%s", a); }

    if (proto != BC_FD_PROTO) {
        char e[BC_FD_ERR_MAX];
        ssize_t k = bc_fd_build_error(e, sizeof(e), EPROTO);
        if (k > 0) bc_fd_send_data(fd, e, (size_t)k);
        return 0;
    }
    if (strcmp(verb, BC_FD_VERB_SO) == 0 && h->so != NULL) { h->so(fd, arg1, arg2); return 1; }
    if (strcmp(verb, "TX") == 0 && h->txt != NULL) { h->txt(fd, arg1, arg2); return 1; }
    if (strcmp(verb, BC_FD_VERB_LS) == 0 && h->list != NULL) { h->list(fd, arg1); return 1; }
    bc_fd_send_data(fd, "unknown_verb", 13);
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif // BC_REQ_DISPATCH_H
