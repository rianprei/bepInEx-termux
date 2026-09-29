// bc_stream_guard.h — o caminho QUENTE do socket do companion.
//
// ============================================================================
// POR QUE ISTO EXISTE (e nao e um lock no meio de uma funcao)
// ============================================================================
// g_stream_fd e COMPARTILHADO: o streaming de eventos pro Termux e os pedidos do
// loader (lista e FD do .so). Sem serializar, a linha de evento cai no meio da
// resposta do pedido e o SO_RCVTIMEO (que e POR SOCKET) vaza de um pedido para
// o outro. Entao ha um mutex em volta de (request + resposta).
//
// Mas o streaming roda na THREAD DO JOGO (um hook disparou). Com
// pthread_mutex_lock ele ficaria ate 5s esperando um pedido em curso: 5s de JOGO
// TRAVADO, sem log, sem crash — o pior modo de falha que existe, e pior que o
// mod nao carregar.
//
// Entao o caminho quente e try_lock: ocupado = o evento desta linha se perde,
// conta, e o jogo segue. Perder uma linha de log durante uma janela de 5s e
// preco justo.
//
// A funcao esta AQUI, e nao inline no main.cpp, por um motivo concreto: e a
// unica forma de o teste de host exercitar o CAMINHO REAL. Testar uma copia do
// padrao nao prova nada sobre a funcao que roda no aparelho — ja foi o que
// passou em revisoes anteriores desta branch (o teste de SCM_RIGHTS so cobria
// bc_fd_open_ro, e nao o ponto de chamada).
//
// REGRA QUE O TESTE DEVE PRESERVAR: nunca usar pthread_mutex_lock aqui. Com
// lock(), uma thread que ja segura o mutex e outra esperariam a primeira: e
// comportamento indefinido em glibc para try_lock, e trava direta para lock.
// O caminho quente NAO pode estar preso.

#ifndef BC_STREAM_GUARD_H
#define BC_STREAM_GUARD_H

#include <pthread.h>
#include <stddef.h>
// <atomic> e nao <stdatomic.h>: o unico consumidor do header e o main.cpp
// (C++), e atomic_uint existe em <atomic> como std::atomic_uint.
#include <atomic>

#ifdef __cplusplus
extern "C" {
#endif

// Envia pelo socket do companion SEM ESPERAR NUNCA.
//
//   io       — o mutex do par request+resposta
//   dropped  — contador de eventos perdidos (incrementado quando ocupado)
//   send     — o send() real, injetado para o teste nao precisar de socket
//   ctx      — contexto do send
//   data/len — o que mandar
//
// Devolve true se mandou, false se perdeu (e contou).
static inline int bc_stream_try_send(pthread_mutex_t *io, std::atomic<unsigned> *dropped,
                                     int (*send)(void *ctx, const void *data, size_t len),
                                     void *ctx, const void *data, size_t len) {
    if (io == nullptr || send == nullptr) return 0;
    if (pthread_mutex_trylock(io) != 0) {
        if (dropped != nullptr) dropped->fetch_add(1u, std::memory_order_relaxed);
        return 0;   // perdeu a linha, o jogo segue
    }
    (void)send(ctx, data, len);
    pthread_mutex_unlock(io);
    return 1;
}

#ifdef __cplusplus
}
#endif

#endif // BC_STREAM_GUARD_H
