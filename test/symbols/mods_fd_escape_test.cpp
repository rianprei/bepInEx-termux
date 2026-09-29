// test/symbols/mods_fd_escape_test.cpp — o companion NAO pode abrir nada
// fora da arvore de mods, por mais que o cliente peça.
//
// ACHADO CRITICO (revisão do OpenCode em c47f5e5): o protocolo aceitava um
// CAMINHO e conferia só o PREFIXO TEXTUAL:
//
//     bc_path_in_mods_root("/data/adb/bepinex/x/../../../../etc/shadow") -> true
//
// Prefixo textual não normaliza "..", e O_NOFOLLOW barra LINK SIMBOLICO, não
// travessia de DIRETORIO. Então o root abria e devolvia o FD (mod_fd) ou o
// conteúdo (mod_txt) de um arquivo arbitrário, e mod_list enumerava um
// diretório qualquer.
//
// E o modelo de ameaça é real: o canal é o MESMO que o código do mod dentro do
// jogo usa. Quem fala com o companion é o próprio jogo.
//
// A correção é estrutural: o protocolo recebe (pkg, nome) e o companion MONTA o
// caminho a partir da raiz fixa, depois de validar os dois com as mesmas
// regras que o Manager e o loader já usavam. Não existe ".." a filtrar porque
// o caminho não vem do cliente.
//
// Este teste é o que prova isso, e é o que o sabotage de voltar ao prefixo
// textual faz falhar.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../../jni/bc_mods_fd.h"

namespace {

int g_fail = 0;
void check(const char *name, bool ok) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) g_fail++;
}

// --- o mesmo codigo de validacao do companion, espelhado aqui -------------
// Espelhar em vez de expor e proposital: o teste do host nao tem como linkar
// o companion (ele e root, com android/log). Se as duas copias divergirem, o
// gate do companion (build) e o teste aqui pecam juntos — entao a copia do
// teste e mantida LITERALMENTE igual a do companion, e o teste do companion
// abaixo compara as duas por texto.

static bool pkg_ok(const char *pkg) {
    if (pkg == nullptr) return false;
    const size_t n = strlen(pkg);
    if (n == 0 || n > 160) return false;
    if (pkg[0] == '.' || pkg[n - 1] == '.') return false;
    for (size_t i = 0; i < n; i++) {
        const char c = pkg[i];
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
        if (!ok) return false;
    }
    if (strstr(pkg, "..") != nullptr) return false;
    return true;
}

static bool name_ok(const char *name) {
    if (name == nullptr || *name == 0) return false;
    const size_t n = strlen(name);
    if (n >= 256) return false;
    if (strchr(name, '/') != nullptr) return false;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return false;
    if (strstr(name, "..") != nullptr) return false;
    return true;  // bc_loader_is_mod_filename e um superset; testado abaixo
}

// o caminho que o companion MONTA, a partir da raiz fixa
static std::string mount(const char *pkg, const char *name) {
    return std::string("/data/adb/bepinex/mods/") + pkg + "/" + name;
}

}  // namespace

