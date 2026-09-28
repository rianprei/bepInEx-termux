// bc_req_client.h — o lado do JOGO no canal de pedidos, puro e host-testável.
//
// Extraído de main.cpp (WIP 5dcc38f) para o teste e2e usar o cliente REAL —
// o mesmo código que roda no device, do build_request ao parse da resposta —
// em vez de uma réplica. Puro: socket vem de fora (device: conexão ao
// @bc_companion aberta pós-specialize; teste: ponta de um socketpair), mutex
// opcional (device: serializa request+resposta entre threads de carga/reload;
// teste: passa o dele ou NULL).
//
// CORREÇÕES que vêm junto com a extração (causas-raiz do e2e vermelho e da
// cirurgia do WIP):
//  - os três pedidos agora usam o MESMO socket de pedidos: no WIP,
//    bc_mod_fd_request/bc_mod_text_request liam o socket de STREAMING
//    (bc_fd_socket/g_stream_fd — o do broadcast) e só o list apontava pro
//    canal de pedidos;
//  - a disciplina de mutex era misturada (fd travava, list DESTRAVAVA sem
//    ter travado — UB — e text não travava nada): agora simétrica e opcional.

#ifndef BC_REQ_CLIENT_H
#define BC_REQ_CLIENT_H

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#include "bc_mods_fd.h"
#include "bc_req_channel.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BC_REQ_TIMEOUT_SEC 5

// Conecta ao socket abstract @<name>. hello != NULL escreve a linha de papel
// (ex.: BC_REQ_HELLO) como PRIMEIRA linha da conexao. Tenta até `retries`
// vezes com 100ms entre elas: o daemon faz bind do @bc_companion DEPOIS do
// primeiro connectCompanion (que acontece no pre-specialize), e a conexao do
// jogo pode chegar antes do bind — ECONNREFUSED aqui é corrida, não estado.
static inline int bc_req_connect(const char *name, const char *hello,
                                 int retries, char *why, size_t whycap) {
    if (why) why[0] = '\0';
    for (int attempt = 0; attempt < retries; attempt++) {
        int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd < 0) {
            if (why) snprintf(why, whycap, "socket: %s", strerror(errno));
            return -1;
        }
        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        addr.sun_path[0] = '\0';
        size_t name_len = strlen(name);
        if (name_len >= sizeof(addr.sun_path) - 1) {
            close(fd);
            if (why) snprintf(why, whycap, "nome de socket longo demais");
            return -1;
        }
        memcpy(addr.sun_path + 1, name, name_len);
        socklen_t addrlen = (socklen_t)(sizeof(addr.sun_family) + 1 + name_len);
        if (connect(fd, (struct sockaddr *)&addr, addrlen) != 0) {
            close(fd);
            if (attempt + 1 < retries) {
                usleep(100 * 1000);
                continue;
            }
            if (why) snprintf(why, whycap, "connect @%s: %s", name, strerror(errno));
            return -1;
        }
        if (hello != NULL) {
            size_t hl = strlen(hello);
            if (write(fd, hello, hl) != (ssize_t)hl) {
                close(fd);
                if (why) snprintf(why, whycap, "hello: %s", strerror(errno));
                return -1;
            }
        }
        return fd;
    }
    return -1;
}

// ---- pedidos: request + resposta = UMA transação no socket dedicado -------
//
// O timeout é armado e desarmado em volta de CADA pedido: um companion mudo
// (daemon morto, socket que não subiu) vira "mod não carrega" com log, NUNCA
// jogo travado em recv. Não vaza para o próximo pedido nem para o streaming.

