// bc_first_line.h — a primeira linha de uma conexão do companion, lida e
// NUL-terminada com o COMPRIMENTO coerente com o buffer.
//
// ============================================================================
// POR QUE ESTE HEADER EXISTE (bug REAL no aparelho, achado pelo Codex #2)
// ============================================================================
// O read_command do companion lia "REQ\n" (4 bytes), trocava o '\n' por NUL
// — para o strcmp da frente funcionar — e devolvia o comprimento ANTIGO
// (4). O parser de papel (bc_req_channel.h: bc_req_role_from_line) conta a
// linha ATÉ o '\n' com memchr: o '\n' não existia mais, ele viu 4 bytes sem
// delimitador, comparou com "REQ" (3) e devolveu BC_ROLE_UNKNOWN. O daemon
// tratava a conexão como Termux e RECUSAVA pelo UID do app — log real do
// aparelho: "rejected connection from UID=10361 (not Termux)", "0 mod(s)
// por pacote". O canal REQ inteiro não funcionava.
//
// Nenhum teste de host pegou porque o req_channel_test exercita o
// serve_connection e handlers substituídos: NUNCA passou pela leitura real
// da primeira linha. Agora a leitura mora AQUI — pura, POSIX, roda no host
// — e o INVARIANTE é o contrato do header: o comprimento devolvido
// descreve o buffer COMO ELE ESTÁ (linha sem o '\n', NUL-terminada logo
// após). O teste de host (test/req_role_test.cpp) chama ISTO num socketpair
// de verdade junto com o parser REAL: leitura e classificação são o MESMO
// código da produção, e a classe do bug não volta sem o gate vermelho.
#ifndef BC_FIRST_LINE_H
#define BC_FIRST_LINE_H

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
#include <unistd.h>

// Lê UMA linha de `fd`, byte a byte, até '\n' (comandos chegando em chunks
// não truncam), EOF, EAGAIN (SO_RCVTIMEO) ou buffer cheio.
//
// Devolve o COMPRIMENTO DA LINHA (sem o '\n'); `buf` sai NUL-terminado logo
// após a linha. -1 em erro de leitura, com o errno preservido — quem chama
// decide (o companion loga). EOF no meio devolve o que chegou (>= 0).
//
// O INVARIANTE (o bug que este header fecha): o retorno e o buffer são
// COERENTES. Se o '\n' foi trocado por NUL, o comprimento desconta ele —
// nunca devolver o comprimento de um buffer que já não existe.
static inline ssize_t bc_read_first_line(int fd, char *buf, size_t cap) {
    size_t used = 0;
    while (used + 1 < cap) {
        ssize_t n = read(fd, buf + used, 1);
        if (n < 0) {
            if (errno == EINTR) continue;   // repete em sinal
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;  // timeout
            return -1;                      // erro real: errno preservado
        }
        if (n == 0) break;                  // EOF (peer fechou)
        char c = buf[used];
        used++;
        if (c == '\n') break;               // fim de mensagem
    }
    // NUL-termina SEMPRE depois do lido; se a linha veio com '\n', o '\n'
    // vira NUL (para o strcmp direto) e o comprimento devolvido DESCONTA
    // esse byte — a linha que o parser vê é exatamente a que está no buffer.
    bool terminated = used > 0 && buf[used - 1] == '\n';
    buf[used] = '\0';
    if (terminated) buf[used - 1] = '\0';
    return terminated ? (ssize_t)(used - 1) : (ssize_t)used;
}

#endif  // BC_FIRST_LINE_H
