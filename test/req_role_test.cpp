// test/req_role_test.cpp — o CAMINHO REAL da primeira linha: o leitor que a
// produção usa, o parser que a produção usa, num socketpair de verdade.
//
// ============================================================================
// POR QUE ESTE TESTE EXISTE (bug REAL no aparelho, achado pelo Codex #2)
// ============================================================================
// O canal REQ inteiro estava morto no aparelho: o read_command do companion
// lia "REQ\n", trocava o '\n' por NUL e devolvia o comprimento ANTIGO (4).
// O bc_req_role_from_line conta a linha até o '\n' com memchr — que não
// achava nada — e via 4 bytes "sem delimitador": papel UNKNOWN, o daemon
// tratava a conexão como Termux e RECUSAVA pelo UID do app ("rejected
// connection from UID=10361 (not Termux)", "0 mod(s) por pacote").
//
// Nenhum teste de host pegou porque nenhum chegava perto da LEITURA: o
// req_channel_test injeta a linha de papel como argumento. Aqui o par
// leitura+classificação é o MESMO código da produção (bc_first_line.h +
// bc_req_channel.h), sobre um socket de verdade — a única coisa "fake" é
// o par de soquetes, que é o que um socket é.
//
// A SABOTAGEM que este teste vigia: devolver o comprimento pré-alteração
// (o bug original — `return used` em vez de descontar o '\n' trocado por
// NUL) faz o papel virar UNKNOWN e o teste FALHAR. Foi exatamente o que o
// aparelho viu.
//
// Compilar: g++ -std=c++17 -Wall -Wextra -Werror -I../jni req_role_test.cpp
// (o glob *_test.cpp do verify_all cuida disto)

#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

#include "../jni/bc_first_line.h"
#include "../jni/bc_req_channel.h"

namespace {

int g_fail = 0;
void check(const char *name, bool ok) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) g_fail++;
}

// Lê a primeira linha de um socketpair e devolve (n, role). O leitor é o
// bc_read_first_line REAL (o que o companion chama via read_command) e o
// classificador é o bc_req_role_from_line REAL — o mesmo par da produção.
bool role_of(const char *what, ssize_t *out_n, bc_req_role *out_role,
             char *buf, size_t cap) {
    int sp[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) != 0) {
        check(what, false);
        return false;
    }
    if (write(sp[1], what, strlen(what)) != (ssize_t)strlen(what)) {
        close(sp[0]); close(sp[1]);
        check(what, false);
        return false;
    }
    close(sp[1]);  // o "cliente" falou e calou
    ssize_t n = bc_read_first_line(sp[0], buf, (int)cap);
    *out_n = n;
    *out_role = bc_req_role_from_line(buf, n);
    close(sp[0]);
    return true;
}

}  // namespace

int main() {
    printf("== req_role: a primeira linha REAL (leitor+parser da produção) ==\n");

    // ── O CASO DO APARELHO: "REQ\n" tem de virar papel REQ ───────────────
    {
        char buf[64];
        ssize_t n = -7;
        bc_req_role r = BC_ROLE_UNKNOWN;
        if (role_of("REQ\n", &n, &r, buf, sizeof(buf))) {
            check("\"REQ\\n\" -> n=3 (comprimento coerente com o NUL)", n == 3);
            check("\"REQ\\n\" -> papel BC_ROLE_REQ (o canal que estava morto no aparelho)",
                  r == BC_ROLE_REQ);
            check("...e o buffer é \"REQ\" NUL-terminado",
                  memcmp(buf, "REQ", 3) == 0 && buf[3] == '\0');
        }
    }
    // ── a linha em CHUNKS: o read byte-a-byte não pode truncar ────────────
    {
        int sp[2];
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) != 0) {
            check("socketpair chunks", false);
        } else {
            // um byte por write — chunks TCP são o caso real do loop
            const char *bytes = "REQ\n";
            bool sent = true;
            for (int i = 0; i < 4 && sent; i++)
                sent = write(sp[1], bytes + i, 1) == 1;
            close(sp[1]);
            char buf[64];
            ssize_t n = bc_read_first_line(sp[0], buf, sizeof(buf));
            bc_req_role r = bc_req_role_from_line(buf, n);
            close(sp[0]);
            check("\"REQ\\n\" em 4 chunks -> BC_ROLE_REQ (loop byte-a-byte)",
                  sent && n == 3 && r == BC_ROLE_REQ);
        }
    }
    // ── o CONTRA-caso: linha de Termux não vira REQ ──────────────────────
    {
        char buf[64];
        ssize_t n = -7;
        bc_req_role r = BC_ROLE_REQ;  // lixo de propósito: tem que mudar
        if (role_of("status\n", &n, &r, buf, sizeof(buf))) {
            check("\"status\\n\" -> n=6, e NÃO é BC_ROLE_REQ (conexão Termux segue Termux)",
                  n == 6 && r != BC_ROLE_REQ);
        }
    }
    // ── STREAM também conta com o '\n' descontado ─────────────────────────
    {
        char buf[64];
        ssize_t n = -7;
        bc_req_role r = BC_ROLE_UNKNOWN;
        if (role_of("STREAM\n", &n, &r, buf, sizeof(buf))) {
            check("\"STREAM\\n\" -> n=6, papel BC_ROLE_STREAM",
                  n == 6 && r == BC_ROLE_STREAM);
        }
    }
    // ── linha SEM '\n' + EOF: o que chegou, sem inventar delimitador ─────
    {
        char buf[64];
        ssize_t n = -7;
        bc_req_role r = BC_ROLE_UNKNOWN;
        if (role_of("REQ", &n, &r, buf, sizeof(buf))) {
            // EOF: o leitor devolve 3 (o que chegou). O parser, sem '\n' no
            // buffer, julga a linha inteira — e "REQ" exata É o papel REQ:
            // o comportamento de antes do bug, inalterado.
            check("\"REQ\" + EOF -> n=3 (EOF devolve o que chegou)",
                  n == 3);
        }
    }
    // ── "REQxyz\n": linha EXATA, não prefixo — não é papel REQ ───────────
    {
        char buf[64];
        ssize_t n = -7;
        bc_req_role r = BC_ROLE_REQ;  // lixo: tem que NÃO ser REQ
        if (role_of("REQxyz\n", &n, &r, buf, sizeof(buf))) {
            check("\"REQxyz\\n\" NÃO é BC_ROLE_REQ (linha exata, não prefixo)",
                  n == 6 && r != BC_ROLE_REQ);
        }
    }

    printf("== Resultado: %s (%d falhas) ==\n",
           g_fail == 0 ? "TODOS PASSARAM" : "HOUVE FALHAS", g_fail);
    return g_fail == 0 ? 0 : 1;
}