// "PATH <pkg>" — pergunta PRÉ-specialize: a decisão de caminho (bc_decide_path)
// precisa de dois fatos que vivem na árvore root-only (existe pasta de mods?
// está na allowlist?), e o loader sozinho não os enxerga mais. O ROOT responde
// os FATOS em "<dir> <allow>\n"; a DECISÃO continua no loader, na função pura
// testada. Conexão ONE-SHOT (abre, pergunta, fecha) no @bc_companion: nessa
// hora o chamador ainda é uid 0 (filho do zygote), e o gate de UID do accept
// (root/shell/Termux) é quem autoriza — o gate POR PACOTE não se aplica a um
// peer uid 0, e aqui isso é correto: quem pergunta é o próprio loader, não
// código de mod (mods rodam só depois do specialize).
static inline int bc_req_ask_path(int sock, const char *pkg, bool *dir_exists,
                                   bool *in_allowlist, char *why, size_t whycap) {
    if (dir_exists != NULL) *dir_exists = false;
    if (in_allowlist != NULL) *in_allowlist = false;
    if (why) why[0] = '\0';
    char req[256];
    ssize_t n = (ssize_t)snprintf(req, sizeof(req), "PATH %s\n", pkg);
    if (n <= 0 || (size_t)n >= sizeof(req)) {
        if (why) snprintf(why, whycap, "pacote invalido para o PATH");
        return -1;
    }
    if (bc_fd_send_data(sock, req, (size_t)n) < 0) {
        if (why) snprintf(why, whycap, "companion nao recebeu o PATH");
        return -1;
    }
    char line[64];
    ssize_t r = bc_fd_read_line(sock, line, sizeof(line));
    if (r <= 0) {
        if (why) snprintf(why, whycap, "companion nao respondeu o PATH");
        return -1;
    }
    int d = 0, a = 0;
    if (sscanf(line, "%d %d", &d, &a) != 2) {
        if (why) snprintf(why, whycap, "resposta de PATH invalida: '%s'", line);
        return -1;
    }
    if (dir_exists != NULL) *dir_exists = d != 0;
    if (in_allowlist != NULL) *in_allowlist = a != 0;
    return 0;
}

// "SO <pkg> <nome>" -> FD do .so, pronto para android_dlopen_ext. -1 = recusa
// (motivo em why; errno vem na linha de erro do companion).
static inline int bc_req_ask_so(int sock, pthread_mutex_t *io, const char *pkg,
                                const char *name, char *why, size_t whycap) {
    if (why) why[0] = '\0';
    if (sock < 0) {
        if (why) snprintf(why, whycap, "sem canal de pedidos com o companion");
        return -1;
    }
    if (io != NULL) pthread_mutex_lock(io);
    struct timeval tv = { .tv_sec = BC_REQ_TIMEOUT_SEC, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    char req[BC_FD_REQ_MAX];
    ssize_t n = bc_fd_build_request2(req, sizeof(req), BC_FD_PROTO,
                                     BC_FD_VERB_SO, pkg, name);
    int fd = -1;
    if (n <= 0) {
        if (why) snprintf(why, whycap, "nome invalido para o pedido de FD");
    } else if (bc_fd_send_data(sock, req, (size_t)n) < 0) {
        if (why) snprintf(why, whycap, "companion nao recebeu o pedido: %s", strerror(errno));
    } else {
        char payload[BC_FD_ERR_MAX];
        ssize_t r = bc_fd_recv_fd(sock, payload, sizeof(payload), &fd);
        if (r < 0) {
            if (why) snprintf(why, whycap, "companion nao respondeu em %ds", BC_REQ_TIMEOUT_SEC);
        } else if (fd < 0) {
            int e = 0;
            if (bc_fd_parse_error(payload, &e))
                { if (why) snprintf(why, whycap, "companion recusou: errno %d (%s)", e, strerror(e)); }
            else
                { if (why) snprintf(why, whycap, "resposta sem fd e sem erro"); }
        }
    }
    struct timeval off = { .tv_sec = 0, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &off, sizeof(off));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &off, sizeof(off));
    if (io != NULL) pthread_mutex_unlock(io);
    return fd;
}

