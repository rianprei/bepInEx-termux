// bc_req_session.h — o CANAL REQ como recurso limitado, puro e host-testável.
//
// ============================================================================
// POR QUE ESTE HEADER EXISTE (P1 da pré-revisão freebuff: DoS por qualquer app)
// ============================================================================
// O ramo REQ do accept loop criava uma pthread por conexão SEM teto e sem
// gate de abertura: qualquer app do aparelho abria N canais ociosos (pilha
// de 8 MB virtual + fd cada) e o jogo legítimo disputava o companion com o
// flood. A defesa é em camadas, sem timeout cego (o canal legítimo PODE
// ficar ocioso — timeout o matava na primeira idle, o trade-off que motivou
// o WIP):
//
//   1. GATE NA ABERTURA: o peer é resolvido (SO_PEERCRED -> pacotes do
//      appId) e o canal só abre se o chamador é SERVÁVEL — seu pacote
//      decide um caminho de mods (bc_decide_path != NONE: Battle Cats,
//      pasta própria ou allowlist) — e o userId é 0 (o dono do /data/adb;
//      uid de outro usuário não tem árvore aqui). Os VERBOS continuam
//      checando identidade por pacote (defesa dupla: abrir o canal não
//      garante nada além do canal).
//
//   2. TETO: global (16 canais) e por uid (4 canais). O excedente recebe
//      E<errno> na cara — SEM criar thread (o acquire acontece no accept
//      loop, ANTES do spawn). O teto global protege o companion de um
//      conjunto de apps distintos; o por-uid impede que UM app sozinho
//      coma o global inteiro.
//
//   3. CONTADOR em TODO caminho de saída: a thread sai (EOF/erro), o
//      spawn falha (release ANTES de fechar), a recusa no topo (nunca
//      adquiriu). Vazamento de slot = canal a menos para o jogo legítimo,
//      e o e2e conta active==0 depois de CADA ciclo de vida.
//
// Puro de propósito: o companion injeta SO_PEERCRED/packages.list/stat;
// o teste de host injeta uids e pacotes de fixture — o MESMO veredito nos
// dois lados (o padrão de bc_peercred.h).

#ifndef BC_REQ_SESSION_H
#define BC_REQ_SESSION_H

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "bc_mods_fd.h"
#include "bc_path_decide.h"
#include "bc_req_channel.h"
#include "bc_req_dispatch.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---- tetos (constantes nomeadas; o porquê mora com o número) ---------------
//
// GLOBAL=16: o companion é um processo com N threads de 8 MB de pilha
// virtual cada; 16 canais é folga de 2x para o caso legítimo (um jogo +
// Termux + reaberturas de processo) e ainda deixa o flood morto no chão.
// POR-UID=4: um app sozinho não pode comer o global inteiro — 4 cobre jogo
// principal + processos :sufixo dele (o bc_process aceita vários).
#define BC_REQ_MAX_GLOBAL 16
#define BC_REQ_MAX_PER_UID 4

// pacotes por chamador (grupo sharedUserId): o bc_peercred usa 16
#define BC_REQ_SESSION_MAX_PKGS 16
#define BC_REQ_SESSION_PKG_CAP 160

struct bc_req_slots {
    pthread_mutex_t mu;
    int global;                                        // canais ativos, total
    struct { uid_t uid; int n; } per[BC_REQ_MAX_GLOBAL];  // por uid (0 = livre)
};

static inline void bc_req_slots_init(struct bc_req_slots *s) {
    memset(s, 0, sizeof(*s));
    pthread_mutex_init(&s->mu, NULL);
}

