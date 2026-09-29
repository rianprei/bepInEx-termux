// test/dir_list_test.cpp — o filtro da listagem da árvore de mods, no
// caminho REAL, com FIFO e symlink DENTRO da árvore.
//
// ============================================================================
// POR QUE ESTE TESTE EXISTE (S2 da revisão do kilo em companion-followups-4)
// ============================================================================
// As duas listagens do companion (BL e mod_list) repetiam o bloco
// readdir->lstat->S_ISREG, e NENHUM teste alcançava esse código: o kilo
// removeu o S_ISREG das duas e o gate inteiro seguiu verde — fifo e
// symlink passavam a ser listados como mods. Agora o filtro é o núcleo
// bc_dir_list_regular_mods, e este teste chama o MESMO código que o
// companion chama, com uma árvore que tem DE TUDO dentro:
//
//   bom.so      arquivo regular      -> LISTADO
//   fifo.so     FIFO (mkfifo)        -> NÃO listado
//   link.so     symlink -> bom.so    -> NÃO listado
//   pasta.so/   diretório            -> NÃO listado
//   nota.txt    nem é .so           -> NÃO listado
//   .hide.so    escondido            -> NÃO listado
//
// "Não listado" É "não entregue": o loader do jogo pede por FD só o que a
// listagem manda; o pedido por nome direto morre no bc_fd_open_ro
// (O_NOFOLLOW + fstat S_ISREG, coberto pelo O1 do bc_fd_harden_test).
//
// A SABOTAGEM que este teste vigia: remover o `|| !S_ISREG(st.st_mode)` do
// núcleo faz fifo.so, link.so e pasta.so aparecerem na lista — FAIL.
// E o check estático no fim pega a outra ponta: as DUAS listagens da
// produção têm que chamar o núcleo (se alguém reescrever um opendir inline
// de volta, o companion volta a ter código de listagem sem cobertura).
//
// Compilar: g++ -std=c++17 -Wall -Wextra -Werror -I../jni dir_list_test.cpp
// (o glob *_test.cpp do verify_all cuida disto)

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "../jni/bc_dir_list.h"

namespace {

int g_fail = 0;
void check(const char *name, bool ok) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) g_fail++;
}

// emitter que junta os nomes (o mesmo contrato do bc_list_emit do companion)
struct Sink {
    std::string names;
    int seen;
};

int sink_emit(void *p, const char *name) {
    Sink *s = (Sink *)p;
    s->names += name;
    s->names += '\n';
    s->seen++;
    return 0;
}

void write_file(const std::string &path, const char *data) {
    FILE *f = fopen(path.c_str(), "wb");
    if (f == nullptr) return;
    fwrite(data, 1, strlen(data), f);
    fclose(f);
}

}  // namespace

