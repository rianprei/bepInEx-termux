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
//   - companion.cpp passa os handlers REAIS de root (cada um gateado por
//     SO_PEERCRED -> packages.list -> pacote do chamador, ver bc_peer_ok_for_pkg);
//   - o teste de host passa handlers de teste, num socketpair.
//
// Se o despacho deixar de funcionar, o teste falha. E como o mesmo header
// alimenta os dois lados, nao existe "o despacho real" que o teste nao exercise.

#ifndef BC_REQ_DISPATCH_H
#define BC_REQ_DISPATCH_H

#include <errno.h>
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
// Verbos da árvore Battle Cats: layout próprio (flat, sem subpasta por
// pacote) e confs na RAIZ da árvore — por isso verbos próprios, e não uma
// forçação do formato (pkg, nome): o servidor quem monta o caminho, e o
// "BT" só aceita nome de uma LISTA FIXA (nunca um caminho do cliente).
typedef void (*bc_req_bc_so_fn)(int fd, const char *name);
typedef void (*bc_req_bc_list_fn)(int fd);
typedef void (*bc_req_bc_conf_fn)(int fd, const char *name);

struct bc_req_handlers {
    bc_req_so_fn so;
    bc_req_txt_fn txt;
    bc_req_list_fn list;
    bc_req_bc_so_fn bc_so;    // "BO"
    bc_req_bc_list_fn bc_list; // "BL"
    bc_req_bc_conf_fn bc_conf; // "BT" — nome da lista fixa abaixo
};

// LISTA FIXA dos arquivos de configuração da RAIZ da árvore que o "BT"
// serve. O cliente manda um NOME; o servidor só aceita se estiver AQUI e
// monta o caminho sozinho — nunca um caminho vindo do cliente (mesma regra
// estrutural do (pkg, nome) dos outros verbos). bc_mods.conf: config dos 4
// hooks estáticos (toggle_mod/set_mod escrevem); bc_generic_allowlist.conf:
// a allowlist do experimento Cocos (pública, o Manager/Termux também lê).
static inline bool bc_req_root_conf_ok(const char *name) {
    if (name == NULL) return false;
    return strcmp(name, "bc_mods.conf") == 0 ||
           strcmp(name, "bc_generic_allowlist.conf") == 0;
}

// Le UMA request e despacha. Contrato de retorno:
//    1  = atendeu (o handler respondeu)
//    0  = recusou — e a recusa RESPONDEU no socket (linha "<errno>\n"): proto
//         errado ou verbo desconhecido. A conexao segue util: o cliente pode
//         tentar outro pedido.
//   -1  = fim da conexao (read <= 0): quem chama fecha o fd.
//
// Silencio e o sintoma que a revisao achou: toda recusa responde, nunca fica
// mudA. E a resposta e uma LINHA de errno, igual ao resto do protocolo — a
// versao anterior mandava "unknown_verb" SEM '\n' (e com um NUL a mais): o
// parser de linha do cliente nao consumia, o pedido seguinte herdava bytes
// soltos no buffer, e o e2e travava no "verbo desconhecido: respondeu".
static inline int bc_req_dispatch_one(int fd, const struct bc_req_handlers *h) {
    char buf[4096];
    ssize_t n = bc_fd_read_line(fd, buf, sizeof(buf));
    if (n <= 0) return -1;
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
    if (strcmp(verb, BC_FD_VERB_BC_SO) == 0 && h->bc_so != NULL) { h->bc_so(fd, arg1); return 1; }
    if (strcmp(verb, BC_FD_VERB_BC_LIST) == 0 && h->bc_list != NULL) { h->bc_list(fd); return 1; }
    if (strcmp(verb, BC_FD_VERB_BC_TXT) == 0 && h->bc_conf != NULL) { h->bc_conf(fd, arg1); return 1; }
    char e[BC_FD_ERR_MAX];
    ssize_t k = bc_fd_build_error(e, sizeof(e), EINVAL);
    if (k > 0) bc_fd_send_data(fd, e, (size_t)k);
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif // BC_REQ_DISPATCH_H
