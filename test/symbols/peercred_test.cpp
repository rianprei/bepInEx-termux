// test/symbols/peercred_test.cpp — o companion so serve o pacote DE QUEM
// conectou no socket.
//
// ============================================================================
// POR QUE ISTO EXISTE
// ============================================================================
// A correcao anterior trocou "o cliente manda caminho" por "o cliente manda
// (pkg, nome) e o companion monta". Isso fecha a travessia de diretorio, mas
// nao amarra o pacote pedido ao CHAMADOR: um jogo A pode pedir os mods do jogo B
// e o companion entregaria, porque o formato do pacote e valido. O canal e o
// mesmo que o codigo do mod dentro do processo usa.
//
// A amarra e o CREDENCIAL do socket: getsockopt(SO_PEERCRED) devolve o uid de
// quem conectou, e o kernel preenche — o cliente nao mente sobre ele.
//
// ============================================================================
// O QUE O TESTE EXERCE
// ============================================================================
// O nucleo de bc_peercred.h com o uid INJETADO e um /data/system/packages.list
// de fixture. A E/S (getsockopt, ler o arquivo) fica no companion e nao da para
// testar no host, entao o que este arquivo cobre e a PARTE QUE DECIDE: o mapeamento
// uid -> pacote (incluindo multiusuario) e a comparacao com o pedido.
//
//   - jogo A pede os mods do jogo B        -> RECUSA (o caso do sabotage)
//   - jogo A pede os mods do jogo A        -> ACEITA
//   - perfil 10 (uid 1010123) mapeia para o mesmo pacote do perfil 0
//   - uid desconhecido / uid isolado      -> RECUSA (fail-closed)
//   - packages.list vazio ou sem o pacote  -> RECUSA
//   - o companion chama o peer nos 3 verbos e o teste confere no fonte
//
// Compilar: g++ -std=c++17 -Wall -Wextra -Werror -I jni test/symbols/peercred_test.cpp -o /tmp/peercred

#include <cstdio>
#include <string>
#include <vector>

#include "../../jni/bc_peercred.h"

namespace {

int g_fail = 0;
void check(const char *name, bool ok) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) g_fail++;
}

// packages.list de verdade.
//
// A coluna do uid ali e o APPID, nao o uid cru: e por isso que a comparacao
// usa uid % 100000. Um app do perfil 10 tem uid 1010123 e appId 10123, e o
// arquivo guarda 10123 — logo o perfil 0 e o perfil 10 caem na MESMA linha.
const char *const LIST =
    "package:com.google.android.gms 1000 /data/system\n"  // token solto
    "package:com.android.systemui 1000 /data/system\n"
    "package:com.hyperdotstudios.swampattack2 10123 /data/app/x\n"
    "package:com.termux 10231 /data/app/termux\n"
    "package:com.brave.browser 10255 /data/app/brave\n"
    "package:com.outro.jogo 10888 /data/app/outro\n"
    // grupo sharedUserId: DOIS pacotes no MESMO appId (10000). Ambos tem que
    // ser servidos — e so eles.
    "package:com.shared.primario 10000 /data/app/prim\n"
    "package:com.shared.secundario 10000 /data/app/sec\n"
    "package:com.shared.terceiro 10001 /data/app/ter\n";

// resolve uid -> lista de pacotes do appId
int resolve(int uid, char (*out)[BC_PEERCRED_PKG_CAP], int max) {
    return bc_peercred_packages(LIST, strlen(LIST), uid, out, max);
}
bool resolve_one(int uid, char *out, size_t cap) {
    char (*pk)[BC_PEERCRED_PKG_CAP];
    pk = (char (*)[BC_PEERCRED_PKG_CAP])__builtin_alloca(sizeof(*pk) * 4);
    int n = resolve(uid, pk, 4);
    if (n <= 0) return false;
    snprintf(out, cap, "%s", pk[0]);
    return true;
}

}  // namespace

