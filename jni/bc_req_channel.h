// bc_req_channel.h — o canal de PEDIDOS entre o jogo e o companion.
//
// ============================================================================
// POR QUE EXISTE (achado do OpenCode em c242d5f, BLOQUEANTE)
// ============================================================================
// O jogo pede .so por FD e conf/allowlist por CONTEUDO, e esse pedido precisa do
// companion (root). O envio estava no g_stream_fd — o socket do STREAMING DE
// EVENTOS pro Termux. E o unico leitor desse socket (companion.cpp,
// stream_socket_reader) faz BROADCAST: nao despacha mod_fd/mod_txt/mod_list. Os
// handlers so eram alcancados pelo termux_accept_loop, que o JOGO nunca conecta.
//
// Resultado em runtime: todo pedido dava timeout de 5s e nenhum mod carregava.
// Os testes passavam porque testavam as PECAS (o cliente e os validadores) e nao
// o FIO — o pedido nunca chegava em ninguem.
//
// A correcao e um SEGUNDO connectCompanion(), aberto no specialize, com uma
// primeira linha identificando o PAPEL. O stream volta a ser so stream, e o
// mutex do socket compartilhado deixa de ser necessario.
//
// POR QUE NAO PODE SER "um connectCompanion POR PEDIDO":
//   jni/zygisk.hpp:211  "All API methods will stop working after
//                        post[XXX]Specialize as Zygisk will be unloaded from
//                        the specialized process afterwards."
//   jni/zygisk.hpp:213  "This API only works in the pre[XXX]Specialize methods
//                        due to SELinux restrictions."
// E o mod e carregado DEPOIS do specialize (quando a lib do jogo mapeia), que e
// a razao de ele existir. Entao a conexao tem que ser aberta no specialize e
// mantida — nao aberta por pedido.

#ifndef BC_REQ_CHANNEL_H
#define BC_REQ_CHANNEL_H

#ifdef __cplusplus
extern "C" {
#endif

// A primeira linha no socket novo, antes de qualquer pedido. Sem ela o
// companion nao sabe se esse socket e o de pedidos ou o de streaming.
#define BC_REQ_ROLE_LINE "REQ"
#define BC_STREAM_ROLE_LINE "STREAM"
#define BC_REQ_HELLO "REQ\n"
#define BC_STREAM_HELLO "STREAM\n"

#ifdef __cplusplus
}
#endif

#endif // BC_REQ_CHANNEL_H
