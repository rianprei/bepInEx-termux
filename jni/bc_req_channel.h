// bc_req_channel.h — o canal de PEDIDOS entre o jogo e o companion.
//
// ============================================================================
// POR QUE EXISTE (achado do OpenCode em c242d5f, BLOQUEANTE)
// ============================================================================
// O jogo pede .so por FD e conf/allowlist por CONTEUDO, e esse pedido precisa do
// companion (root). Os pedidos iam pelo g_stream_fd — o socket do STREAMING DE
// EVENTOS pro Termux, cujo unico leitor (companion.cpp, stream_socket_reader)
// faz BROADCAST: nao despacha nada. Os handlers so eram alcancados pelo
// termux_accept_loop, que o JOGO nunca conectava. Em runtime: todo pedido dava
// timeout de 5s e NENHUM mod carregava — e a suite inteira passava, porque
// testava as PECAS e nao o FIO.
//
// ============================================================================
// ONDE O CANAL MORA (decisao de transporte, com evidencia)
// ============================================================================
// O socket do canal de pedidos e a CONEXAO do jogo ao @bc_companion (o mesmo
// abstract socket que o Termux ja usa — comprovado em rodadas de device), e nao
// um segundo connectCompanion():
//
//   1. SO_PEERCRED so diz algo UTIL la: no connectCompanion() a conexao e
//      aberta em preAppSpecialize, quando o processo ainda e filho do zygote
//      com uid 0 (o setuid do specialize vem DEPOIS), e as credenciais do par
//      ficam congeladas nesse instante — o peer seria SEMPRE uid 0. O gate
//      por pacote (bc_peer_ok_for_pkg: SO_PEERCRED -> /data/system/packages.list
//      -> pacote do chamador) recusaria o PROPRI JOGO (fail-closed contra o
//      uid 0 que nao mapeia a pacote nenhum). No @bc_companion, aberto
//      DEPOIS do specialize, o SO_PEERCRED devolve o uid REAL do app e o
//      gate funciona — o mesmo gate, o mesmo packages.list, a mesma regra
//      "o pedido so e servido se o pkg for do chamador".
//
//   2. O connectCompanion() e servido pelo companion_handler, que daemoniza.
//      Uma SEGUNDA conexao viraria um segundo daemon: o accept loop dele
//      morre no bind EADDRINUSE (o primeiro segura @bc_companion) e leva a
//      thread de pedidos junto — o canal nasceria morto.
//
// A PRIMEIRA LINHA da conexao declara o PAPEL ("REQ"), como no desenho original
// do kilo; a diferenca e so o transporte. O stream continua sendo so stream.
//
// ============================================================================
// POR QUE NAO PODE SER "um connectCompanion POR PEDIDO":
//   jni/zygisk.hpp:211  "All API methods will stop working after
//                        post[XXX]Specialize as Zygisk will be unloaded from
//                        the specialized process afterwards."
//   jni/zygisk.hpp:213  "This API only works in the pre[XXX]Specialize methods
//                        due to SELinux restrictions."
// E os mods sao carregados DEPOIS do specialize. O @bc_companion nao tem essa
// restricao: e um socket abstract comum do dominio Unix.

#ifndef BC_REQ_CHANNEL_H
#define BC_REQ_CHANNEL_H

#include <stddef.h>
#include <string.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

// A primeira linha no socket novo, antes de qualquer pedido. Sem ela o
// companion nao sabe se essa conexao e de pedidos.
#define BC_REQ_ROLE_LINE "REQ"
#define BC_STREAM_ROLE_LINE "STREAM"
#define BC_REQ_HELLO "REQ\n"
#define BC_STREAM_HELLO "STREAM\n"

// Nome do socket abstract do companion — fonte única: o daemon faz bind
// (companion.cpp) e o jogo conecta (main.cpp) com o MESMO literal.
#define BC_COMPANION_SOCKET_NAME "bc_companion"

typedef enum {
    BC_ROLE_UNKNOWN = 0,
    BC_ROLE_REQ,
    BC_ROLE_STREAM,
} bc_req_role;

// Classifica a PRIMEIRA LINHA de uma conexao nova. Puro: o companion chama no
// accept do @bc_companion, o teste de host chama no serve do socketpair — o
// MESMO codigo decide nos dois lados.
//
// Linha EXATA (ate o '\n'), nao prefixo: strncmp(prefixo) aceitaria "REQxyz"
// como papel de pedidos. Uma linha de log do stream comeca com "[", entao nao
// ha colisao na pratica — mas o parser nao depende disso.
static inline bc_req_role bc_req_role_from_line(const char *buf, ssize_t n) {
    if (buf == NULL || n <= 0) return BC_ROLE_UNKNOWN;
    const char *nl = (const char *)memchr(buf, '\n', (size_t)n);
    size_t len = (nl != NULL) ? (size_t)(nl - buf) : (size_t)n;
    if (len == strlen(BC_REQ_ROLE_LINE) &&
        memcmp(buf, BC_REQ_ROLE_LINE, len) == 0) {
        return BC_ROLE_REQ;
    }
    if (len == strlen(BC_STREAM_ROLE_LINE) &&
        memcmp(buf, BC_STREAM_ROLE_LINE, len) == 0) {
        return BC_ROLE_STREAM;
    }
    return BC_ROLE_UNKNOWN;
}

#ifdef __cplusplus
}
#endif

#endif // BC_REQ_CHANNEL_H