// Adquire um slot (global + por-uid). 0 = ok; -1 = recusado, com o errno
// da recusa em *why_errno (EMFILE: global cheio; EAGAIN: o uid já tem
// BC_REQ_MAX_PER_UID). Chamado ANTES de qualquer spawn — a recusa não cria
// thread nenhuma (T-P1a).
static inline int bc_req_slot_acquire(struct bc_req_slots *s, uid_t uid, int *why_errno) {
    if (why_errno) *why_errno = 0;
    pthread_mutex_lock(&s->mu);
    if (s->global >= BC_REQ_MAX_GLOBAL) {
        pthread_mutex_unlock(&s->mu);
        if (why_errno) *why_errno = EMFILE;
        return -1;
    }
    int slot = -1;
    for (int i = 0; i < BC_REQ_MAX_GLOBAL; i++) {
        if (s->per[i].uid == uid && s->per[i].n > 0) {
            if (s->per[i].n >= BC_REQ_MAX_PER_UID) {
                pthread_mutex_unlock(&s->mu);
                if (why_errno) *why_errno = EAGAIN;
                return -1;
            }
            slot = i;
            break;
        }
        if (slot < 0 && s->per[i].n == 0) slot = i;
    }
    if (slot < 0) {  // todos os 16 espaços com uids distintos (global já teria batido)
        pthread_mutex_unlock(&s->mu);
        if (why_errno) *why_errno = EMFILE;
        return -1;
    }
    s->per[slot].uid = uid;
    s->per[slot].n++;
    s->global++;
    pthread_mutex_unlock(&s->mu);
    return 0;
}

// Devolve o slot. Chamado em TODO caminho de saída da thread (e no
// spawn-falho). Vazar slot = canal a menos para o jogo legítimo.
static inline void bc_req_slot_release(struct bc_req_slots *s, uid_t uid) {
    pthread_mutex_lock(&s->mu);
    for (int i = 0; i < BC_REQ_MAX_GLOBAL; i++) {
        if (s->per[i].uid == uid && s->per[i].n > 0) {
            s->per[i].n--;
            if (s->per[i].n == 0) s->per[i].uid = 0;
            if (s->global > 0) s->global--;
            break;
        }
    }
    pthread_mutex_unlock(&s->mu);
}

// Canais ativos no total (o teste de vazamento confere 0 aqui).
static inline int bc_req_slot_active(struct bc_req_slots *s) {
    pthread_mutex_lock(&s->mu);
    int g = s->global;
    pthread_mutex_unlock(&s->mu);
    return g;
}

// ---- gate de abertura (a decisão de identidade do canal) --------------------
//
// userId: o Android multiplexa usuários no uid (userId*100000 + appId). A
// árvore /data/adb/bepinex é do usuário 0 — um app do perfil de trabalho
// (userId != 0) não tem mods AQUI, e abrir canal pra ele é abrir canal que
// nenhum verbo vai servir (fail-closed na porta, não no verbo).
// Servável: bc_decide_path != NONE — Battle Cats, pasta própria
// (zero-config) ou allowlist. O companion injeta os DOIS fatos (stat da
// árvore root-only e allowlist), igual ao verbo PATH.
static inline int bc_req_open_gate(uid_t uid, const char *pkg, bool dir_exists,
                                   bool in_allowlist, int *why_errno) {
    if (why_errno) *why_errno = 0;
    if (uid / 100000 != 0) {
        if (why_errno) *why_errno = EPERM;
        return -1;
    }
    if (bc_decide_path(pkg, dir_exists, in_allowlist) == BC_PATH_NONE) {
        if (why_errno) *why_errno = EACCES;
        return -1;
    }
    return 0;
}

// ---- a ponta injetável ------------------------------------------------------
struct bc_req_server_ops {
    int (*peer_uid)(int fd, uid_t *out);       // SO_PEERCRED (host-compatible)
    int (*resolve_pkgs)(int fd, uid_t uid,    // pacotes do appId do chamador
                        char pkgs[][BC_REQ_SESSION_PKG_CAP], int max);
    bool (*pkg_has_dir)(const char *pkg);      // companion: stat da árvore
    bool (*pkg_in_allowlist)(const char *pkg);
    const struct bc_req_handlers *handlers;
    int (*spawn)(void *(*body)(void *), void *arg);  // pthread_create (ou stub)
};