int main() {
    printf("== dir_list: só arquivo REGULAR entra na listagem ==\n");

    char tmpl[] = "/tmp/bc-dir-list-XXXXXX";
    const char *dir = mkdtemp(tmpl);
    if (dir == nullptr) {
        printf("  [FAIL] mkdtemp da fixture\n");
        return 2;
    }
    std::string base = dir;

    write_file(base + "/bom.so", "conteudo do mod regular");
    if (mkfifo((base + "/fifo.so").c_str(), 0644) != 0) {
        printf("  [FAIL] mkfifo da fixture\n");
        return 2;
    }
    if (symlink("bom.so", (base + "/link.so").c_str()) != 0) {
        printf("  [FAIL] symlink da fixture\n");
        return 2;
    }
    mkdir((base + "/pasta.so").c_str(), 0755);
    write_file(base + "/nota.txt", "nao e mod");
    write_file(base + "/.hide.so", "escondido comeca com ponto");

    // ---- o núcleo REAL: só bom.so ----
    {
        Sink s;
        s.seen = 0;
        int total = bc_dir_list_regular_mods(base.c_str(), sink_emit, &s);
        check("total = 1 (só o regular)", total == 1 && s.seen == 1);
        check("bom.so listado", s.names == "bom.so\n");
        check("fifo.so NÃO listado", s.names.find("fifo.so") == std::string::npos);
        check("link.so (symlink) NÃO listado",
              s.names.find("link.so") == std::string::npos);
        check("pasta.so (diretório) NÃO listado",
              s.names.find("pasta.so") == std::string::npos);
        check(".hide.so NÃO listado (escondido)",
              s.names.find("hide.so") == std::string::npos);
    }
    // ---- pasta ausente: -1 (o companion responde "0\n") ----
    {
        Sink s;
        s.seen = 0;
        int total = bc_dir_list_regular_mods((base + "/nao-existe").c_str(),
                                             sink_emit, &s);
        check("pasta ausente: -1, nada emitido", total == -1 && s.seen == 0);
    }
    // ---- emitter que para no meio: o total é o que EMITIU COM SUCESSO ----
    {
        // o emitter recusa o primeiro nome (send falhou, cliente sumiu):
        // o enumeração PARA e o total fica no que foi entregue antes (zero).
        struct One {
            int n;
        } one = { 0 };
        int total = bc_dir_list_regular_mods(
            base.c_str(),
            [](void *p, const char *) -> int { (*static_cast<int *>(p))++; return -1; },
            &one);
        check("emitter que para: 1 chamado, 0 contado",
              total == 0 && one.n == 1);
    }

    // ---- a OUTRA ponta (S2 completo): a produção chama o núcleo, as DUAS --
    // listagens. Mesmo padrão da seção 4 do peercred_test: ler o fonte,
    // tirar comentário e CONTAR. Inline de opendir de volta = sem cobertura
    // de novo, e é aqui que pega.
    // O caminho do companion: o gate compila com cwd=test e __FILE__ relativo
    // (glob do verify_all), mas também pode rodar standalone do repo —
    // tento os dois.
    {
        const char *candidatos[] = {
            "../jni/companion.cpp",   // cwd=test (o caso do verify_all)
            "jni/companion.cpp",      // cwd=repo (standalone)
        };
        FILE *c = NULL;
        for (int i = 0; i < 2 && c == NULL; i++) c = fopen(candidatos[i], "r");
        if (c == nullptr) {
            check("abri jni/companion.cpp", false);
        } else {
            std::string code;
            char buf[262144];
            size_t n = fread(buf, 1, sizeof(buf) - 1, c);
            fclose(c);
            code.assign(buf, n);
            // tira comentários (o padrão do peercred_test seção 4)
            std::string limpo;
            size_t pos = 0;
            while (pos < code.size()) {
                size_t fim = code.find('\n', pos);
                if (fim == std::string::npos) fim = code.size();
                std::string linha = code.substr(pos, fim - pos);
                size_t h = linha.find_first_not_of(" \t");
                bool eh_com = (h != std::string::npos) &&
                              (linha[h] == '#' || linha.compare(h, 2, "//") == 0);
                if (!eh_com) limpo += linha + "\n";
                pos = fim + 1;
            }
            int calls = 0;
            size_t p = 0;
            while ((p = limpo.find("bc_dir_list_regular_mods(", p)) != std::string::npos) {
                calls++;
                p += 10;
            }
            char nome[96];
            snprintf(nome, sizeof(nome),
                     "as DUAS listagens da produção chamam o núcleo (achei %d)",
                     calls);
            check(nome, calls == 2);
        }
    }

    // limpeza
    std::string cmd = "rm -rf '" + base + "'";
    if (system(cmd.c_str()) != 0) {
        // fixture em /tmp: se a limpeza falhar, o tmpwatcher cuida
    }

    printf("== Resultado: %s (%d falhas) ==\n",
           g_fail == 0 ? "TODOS PASSARAM" : "HOUVE FALHAS", g_fail);
    return g_fail == 0 ? 0 : 1;
}