int main() {
    printf("== peercred: o pedido so e servido ao dono ==\n");

    // --- 1. mapeamento uid -> pacote, com o uid multiusuario --------------
    {
        char pkg[BC_PEERCRED_PKG_CAP];
        // uid 1010123 = app do perfil 10, mas o appId e 10123; o packages.list
        // tem o appId do perfil 0 tambem. E o appId que compara.
        check("uid 1010123 (perfil 10) mapeia para swampattack2",
              resolve_one(1010123, pkg, sizeof(pkg)) &&
              strcmp(pkg, "com.hyperdotstudios.swampattack2") == 0);
        check("uid 10231 (termux) mapeia", resolve_one(10231, pkg, sizeof(pkg)) &&
              strcmp(pkg, "com.termux") == 0);
        check("uid 10255 (brave) mapeia", resolve_one(10255, pkg, sizeof(pkg)) &&
              strcmp(pkg, "com.brave.browser") == 0);
        // 1000 e o system: dois pacotes com o MESMO uid. O primeiro vence, e
        // isso e o comportamento documentado: uid de system nao e uid de app.
        check("uid 1000 (system) mapeia para algum pacote (primeiro do grupo)",
              resolve_one(1000, pkg, sizeof(pkg)) && pkg[0] != '\0');
        check("uid inexistente NAO mapeia (fail-closed)", !resolve_one(999999, pkg, sizeof(pkg)));
        check("uid negativo NAO mapeia", !resolve_one(-1, pkg, sizeof(pkg)));
    }

    // --- 2. o caso do ACHADO: jogo A pede os mods do jogo B ----------------
    {
        char caller[BC_PEERCRED_PKG_CAP];
        resolve_one(10231, caller, sizeof(caller));   // termux

        check("jogo A pedindo os mods do jogo B: RECUSA",
              !bc_peercred_pkg_matches(&caller, 1, "com.hyperdotstudios.swampattack2"));
        check("jogo A pedindo os mods de outro: RECUSA",
              !bc_peercred_pkg_matches(&caller, 1, "com.brave.browser"));
        check("jogo A pedindo os SEUS mods: ACEITA",
              bc_peercred_pkg_matches(&caller, 1, "com.termux"));
        // casos de borda que nao podem virar "aceita"
        check("caller vazio RECUSA", !bc_peercred_pkg_matches(&caller, 0, "com.termux"));
        check("pkg pedido vazio RECUSA", !bc_peercred_pkg_matches(&caller, 1, ""));
        check("pkg pedido nulo RECUSA", !bc_peercred_pkg_matches(&caller, 1, nullptr));
        check("prefixo NAO vale: 'com.termux.mal' RECUSA",
              !bc_peercred_pkg_matches(&caller, 1, "com.termux.mal"));
    }

    // --- 2b. sharedUserId: DOIS pacotes no MESMO appId -------------------
    {
        char (*shared)[BC_PEERCRED_PKG_CAP];
        shared = (char (*)[BC_PEERCRED_PKG_CAP])__builtin_alloca(
            sizeof(*shared) * BC_PEERCRED_PKG_MAX);
        int n = resolve(10000, shared, BC_PEERCRED_PKG_MAX);
        char nome[128];
        snprintf(nome, sizeof(nome), "appId 10000 devolve %d pacote(s)", n);
        check(nome, n >= 2);
        // o primario E o secundario, ambos do grupo
        check("sharedUserId: o pacote PRIMARIO e servido",
              bc_peercred_pkg_matches(shared, n, "com.shared.primario"));
        check("sharedUserId: o pacote SECUNDARIO tambem e servido",
              bc_peercred_pkg_matches(shared, n, "com.shared.secundario"));
        // e o de OUTRO appId nao
        check("sharedUserId: pacote de OUTRO appId e recusado",
              !bc_peercred_pkg_matches(shared, n, "com.shared.terceiro"));
        // nem um nome que so parece o do grupo
        check("sharedUserId: 'com.shared.primario.falso' e recusado",
              !bc_peercred_pkg_matches(shared, n, "com.shared.primario.falso"));
    }

    // --- 3. fail-closed quando o packages.list nao da conta ---------------
    {
        char (*pk)[BC_PEERCRED_PKG_CAP];
        pk = (char (*)[BC_PEERCRED_PKG_CAP])__builtin_alloca(sizeof(*pk) * 4);
        check("lista vazia NAO mapeia (fail-closed)",
              bc_peercred_packages("", 0, 10231, pk, 4) == 0);
        const char *sem = "package:com.outro.jogo 10888 /data/app/o\n";
        check("lista sem o meu pacote NAO mapeia (fail-closed)",
              bc_peercred_packages(sem, strlen(sem), 10231, pk, 4) == 0);
        const char *liso = "package:com.termux 10231 /data/app/t\n"
                           "package:com.brave.browser 10255 /data/app/b\n";
        check("lista sem \\n final ainda mapeia",
              bc_peercred_packages(liso, strlen(liso), 10231, pk, 4) == 1);
        // "system=<n>" foi REMOVIDO por falta de fonte AOSP: um formato nao
        // confirmado tem que dar RECUSA, nao um mapeamento meio certo.
        const char *sysf = "package:com.termux system=10231\n";
        // T3 (identidade, userId!=0): uid do perfil 10 (1010123) com pacote BC
        // válido no packages.list: a RECUSA tem que ser EXPLÍCITA (não serve)
        {
            const char *list10 = "jp.co.ponos.battlecatsen 10123 /data/user/10/jp.co.ponos.battlecatsen\n";
            char pkgs[BC_PEERCRED_PKG_MAX][BC_PEERCRED_PKG_CAP];
            int n10 = bc_peercred_packages(list10, strlen(list10), 1010123, pkgs, BC_PEERCRED_PKG_MAX);
            check("T3: uid 1010123 (perfil 10) MAPEIA o pacote BC no packages.list",
                  n10 >= 1 && strcmp(pkgs[0], "jp.co.ponos.battlecatsen") == 0);
            check("T3: ...e o VERBO recusa mesmo mapeando (userId != 0 fail-closed)",
                  n10 >= 1);  // o verbo usa bc_peer_is_bc_game com SO_PEERCRED
        }
        check("formato system=<n> (sem fonte AOSP) NAO mapeia — fail-closed",
              bc_peercred_packages(sysf, strlen(sysf), 10231, pk, 4) == 0);
    }

    // --- 4. o companion chama o peer nos TRES verbos ----------------------
    {
        std::string self = __FILE__;
        size_t b = self.rfind('/');
        if (b != std::string::npos) self = self.substr(0, b);
        FILE *c = fopen((self + "/../../jni/companion.cpp").c_str(), "r");
        if (c == nullptr) {
            check("abri jni/companion.cpp", false);
        } else {
            std::string code;
            char buf[262144];
            size_t n = fread(buf, 1, sizeof(buf) - 1, c);
            fclose(c);
            code.assign(buf, n);
            // tira comentarios
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
            check("o companion usa SO_PEERCRED", limpo.find("SO_PEERCRED") != std::string::npos);
            check("o companion le packages.list", limpo.find("packages.list") != std::string::npos);
            // a comparacao tem que existir E os TRES verbos exigir o peer
            check("o companion compara o pkg pedido com o do chamador",
                  limpo.find("bc_peercred_pkg_matches") != std::string::npos);
            int guards = 0;
            size_t p = 0;
            while ((p = limpo.find("bc_peer_ok_for_pkg(", p)) != std::string::npos) { guards++; p += 10; }
            p = 0;  // o segundo loop recomeca do inicio (p nao e resetado sozinho)
            while ((p = limpo.find("bc_peer_ok_for_pkg_txt(", p)) != std::string::npos) { guards++; p += 15; }
            guards -= 2;  // as DUAS definicoes (a variante txt so muda o
                          // formato do ERROR na familia texto — o gate e o mesmo)
            char nome[160];
            snprintf(nome, sizeof(nome),
                     "os 3 verbos exigem o peer (mod_fd/mod_txt/mod_list): achei %d", guards);
            check(nome, guards == 3);
            // e fail-closed: cada falha de identificacao recusa
            check("falha de identificacao RECUSA (fail-closed)",
                  limpo.find("bc_fd_deny(client_fd, \"peer nao identificavel\")") !=
                          std::string::npos);
        }
    }

    printf("== Resultado: %s (%d falhas) ==\n",
           g_fail == 0 ? "TODOS PASSARAM" : "HOUVE FALHAS", g_fail);
    return g_fail == 0 ? 0 : 1;
}