// "TX <pkg> <nome>" -> conteudo (conf/allowlist): "<len>\n<bytes>". Devolve o
// comprimento, ou -1 com motivo em why.
static inline int bc_req_ask_txt(int sock, pthread_mutex_t *io, const char *pkg,
                                 const char *name, char *out, size_t cap,
                                 char *why, size_t whycap) {
    if (out && cap) out[0] = '\0';
    if (why) why[0] = '\0';
    if (sock < 0) {
        if (why) snprintf(why, whycap, "sem canal de pedidos com o companion");
        return -1;
    }
    if (io != NULL) pthread_mutex_lock(io);
    struct timeval tv = { .tv_sec = BC_REQ_TIMEOUT_SEC, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    char req[BC_FD_REQ_MAX];
    ssize_t n = bc_fd_build_request2(req, sizeof(req), BC_FD_PROTO, "TX", pkg, name);
    long total = -1;
    if (n <= 0) {
        if (why) snprintf(why, whycap, "nome invalido para o pedido de conteudo");
    } else if (bc_fd_send_data(sock, req, (size_t)n) < 0) {
        if (why) snprintf(why, whycap, "companion nao recebeu o pedido de conteudo");
    } else {
        char buf[16384];
        size_t used = 0;
        char *nl = NULL;
        // 1a fase: achar a linha de cabecalho "<len>\n". O companion manda
        // o cabecalho e o conteudo em writes SEPARADOS — o conteudo pode
        // nao ter chegado quando o '\n' do cabecalho aparece.
        for (;;) {
            ssize_t r = read(sock, buf + used, sizeof(buf) - 1 - used);
            if (r < 0) {
                if (errno == EINTR) continue;
                if (why) snprintf(why, whycap, "companion nao respondeu o conteudo em %ds", BC_REQ_TIMEOUT_SEC);
                break;
            }
            if (r == 0) {
                if (why) snprintf(why, whycap, "companion fechou antes do conteudo");
                break;
            }
            used += (size_t)r;
            buf[used] = '\0';
            nl = (char *)memchr(buf, '\n', used);
            if (nl != NULL) break;
            if (used >= sizeof(buf) - 1) {
                if (why) snprintf(why, whycap, "conteudo maior que o buffer");
                break;
            }
        }
        if (nl != NULL) {
            *nl = '\0';
            size_t head = (size_t)(nl - buf) + 1;
            // Recusa da família texto vem como "E<errno>\n" — SEM o prefixo,
            // o "<errno>\n" seria parseado como comprimento e o cliente
            // esperaria bytes que não vêm (stall de 5s por recusa).
            int perr = 0;
            if (bc_fd_parse_txt_error(buf, &perr)) {
                if (why) snprintf(why, whycap, "companion recusou: errno %d (%s)", perr, strerror(perr));
                total = -1;
            } else if (atol(buf) < 0) {
                if (why) snprintf(why, whycap, "resposta de conteudo invalida");
                total = -1;
            } else {
                total = atol(buf);
            }
            if (total >= 0 && (size_t)total >= cap) {
                if (why) snprintf(why, whycap, "conteudo maior que o buffer (%ld)", total);
                total = -1;
            }
            if (total > 0) {
                // 2a fase: ler ATÉ total bytes de fato chegarem — a versao
                // anterior copiava `total` bytes a partir do que JÁ estava no
                // buffer, mesmo faltando (copia de lixo) e deixava o resto
                // preso no socket, envenenando o pedido SEGUINTE.
                size_t have = used - head;
                while (have < (size_t)total) {
                    ssize_t r = read(sock, buf + head + have, (size_t)total - have);
                    if (r < 0) {
                        if (errno == EINTR) continue;
                        if (why) snprintf(why, whycap, "conteudo incompleto em %ds", BC_REQ_TIMEOUT_SEC);
                        total = -1;
                        break;
                    }
                    if (r == 0) {
                        if (why) snprintf(why, whycap, "companion fechou no meio do conteudo");
                        total = -1;
                        break;
                    }
                    have += (size_t)r;
                }
                if (total > 0) {
                    memcpy(out, buf + head, (size_t)total);
                    out[total] = '\0';
                }
            }
        }
    }
    struct timeval off = { .tv_sec = 0, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &off, sizeof(off));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &off, sizeof(off));
    if (io != NULL) pthread_mutex_unlock(io);
    return (int)total;
}