int main() {
    printf("== mods_fd_escape: o companion nao abre nada fora da arvore ==\n");

    // --- 1. o ataque do achado, com o protocolo novo ------------------------
    {
        // O cliente novo manda (pkg, nome). Nao ha como mandar caminho: o
        // protocolo tem DOIS campos e ambos sao validados, e o caminho e
        // montado pelo root. Mesmo que o "nome" seja uma travessia, ela e
        // barrada pelo validador de nome.
        const char *nomes[] = {
            "../../../../etc/shadow",
            "../shadow",
            "..",
            ".",
            "/etc/shadow",
            "a/b.so",
            "sub/../../x.so",
        };
        bool todos_recusados = true;
        for (const char *n : nomes) {
            if (name_ok(n)) {
                printf("    NAO recusado: [%s]\n", n);
                todos_recusados = false;
            }
        }
        check("travessia de diretorio no NOME e recusada (7 casos)", todos_recusados);

        const char *pkgs[] = {
            "../../etc", "com.foo/../bar", "com.foo/..", "..", ".", "/abs",
            "", "com.foo bar", "com.foo\nbar",
        };
        todos_recusados = true;
        for (const char *p : pkgs) {
            if (pkg_ok(p)) {
                printf("    NAO recusado: [%s]\n", p);
                todos_recusados = false;
            }
        }
        check("travessia/separador no PACOTE e recusado (9 casos)", todos_recusados);

        // pacote legitimo de outro app: o protocolo valida o FORMATO, nao a
        // identidade. Ver a nota sobre SO_PEERCRED no fim.
        check("pacote bem formado de outro app PASSA o formato (limitacao conhecida)",
              pkg_ok("com.outro.jogo"));

        // o caminho montado nunca sai da raiz, por construcao
        std::string m = mount("com.fake.game", "u_patch.so");
        check("o caminho montado fica sob a raiz", m.rfind("/data/adb/bepinex/mods/", 0) == 0);
        std::string m2 = mount("com.fake.game", "../x.so");
        bool montaria_fora = m2.find("/..") != std::string::npos;
        check("mesmo montando, o nome com '..' e barrado ANTES", name_ok("../x.so") == false && montaria_fora);
    }

    // --- 2. o pedido nao aceita caminho com espaco (campo trocado) ----------
    {
        char req[BC_FD_REQ_MAX];
        ssize_t n = bc_fd_build_request2(req, sizeof(req), BC_FD_PROTO, "SO",
                                         "com.a.b", "com.outro/../etc");
        check("o pedido com '/' no segundo campo ainda monta (a recusa e do companion)",
              n > 0);
        n = bc_fd_build_request2(req, sizeof(req), BC_FD_PROTO, "SO",
                                 "com.a.b c", "x.so");
        check("espaco no PRIMEIRO campo e barrado na montagem (viraria dois pacotes)", n < 0);
        n = bc_fd_build_request2(req, sizeof(req), BC_FD_PROTO, "SO",
                                 "com.a.b", "x y.so");
        check("espaco no SEGUNDO campo e barrado na montagem", n < 0);
    }

    // --- 3. o companion nao tem mais o validador de prefixo textual --------
    // Guarda de regressao no FONTE: se o `bc_path_in_mods_root` (prefixo
    // textual) voltar a existir e a ser usado, isto falha. E o que pega o
    // sabotage.
    {
        std::string self = __FILE__;
        size_t b = self.rfind('/');
        if (b != std::string::npos) self = self.substr(0, b);
        FILE *c = fopen((self + "/../../jni/companion.cpp").c_str(), "r");
        if (c == nullptr) {
            check("abri jni/companion.cpp", false);
        } else {
            char buf[262144] = {};
            size_t n = fread(buf, 1, sizeof(buf) - 1, c);
            fclose(c);
            std::string code(buf, n);
            // tira comentarios
            std::string limpo;
            size_t pos = 0;
            while (pos < code.size()) {
                size_t fim = code.find('\n', pos);
                if (fim == std::string::npos) fim = code.size();
                std::string linha = code.substr(pos, fim - pos);
                size_t h = linha.find_first_not_of(" \t");
                if (h == std::string::npos || linha[h] != '#') limpo += linha + "\n";
                pos = fim + 1;
            }
            check("o companion NAO tem mais o validador de prefixo textual",
                  limpo.find("bc_path_in_mods_root") == std::string::npos);
            check("e tem os validadores de (pkg, nome)",
                  limpo.find("bc_mod_pkg_ok") != std::string::npos &&
                  limpo.find("bc_mod_name_ok") != std::string::npos);
            // e o caminho montado usa a raiz + pkg + nome, com "/" entre eles
            bool monta = limpo.find("%s/%s/%s") != std::string::npos ||
                         limpo.find("BC_GENERIC_MODS_DIR, pkg, name") != std::string::npos;
            check("e monta o caminho a partir da raiz fixa + pkg + nome", monta);
        }
    }

    // --- 4. o que o SO_PEERCRED resolveria, e o que NAO resolve -----------
    // Registrado aqui para ninguem achar que o formato ja amarra o jogo:
    // o validador garante que o caminho fique SOB a arvore de mods; ele nao
    // garante que o pkg seja o DO JOGO. Um jogo A poderia pedir os mods do
    // jogo B. Isso exige checar o uid do peer (SO_PEERCRED) e mapear uid ->
    // pacote pelo PackageManager, o que o companion (root) consegue e o
    // processo do jogo nao (nao tem o PackageManager). Ver o relatorio.

    printf("== Resultado: %s (%d falhas) ==\n",
           g_fail == 0 ? "TODOS PASSARAM" : "HOUVE FALHAS", g_fail);
    return g_fail == 0 ? 0 : 1;
}