struct bc_req_thread_arg {
    int fd;
    uid_t uid;
    struct bc_req_slots *slots;
    const struct bc_req_handlers *handlers;
};

static inline void bc_req_reply_errno(int fd, int e) {
    char buf[BC_FD_ERR_MAX];
    ssize_t n = bc_fd_build_txt_error(buf, sizeof(buf), e);
    if (n > 0) bc_fd_send_data(fd, buf, (size_t)n);
}

// O corpo da thread do canal: despacha até a conexão morrer e DEVOLVE o
// slot em TODA saída. O arg é doado pelo serve (malloc) e liberado aqui.
static inline void *bc_req_thread_body(void *p) {
    struct bc_req_thread_arg *a = (struct bc_req_thread_arg *)p;
    for (;;) {
        if (bc_req_dispatch_one(a->fd, a->handlers) < 0) break;
    }
    bc_req_slot_release(a->slots, a->uid);
    close(a->fd);
    free(a);
    return NULL;
}

// O ramo REQ do accept loop inteiro, puro. `first/firstlen` é a linha de
// papel que o accept já leu. Devolve 0 se o canal foi aceito (a thread é
// dona do fd e do slot), -1 se recusou/fechou (o accept segue).
static inline int bc_req_serve_connection(int fd, const char *first, ssize_t firstlen,
                                          const struct bc_req_server_ops *ops,
                                          struct bc_req_slots *slots) {
    if (bc_req_role_from_line(first, firstlen) != BC_ROLE_REQ) {
        close(fd);
        return -1;
    }
    uid_t uid = 0;
    if (ops->peer_uid == NULL || ops->peer_uid(fd, &uid) != 0) {
        bc_req_reply_errno(fd, EPERM);
        close(fd);
        return -1;
    }
    char pkgs[BC_REQ_SESSION_MAX_PKGS][BC_REQ_SESSION_PKG_CAP];
    int npkgs = 0;
    if (ops->resolve_pkgs != NULL) {
        npkgs = ops->resolve_pkgs(fd, uid, pkgs, BC_REQ_SESSION_MAX_PKGS);
    }
    if (npkgs <= 0) {  // peer não mapeia a pacote nenhum: fail-closed
        bc_req_reply_errno(fd, EPERM);
        close(fd);
        return -1;
    }
    int gate_errno = EACCES;
    bool servable = false;
    for (int i = 0; i < npkgs && !servable; i++) {
        bool dir = ops->pkg_has_dir != NULL && ops->pkg_has_dir(pkgs[i]);
        bool allow = ops->pkg_in_allowlist != NULL && ops->pkg_in_allowlist(pkgs[i]);
        if (bc_req_open_gate(uid, pkgs[i], dir, allow, &gate_errno) == 0) {
            servable = true;
        }
    }
    if (!servable) {
        bc_req_reply_errno(fd, gate_errno);
        close(fd);
        return -1;
    }
    // Teto ANTES do spawn: o excedente é recusado SEM criar thread (T-P1a).
    int why = 0;
    if (bc_req_slot_acquire(slots, uid, &why) != 0) {
        bc_req_reply_errno(fd, why);
        close(fd);
        return -1;
    }
    struct bc_req_thread_arg *a =
        (struct bc_req_thread_arg *)malloc(sizeof(*a));
    if (a == NULL) {  // sem memória: devolve o slot e fecha
        bc_req_slot_release(slots, uid);
        close(fd);
        return -1;
    }
    a->fd = fd;
    a->uid = uid;
    a->slots = slots;
    a->handlers = ops->handlers;
    if (ops->spawn == NULL || ops->spawn(bc_req_thread_body, a) != 0) {
        // T-P1c: caminho do spawn-falho — o slot volta ANTES do close.
        bc_req_slot_release(slots, uid);
        free(a);
        close(fd);
        return -1;
    }
    return 0;  // a thread é dona do fd e do slot agora
}

#ifdef __cplusplus
}
#endif

#endif // BC_REQ_SESSION_H