// ---- implementação compartilhada: leitor de "nomes + total no fim" -------
//
// O mesmo formato do LS/BL: linhas de nome e o total "<n>\n" por último. A
// extração evita duplicar o parser (o WIP já provou o que duplicar causa: o
// cliente esperava o total primeiro, o servidor mandava no fim).
static inline int bc_req_read_names_and_total(int sock, char *out, size_t cap,
                                              int *names_seen, char *why, size_t whycap) {
    char buf[4096];
    size_t used = 0, used_out = 0;
    int total = -1, seen = 0;
    for (;;) {
        ssize_t r = read(sock, buf + used, sizeof(buf) - 1 - used);
        if (r < 0) {
            if (errno == EINTR) continue;
            if (why) snprintf(why, whycap, "companion nao respondeu em %ds", BC_REQ_TIMEOUT_SEC);
            break;
        }
        if (r == 0) {
            if (why) snprintf(why, whycap, "companion fechou antes do total");
            break;
        }
        used += (size_t)r;
        buf[used] = '\0';
        char *line = buf;
        bool stop = false;
        while (line != NULL && *line != '\0') {
            char *e = strchr(line, '\n');
            if (e == NULL) break;
            *e = '\0';
            bool only_digits = line[0] != '\0';
            for (char *q = line; *q != '\0'; q++) {
                if (*q < '0' || *q > '9') { only_digits = false; break; }
            }
            int perr = 0;
            if (bc_fd_parse_txt_error(line, &perr)) {
                // Recusa da família lista: "E<errno>\n" (sem o prefixo, o
                // erro seria lido como o total de nomes).
                if (why) snprintf(why, whycap, "companion recusou: errno %d (%s)", perr, strerror(perr));
                total = -1;
                out[used_out] = '\0';
                stop = true;
                break;
            }
            if (only_digits) {
                total = atoi(line);
                out[used_out] = '\0';
                stop = true;
                break;
            }
            size_t l = strlen(line);
            if (l + used_out + 2 > cap) {
                if (why) snprintf(why, whycap, "lista maior que o buffer");
                total = -1;
                stop = true;
                break;
            }
            memcpy(out + used_out, line, l);
            used_out += l;
            out[used_out++] = '\n';
            seen++;
            line = e + 1;
        }
        if (stop) break;
        if (line != NULL) {
            size_t rest = (size_t)(line - buf);
            memmove(buf, buf + rest, used - rest);
            used -= rest;
            buf[used] = '\0';
        }
        if (used >= sizeof(buf) - 1) {
            if (why) snprintf(why, whycap, "resposta de lista grande demais");
            break;
        }
    }
    if (total >= 0 && total != seen) {
        if (why) snprintf(why, whycap, "total (%d) != nomes recebidos (%d)", total, seen);
        total = -1;
    }
    if (names_seen != NULL) *names_seen = seen;
    return total;
}

// "LS <pkg>" -> "<n>\n" + n linhas "nome". Devolve n (0 = pasta vazia), ou -1
// com motivo em why. Os nomes vão um por linha em `out` (sem o total).
static inline int bc_req_ask_list(int sock, pthread_mutex_t *io, const char *pkg,
                                  char *out, size_t cap, char *why, size_t whycap) {
    if (out && cap) out[0] = '\0';
    if (why) why[0] = '\0';
    if (sock < 0) {
        if (why) snprintf(why, whycap, "sem canal de pedidos com o companion");
        return -1;
    }
    if (io != NULL) pthread_mutex_lock(io);
    struct timeval tv = { .tv_sec = BC_REQ_TIMEOUT_SEC, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    char req[BC_FD_REQ_MAX];
    ssize_t n = bc_fd_build_request(req, sizeof(req), BC_FD_PROTO, BC_FD_VERB_LS, pkg);
    int total = -1;
    if (n <= 0) {
        if (why) snprintf(why, whycap, "pacote invalido para o pedido de lista");
    } else if (bc_fd_send_data(sock, req, (size_t)n) < 0) {
        if (why) snprintf(why, whycap, "companion nao recebeu o pedido de lista");
    } else {
        // MESMO leitor do "BL" (bc_req_read_names_and_total): formato de fio
        // da produção (nomes, total no fim) + recusa "E<errno>\n". Um leitor
        // único para o mesmo formato — o WIP provou o que duplicar causa.
        total = bc_req_read_names_and_total(sock, out, cap, NULL, why, whycap);
    }
    struct timeval off = { .tv_sec = 0, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &off, sizeof(off));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &off, sizeof(off));
    if (io != NULL) pthread_mutex_unlock(io);
    return total;
}

// ---- verbos da árvore Battle Cats (layout flat, confs na raiz) -----------
//
// Mesmo canal, mesmas regras: request+resposta = uma transação, timeout curto
// armado/desarmado, mutex opcional. O gate (quem pode pedir) vive no
// companion: chamador tem de SER o jogo BC (SO_PEERCRED -> packages.list).

// "BO <nome>" -> FD de um .so da árvore BC (nome validado pelos DOIS lados).
static inline int bc_req_ask_bc_so(int sock, pthread_mutex_t *io, const char *name,
                                   char *why, size_t whycap) {
    if (why) why[0] = '\0';
    if (sock < 0) {
        if (why) snprintf(why, whycap, "sem canal de pedidos com o companion");
        return -1;
    }
    if (io != NULL) pthread_mutex_lock(io);
    struct timeval tv = { .tv_sec = BC_REQ_TIMEOUT_SEC, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    char req[BC_FD_REQ_MAX];
    ssize_t n = (ssize_t)snprintf(req, sizeof(req), "%d %s %s\n", BC_FD_PROTO,
                                  BC_FD_VERB_BC_SO, name);
    int fd = -1;
    if (n <= 0 || (size_t)n >= sizeof(req)) {
        if (why) snprintf(why, whycap, "nome invalido para o pedido BO");
    } else if (bc_fd_send_data(sock, req, (size_t)n) < 0) {
        if (why) snprintf(why, whycap, "companion nao recebeu o BO: %s", strerror(errno));
    } else {
        char payload[BC_FD_ERR_MAX];
        ssize_t r = bc_fd_recv_fd(sock, payload, sizeof(payload), &fd);
        if (r < 0) {
            if (why) snprintf(why, whycap, "companion nao respondeu em %ds", BC_REQ_TIMEOUT_SEC);
        } else if (fd < 0) {
            int e = 0;
            if (bc_fd_parse_error(payload, &e))
                { if (why) snprintf(why, whycap, "companion recusou: errno %d (%s)", e, strerror(e)); }
            else
                { if (why) snprintf(why, whycap, "resposta sem fd e sem erro"); }
        }
    }
    struct timeval off = { .tv_sec = 0, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &off, sizeof(off));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &off, sizeof(off));
    if (io != NULL) pthread_mutex_unlock(io);
    return fd;
}

// "BL" -> nomes de .so da árvore BC, um por linha, total no FIM (mesmo
// formato de fio do LS; ver bc_req_ask_list).
static inline int bc_req_ask_bc_list(int sock, pthread_mutex_t *io,
                                     char *out, size_t cap, char *why, size_t whycap);

// "BT <nome>" -> conteudo de um conf da RAIZ da árvore. O nome tem de estar
// na LISTA FIXA do servidor (bc_req_root_conf_ok): o cliente nao ganha um
// "abra qualquer arquivo" — pede um dos confs que existem de propósito.
static inline int bc_req_ask_bc_conf(int sock, pthread_mutex_t *io, const char *name,
                                     char *out, size_t cap, char *why, size_t whycap);

static inline int bc_req_ask_bc_list(int sock, pthread_mutex_t *io,
                                     char *out, size_t cap, char *why, size_t whycap) {
    if (out && cap) out[0] = '\0';
    if (why) why[0] = '\0';
    if (sock < 0) {
        if (why) snprintf(why, whycap, "sem canal de pedidos com o companion");
        return -1;
    }
    if (io != NULL) pthread_mutex_lock(io);
    struct timeval tv = { .tv_sec = BC_REQ_TIMEOUT_SEC, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    char req[BC_FD_REQ_MAX];
    ssize_t n = (ssize_t)snprintf(req, sizeof(req), "%d %s\n", BC_FD_PROTO,
                                  BC_FD_VERB_BC_LIST);
    int total = -1;
    if (n <= 0 || (size_t)n >= sizeof(req)) {
        if (why) snprintf(why, whycap, "pedido BL invalido");
    } else if (bc_fd_send_data(sock, req, (size_t)n) < 0) {
        if (why) snprintf(why, whycap, "companion nao recebeu o BL");
    } else {
        total = bc_req_read_names_and_total(sock, out, cap, NULL, why, whycap);
    }
    struct timeval off = { .tv_sec = 0, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &off, sizeof(off));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &off, sizeof(off));
    if (io != NULL) pthread_mutex_unlock(io);
    return total;
}

static inline int bc_req_ask_bc_conf(int sock, pthread_mutex_t *io, const char *name,
                                     char *out, size_t cap, char *why, size_t whycap) {
    if (out && cap) out[0] = '\0';
    if (why) why[0] = '\0';
    if (sock < 0) {
        if (why) snprintf(why, whycap, "sem canal de pedidos com o companion");
        return -1;
    }
    if (io != NULL) pthread_mutex_lock(io);
    struct timeval tv = { .tv_sec = BC_REQ_TIMEOUT_SEC, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    char req[BC_FD_REQ_MAX];
    ssize_t n = (ssize_t)snprintf(req, sizeof(req), "%d %s %s\n", BC_FD_PROTO,
                                  BC_FD_VERB_BC_TXT, name);
    long total = -1;
    if (n <= 0 || (size_t)n >= sizeof(req)) {
        if (why) snprintf(why, whycap, "nome invalido para o pedido BT");
    } else if (bc_fd_send_data(sock, req, (size_t)n) < 0) {
        if (why) snprintf(why, whycap, "companion nao recebeu o BT");
    } else {
        // MESMO formato do TX: "<len>\n" + bytes — leitor completo (espera
        // os len bytes de fato chegarem; ver bc_req_ask_txt).
        char buf[16384];
        size_t used = 0;
        char *nl = NULL;
        for (;;) {
            ssize_t r = read(sock, buf + used, sizeof(buf) - 1 - used);
            if (r < 0) {
                if (errno == EINTR) continue;
                if (why) snprintf(why, whycap, "companion nao respondeu o conf em %ds", BC_REQ_TIMEOUT_SEC);
                break;
            }
            if (r == 0) {
                if (why) snprintf(why, whycap, "companion fechou antes do conf");
                break;
            }
            used += (size_t)r;
            buf[used] = '\0';
            nl = (char *)memchr(buf, '\n', used);
            if (nl != NULL) break;
            if (used >= sizeof(buf) - 1) {
                if (why) snprintf(why, whycap, "conf maior que o buffer");
                break;
            }
        }
        if (nl != NULL) {
            *nl = '\0';
            size_t head = (size_t)(nl - buf) + 1;
            int perr = 0;
            if (bc_fd_parse_txt_error(buf, &perr)) {
                if (why) snprintf(why, whycap, "companion recusou: errno %d (%s)", perr, strerror(perr));
                total = -1;
            } else if (atol(buf) < 0 || (size_t)atol(buf) >= cap) {
                if (why) snprintf(why, whycap, "conf maior que o buffer");
                total = -1;
            } else {
                total = atol(buf);
                size_t have = used - head;
                while (have < (size_t)total) {
                    ssize_t r = read(sock, buf + head + have, (size_t)total - have);
                    if (r < 0) {
                        if (errno == EINTR) continue;
                        if (why) snprintf(why, whycap, "conf incompleto em %ds", BC_REQ_TIMEOUT_SEC);
                        total = -1;
                        break;
                    }
                    if (r == 0) {
                        if (why) snprintf(why, whycap, "companion fechou no meio do conf");
                        total = -1;
                        break;
                    }
                    have += (size_t)r;
                }
                if (total > 0) {
                    memcpy(out, buf + head, (size_t)total);
                    out[total] = '\0';
                }
            }
        }
    }
    struct timeval off = { .tv_sec = 0, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &off, sizeof(off));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &off, sizeof(off));
    if (io != NULL) pthread_mutex_unlock(io);
    return (int)total;
}

#ifdef __cplusplus
}
#endif

#endif // BC_REQ_CLIENT_H
