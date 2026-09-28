// test/symbols/req_channel_test.cpp — PONTA A PONTA: o cliente REAL de um lado,
// o despachante REAL do outro, num socketpair de verdade.
//
// ============================================================================
// POR QUE ESTE TESTE EXISTE (achado BLOQUEANTE do OpenCode em c242d5f)
// ============================================================================
// Os testes anteriores mediam as PECAS: o cliente montava a request certo, os
// validadores recusavam certo, o SO_PEERCRED mapeava certo. E nada disso
// importava, porque o FIO nao existia: os pedidos iam para o socket de
// STREAMING (cujo unico leitor faz broadcast) e nenhum mod carregava — com a
// suite inteira verde. Um teste que so verifica pecas nao verifica o sistema.
//
// Aqui TUDO que dá para exercitar de verdade é de verdade:
//   - o CLIENTE é bc_req_client.h — o MESMO código que o jogo usa no device
//     (bc_req_ask_so/txt/list, com os mesmos build_request/recv_fd/parse_error
//     e o mesmo mutex por transação);
//   - o PAPEL da conexão é bc_req_role_from_line — o MESMO parser que o accept
//     loop do companion usa para adotar a conexão REQ;
//   - o DESPACHO é bc_req_dispatch_one — o MESMO núcleo que o companion
//     chama com os handlers de root;
//   - só a ponta "root" (handlers) é substituída, porque precisa de UID 0 —
//     e os substitutos seguem o MESMO contrato dos reais (validação de nome,
//     erro como linha de errno, lista pelo filtro de .so).
//
// O alarm(2) é o ponto: se o despacho sumir, o pedido fica sem resposta e o
// teste FALHA em 2s em vez de virar um build pendurado — que seria o mesmo
// modo de falha que a revisão achou (timeout de 5s no jogo, nenhum mod).
//
// Fixturas NUL-safe: o WIP criava o .so com fputs() de uma string com \0 no
// meio — fputs para no NUL e o arquivo saía com 7 bytes. Agora os bytes são
// escritos com fwrite() de comprimento exato e a checagem é memcmp() byte a
// byte — mais forte que strstr, que não enxerga nada depois de um NUL.
//
// Compilar: g++ -std=c++17 -Wall -Wextra -Werror -D_GNU_SOURCE -I jni
//   test/symbols/req_channel_test.cpp -o /tmp/req_channel_test

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "../../jni/bc_mods_fd.h"
#include "../../jni/bc_req_channel.h"
#include "../../jni/bc_req_client.h"
#include "../../jni/bc_req_dispatch.h"
#include "../../jni/bc_loader.h"

namespace {

int g_fail = 0;
void check(const char *name, bool ok) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) g_fail++;
}

void on_alarm(int) {
    static const char msg[] =
        "  [FAIL] ALARME: o pedido ficou pendurado — o despacho nao respondeu\n";
    ssize_t r = write(2, msg, sizeof(msg) - 1);
    (void)r;
    _exit(3);
}

const char *g_root = nullptr;

// Identidade do CHAMADOR desta conexão — no device vem do SO_PEERCRED
// (kernel, não spoofável); no host é injetada por conexão. Os gates dos
// handlers de teste espelham o CONTRATO dos de produção: bc_peer_ok_for_pkg
// ("o pedido só é servido se o pkg for do chamador") e bc_peer_is_bc_game
// ("verbos BC só para o jogo BC").
static __thread const char *g_caller = nullptr;

static void deny_caller(int fd, const char *what) {
    char e[BC_FD_ERR_MAX];
    ssize_t n = bc_fd_build_error(e, sizeof(e), EACCES);
    if (n > 0) bc_fd_send_data(fd, e, (size_t)n);
    fprintf(stderr, "  [gate] %s: recusado (chamador=%s)\n", what,
            g_caller ? g_caller : "?");
}

// Recusa da família TEXTO/LISTA (t_txt/t_list/t_bc_*): "E<errno>\n" — o
// contrato que a produção usa (bc_fd_deny_txt), sem o qual o cliente
// parsearia a recusa como comprimento.
static void deny_caller_txt(int fd, const char *what) {
    char e[BC_FD_ERR_MAX];
    ssize_t n = bc_fd_build_txt_error(e, sizeof(e), EACCES);
    if (n > 0) bc_fd_send_data(fd, e, (size_t)n);
    fprintf(stderr, "  [gate] %s: recusado (chamador=%s)\n", what,
            g_caller ? g_caller : "?");
}

// --- handlers de teste: MESMO contrato dos handlers de root ---------------
// (só a ponta que precisa de UID 0 é substituída; produção: companion.cpp
// handle_mod_fd/handle_mod_txt/handle_mod_list/handle_bc_* — que ainda por
// cima gateiam por SO_PEERCRED -> packages.list -> pacote do chamador,
// dispositivo a mais que o host não tem.)

void t_so(int fd, const char *pkg, const char *name) {
    if (g_caller == nullptr || strcmp(pkg, g_caller) != 0) {
        deny_caller(fd, "mod_fd");
        return;
    }
    char path[4096];
    snprintf(path, sizeof(path), "%.900s/%.400s/%.400s", g_root, pkg, name);
    int f = open(path, O_RDONLY);
    if (f < 0) {
        char e[BC_FD_ERR_MAX];
        ssize_t n = bc_fd_build_error(e, sizeof(e), errno);
        if (n > 0) bc_fd_send_data(fd, e, (size_t)n);
        return;
    }
    // 1 byte de payload, NAO 0: send de 0 byte em SOCK_STREAM chega como EOF.
    static const char kAck = 'F';
    bc_fd_send(fd, f, &kAck, 1);
    close(f);
}

void t_txt(int fd, const char *pkg, const char *name) {
    if (g_caller == nullptr || strcmp(pkg, g_caller) != 0) {
        deny_caller_txt(fd, "mod_txt");
        return;
    }
    char path[4096];
    snprintf(path, sizeof(path), "%.900s/%.400s/%.400s", g_root, pkg, name);
    int f = open(path, O_RDONLY);
    if (f < 0) {
        char e[BC_FD_ERR_MAX];
        ssize_t n = bc_fd_build_txt_error(e, sizeof(e), errno);
        if (n > 0) bc_fd_send_data(fd, e, (size_t)n);
        return;
    }
    char buf[512];
    ssize_t n = read(f, buf, sizeof(buf));
    close(f);
    if (n < 0) {
        char e[BC_FD_ERR_MAX];
        ssize_t k = bc_fd_build_error(e, sizeof(e), errno);
        if (k > 0) bc_fd_send_data(fd, e, (size_t)k);
        return;
    }
    char head[32];
    int hn = snprintf(head, sizeof(head), "%zd\n", n);
    bc_fd_send_data(fd, head, (size_t)hn);
    bc_fd_send_data(fd, buf, (size_t)n);
}

void t_list(int fd, const char *pkg) {
    if (g_caller == nullptr || strcmp(pkg, g_caller) != 0) {
        deny_caller_txt(fd, "mod_list");
        return;
    }
    // Mesmo formato do handle_mod_list: um .so por linha, filtrado por
    // bc_loader_is_mod_filename, e o total no fim.
    char dir[4096];
    snprintf(dir, sizeof(dir), "%.3600s/%.400s", g_root, pkg);
    DIR *d = opendir(dir);
    if (d == nullptr) {
        bc_fd_send_data(fd, "0\n", 2);
        return;
    }
    int total = 0;
    char line[512];
    struct dirent *de;
    while ((de = readdir(d)) != nullptr) {
        if (de->d_name[0] == '.') continue;
        if (!bc_loader_is_mod_filename(de->d_name)) continue;
        char full[4096];
        int fw = snprintf(full, sizeof(full), "%.2040s/%.1000s", dir, de->d_name);
        if (fw <= 0 || (size_t)fw >= sizeof(full)) continue;
        struct stat st;
        if (lstat(full, &st) != 0 || !S_ISREG(st.st_mode)) continue;
        int n = snprintf(line, sizeof(line), "%s\n", de->d_name);
        if (n > 0 && bc_fd_send_data(fd, line, (size_t)n) < 0) break;
        total++;
    }
    closedir(d);
    snprintf(line, sizeof(line), "%d\n", total);
    bc_fd_send_data(fd, line, strlen(line));
}

// --- o serve: papel REAL (bc_req_role_from_line) + despacho REAL -----------

// Árvore BC do fixture: <root>/bc_mods/ (flat) + confs na RAIZ (<root>/),
// espelhando BC_MODS_DIR=/data/adb/bepinex/bc_mods e BC_MODS_ROOT/<conf>.
static const char kBCPkg[] = "jp.co.ponos.battlecatsen";

static bool t_caller_is_bc(void) {
    return g_caller != nullptr &&
           strstr(g_caller, "jp.co.ponos.battlecatsen") != nullptr;  // bc_path_is_bc
}

// "BO": gate = o chamador É o jogo BC (bc_peer_is_bc_game na produção).
void t_bc_so(int fd, const char *name) {
    if (!t_caller_is_bc()) {
        deny_caller(fd, "BO");
        return;
    }
    if (!bc_loader_is_mod_filename(name)) {
        deny_caller(fd, "BO: nome invalido");
        return;
    }
    char path[4096];
    snprintf(path, sizeof(path), "%.2040s/bc_mods/%.1000s", g_root, name);
    int f = open(path, O_RDONLY);
    if (f < 0) {
        char e[BC_FD_ERR_MAX];
        ssize_t n = bc_fd_build_error(e, sizeof(e), errno);
        if (n > 0) bc_fd_send_data(fd, e, (size_t)n);
        return;
    }
    static const char kAck = 'F';
    bc_fd_send(fd, f, &kAck, 1);
    close(f);
}

// "BL": lista da árvore BC (mesmo formato: nomes + total no fim).
void t_bc_list(int fd) {
    if (!t_caller_is_bc()) {
        deny_caller_txt(fd, "BL");
        return;
    }
    char dir[4096];
    snprintf(dir, sizeof(dir), "%.3600s/bc_mods", g_root);
    DIR *d = opendir(dir);
    if (d == nullptr) {
        bc_fd_send_data(fd, "0\n", 2);
        return;
    }
    int total = 0;
    char line[512];
    struct dirent *de;
    while ((de = readdir(d)) != nullptr) {
        if (de->d_name[0] == '.') continue;
        if (!bc_loader_is_mod_filename(de->d_name)) continue;
        char full[4096];
        int fw = snprintf(full, sizeof(full), "%.2040s/%.1000s", dir, de->d_name);
        if (fw <= 0 || (size_t)fw >= sizeof(full)) continue;
        struct stat st;
        if (lstat(full, &st) != 0 || !S_ISREG(st.st_mode)) continue;
        int n = snprintf(line, sizeof(line), "%s\n", de->d_name);
        if (n > 0 && bc_fd_send_data(fd, line, (size_t)n) < 0) break;
        total++;
    }
    closedir(d);
    snprintf(line, sizeof(line), "%d\n", total);
    bc_fd_send_data(fd, line, strlen(line));
}

// "BT": conf da RAIZ — nome da LISTA FIXA REAL (bc_req_root_conf_ok, o
// MESMO código que a produção usa). Fora da lista: EACCES.
void t_bc_conf(int fd, const char *name) {
    if (!t_caller_is_bc()) {
        deny_caller_txt(fd, "BT");
        return;
    }
    if (!bc_req_root_conf_ok(name)) {
        deny_caller_txt(fd, "BT: conf fora da lista fixa");
        return;
    }
    char path[4096];
    snprintf(path, sizeof(path), "%.2040s/%.1000s", g_root, name);
    int f = open(path, O_RDONLY);
    if (f < 0) {
        char e[BC_FD_ERR_MAX];
        ssize_t n = bc_fd_build_txt_error(e, sizeof(e), errno);
        if (n > 0) bc_fd_send_data(fd, e, (size_t)n);
        return;
    }
    char buf[512];
    ssize_t n = read(f, buf, sizeof(buf));
    close(f);
    if (n < 0) {
        char e[BC_FD_ERR_MAX];
        ssize_t k = bc_fd_build_txt_error(e, sizeof(e), errno);
        if (k > 0) bc_fd_send_data(fd, e, (size_t)k);
        return;
    }
    char head[32];
    int hn = snprintf(head, sizeof(head), "%zd\n", n);
    bc_fd_send_data(fd, head, (size_t)hn);
    bc_fd_send_data(fd, buf, (size_t)n);
}

// --- o serve: papel REAL (bc_req_role_from_line) + despacho REAL -----------

struct serve_arg { int fd; const struct bc_req_handlers *h; const char *caller; };

void *serve_loop(void *arg) {
    struct serve_arg *a = (struct serve_arg *)arg;
    g_caller = a->caller;  // o "SO_PEERCRED" do host: identidade da conexão
    // A PRIMEIRA LINHA é o papel — o MESMO parser do accept loop do
    // companion. Sem REQ, a conexão não é de pedidos: fecha.
    char hello[64];
    ssize_t hn = bc_fd_read_line(a->fd, hello, sizeof(hello));
    if (bc_req_role_from_line(hello, hn) != BC_ROLE_REQ) {
        return nullptr;
    }
    for (;;) {
        if (bc_req_dispatch_one(a->fd, a->h) < 0) break;  // -1 = EOF; 0 = recusa respondida
    }
    return nullptr;
}

}  // namespace

int main() {
    signal(SIGALRM, on_alarm);
    alarm(2);

    printf("== req_channel: cliente REAL <-> despacho REAL ==\n");

    g_root = getenv("BC_TEST_MODS_ROOT");
    if (g_root == NULL) {
        printf("  [FAIL] BC_TEST_MODS_ROOT ausente\n");
        return 1;
    }

    // arvore de mods de mentira, com um .so de verdade para abrir.
    // fwrite de comprimento EXATO: o WIP usava fputs() e a string tinha um
    // \0 no meio — o arquivo saía com 7 bytes e o teste provava nada.
    static const char kSoBytes[] =
        "\177ELF\002\001\001\000conteudo-do-mod-para-o-teste";
    static const size_t kSoLen = sizeof(kSoBytes) - 1;
    static const char kConfBytes[] = "appInit=on\n";

    char pkgdir[4096], sopath[4096], confpath[4096];
    snprintf(pkgdir, sizeof(pkgdir), "%.3900s/pkg.teste", g_root);
    if (mkdir(pkgdir, 0755) != 0 && errno != EEXIST) { perror("mkdir"); return 1; }
    snprintf(sopath, sizeof(sopath), "%.3800s/mod.so", pkgdir);
    snprintf(confpath, sizeof(confpath), "%.3800s/mod.conf", pkgdir);
    FILE *f = fopen(sopath, "wb");
    if (f == NULL) { perror("criar .so"); return 1; }
    if (fwrite(kSoBytes, 1, kSoLen, f) != kSoLen) { perror("escrever .so"); return 1; }
    fclose(f);
    f = fopen(confpath, "wb");
    if (f == NULL) { perror("criar .conf"); return 1; }
    if (fwrite(kConfBytes, 1, sizeof(kConfBytes) - 1, f) != sizeof(kConfBytes) - 1) {
        perror("escrever .conf");
        return 1;
    }
    fclose(f);

    int sp[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) != 0) { perror("socketpair"); return 1; }

    // O lado do JOGO: escreve o papel PRIMEIRO (igual bc_req_connect no
    // device) e então conversa pelos pedidos REAIS. O mutex é o mesmo papel
    // do g_companion_io do loader — serializa cada transação.
    if (write(sp[0], BC_REQ_HELLO, strlen(BC_REQ_HELLO)) !=
        (ssize_t)strlen(BC_REQ_HELLO)) {
        perror("hello");
        return 1;
    }

    const struct bc_req_handlers h = {t_so, t_txt, t_list, t_bc_so, t_bc_list, t_bc_conf};
    struct serve_arg sa = {sp[1], &h, "pkg.teste"};
    pthread_t srv;
    if (pthread_create(&srv, NULL, serve_loop, &sa) != 0) { perror("pthread"); return 1; }

    // Cenário BC: árvore flat com DOIS mods (um DESABILITADO via .so.off —
    // o mecanismo real de enable/disable de mods dinâmicos, que o Manager
    // aplica renomeando o arquivo) + bc_mods.conf na RAIZ (config dos hooks
    // estáticos). O "jogo BC" conecta como SEGUNDA conexão com sua própria
    // identidade — o gate espelha bc_peer_is_bc_game.
    char bcdir[4096], bca_path[4096], bcconf_path[4096];
    snprintf(bcdir, sizeof(bcdir), "%.3900s/bc_mods", g_root);
    if (mkdir(bcdir, 0755) != 0 && errno != EEXIST) { perror("mkdir bc"); return 1; }
    static const char kBcA[] = "\177ELF\002\001\001\000bc-mod-a-habilitado";
    static const char kBcB[] = "\177ELF\002\001\001\000bc-mod-b-DESABILITADO-por-off";
    static const char kBcConf[] = "godMode=on\n";
    snprintf(bca_path, sizeof(bca_path), "%.3800s/mod_a.so", bcdir);
    char bcb_path[4096];
    snprintf(bcb_path, sizeof(bcb_path), "%.3800s/mod_b.so.off", bcdir);
    snprintf(bcconf_path, sizeof(bcconf_path), "%.3900s/bc_mods.conf", g_root);
    f = fopen(bca_path, "wb");
    if (f == NULL) { perror("bc .so a"); return 1; }
    if (fwrite(kBcA, 1, sizeof(kBcA) - 1, f) != sizeof(kBcA) - 1) return 1;
    fclose(f);
    f = fopen(bcb_path, "wb");
    if (f == NULL) { perror("bc .so b"); return 1; }
    if (fwrite(kBcB, 1, sizeof(kBcB) - 1, f) != sizeof(kBcB) - 1) return 1;
    fclose(f);
    f = fopen(bcconf_path, "wb");
    if (f == NULL) { perror("bc conf"); return 1; }
    if (fwrite(kBcConf, 1, sizeof(kBcConf) - 1, f) != sizeof(kBcConf) - 1) return 1;
    fclose(f);

    int sp_bc[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp_bc) != 0) { perror("socketpair bc"); return 1; }
    if (write(sp_bc[0], BC_REQ_HELLO, strlen(BC_REQ_HELLO)) !=
        (ssize_t)strlen(BC_REQ_HELLO)) {
        perror("hello bc");
        return 1;
    }
    struct serve_arg sa_bc = {sp_bc[1], &h, kBCPkg};
    pthread_t srv_bc;
    if (pthread_create(&srv_bc, NULL, serve_loop, &sa_bc) != 0) { perror("pthread bc"); return 1; }

    pthread_mutex_t io = PTHREAD_MUTEX_INITIALIZER;
    char why[192];

    // 1. mod_fd: o FD volta e ABRE o .so com os bytes exatos
    {
        int fd = bc_req_ask_so(sp[0], &io, "pkg.teste", "mod.so", why, sizeof(why));
        check("mod_fd: o despacho respondeu com um FD", fd >= 0);
        if (fd >= 0) {
            char buf[128] = {};
            ssize_t n = read(fd, buf, sizeof(buf) - 1);
            check("mod_fd: o FD ABRE o .so (bytes exatos, memcmp)",
                  n == (ssize_t)kSoLen && memcmp(buf, kSoBytes, kSoLen) == 0);
            close(fd);
        } else {
            check("mod_fd: o FD ABRE o .so (bytes exatos, memcmp)", false);
        }
    }

    // 2. mod_txt: o conteudo volta, inteiro e certo
    {
        char txt[512] = {};
        int n = bc_req_ask_txt(sp[0], &io, "pkg.teste", "mod.conf", txt, sizeof(txt),
                               why, sizeof(why));
        size_t conf_len = sizeof(kConfBytes) - 1;
        check("mod_txt: o despacho devolveu conteudo", n == (int)conf_len);
        check("mod_txt: o conteudo e o do .conf",
              n == (int)conf_len && memcmp(txt, kConfBytes, conf_len) == 0);
    }

    // 3. mod_list: lista REAL da fixture (o handler escaneia o diretorio com
    // o MESMO filtro de .so da producao)
    {
        char list[4096] = {};
        int n = bc_req_ask_list(sp[0], &io, "pkg.teste", list, sizeof(list),
                                why, sizeof(why));
        check("mod_list: o despacho respondeu", n >= 0);
        check("mod_list: o .so da arvore aparece na lista",
              n > 0 && strstr(list, "mod.so") != NULL);
    }

    // 4. Cenário BC: a LISTA traz só o mod HABILITADO (mod_b está
    // desabilitado pelo mecanismo real, o sufixo .off — o Manager renomeia o
    // arquivo; o filtro de lista da produção/recusa .off é o mesmo).
    {
        char list[4096] = {};
        int n = bc_req_ask_bc_list(sp_bc[0], &io, list, sizeof(list), why, sizeof(why));
        bool ok = n == 1 && strstr(list, "mod_a.so") != NULL &&
                  strstr(list, "mod_b") == NULL;
        check("BC: lista traz exatamente o mod habilitado (1, mod_a)", ok);
        if (!ok) printf("    (n=%d list=%s why=%s)\n", n, list, why);
    }

    // 5. BC: FD do mod habilitado ABRE com os bytes exatos.
    {
        int fd = bc_req_ask_bc_so(sp_bc[0], &io, "mod_a.so", why, sizeof(why));
        check("BC: BO devolve FD do mod habilitado", fd >= 0);
        if (fd >= 0) {
            char buf[128] = {};
            ssize_t n = read(fd, buf, sizeof(buf) - 1);
            check("BC: o FD abre com os bytes exatos (memcmp)",
                  n == (ssize_t)(sizeof(kBcA) - 1) &&
                      memcmp(buf, kBcA, sizeof(kBcA) - 1) == 0);
            close(fd);
        } else {
            check("BC: o FD abre com os bytes exatos (memcmp)", false);
        }
    }

    // 6. BC: o conf da raiz chega por "BT" com o conteúdo certo.
    {
        char conf[512] = {};
        int n = bc_req_ask_bc_conf(sp_bc[0], &io, "bc_mods.conf", conf, sizeof(conf),
                                   why, sizeof(why));
        size_t conf_len = sizeof(kBcConf) - 1;
        check("BC: BT devolve o bc_mods.conf da raiz (bytes exatos)",
              n == (int)conf_len && memcmp(conf, kBcConf, conf_len) == 0);
        if (n != (int)conf_len) printf("    (n=%d why=%s)\n", n, why);
    }

    // 7. BC: "BT" é LISTA FIXA — nome fora da lista é recusado com EACCES,
    // mesmo sendo um jogo legítimo pedindo.
    {
        char conf[64] = {};
        int n = bc_req_ask_bc_conf(sp_bc[0], &io, "allowlist_evil.conf", conf,
                                   sizeof(conf), why, sizeof(why));
        check("BC: BT com nome fora da lista fixa recusado", n < 0);
        // mas o nome DA lista serve (a allowlist é conf legítimo da raiz).
        n = bc_req_ask_bc_conf(sp_bc[0], &io, "bc_generic_allowlist.conf", conf,
                               sizeof(conf), why, sizeof(why));
        // a allowlist não existe na fixture: erro de OPEN (ENOENT), não de
        // GATE — distinto do caso "fora da lista" (que não passa do gate).
        bool enoent = false;
        if (n < 0 && strstr(why, "errno 2") != NULL) enoent = true;  // ENOENT
        check("BC: BT com nome da lista passa do gate (ENOENT do open, não recusa)",
              n < 0 && enoent);
    }

    // 8. CROSS-GATE: o jogo GENÉRICO (pkg.teste) não recebe NADA da árvore
    // BC — nem lista, nem mod. E o jogo BC pedindo mod de OUTRO pkg pelo
    // "SO" também leva recusa (o bc_peer_ok_for_pkg da produção).
    {
        char list[4096] = {};
        int n = bc_req_ask_bc_list(sp[0], &io, list, sizeof(list), why, sizeof(why));
        check("cross-gate: jogo genérico não lista a árvore BC", n < 0);
        int fd = bc_req_ask_bc_so(sp[0], &io, "mod_a.so", why, sizeof(why));
        check("cross-gate: jogo genérico não recebe mod BC por BO", fd < 0);
        char txt[64] = {};
        n = bc_req_ask_txt(sp_bc[0], &io, "pkg.teste", "mod.conf", txt, sizeof(txt),
                           why, sizeof(why));
        check("cross-gate: jogo BC não pede mod de outro pkg pelo SO/TX", n < 0);
    }

    // 9. verbo desconhecido: resposta de erro em LINHA (errno), sem travar.
    // A resposta tem que ser consumivel pelo MESMO parser do jogo — a
    // versao WIP mandava "unknown_verb" sem \n (e com um NUL a mais) e o
    // parser de linha travava a resposta no buffer.
    {
        char req[BC_FD_REQ_MAX];
        ssize_t n = bc_fd_build_request(req, sizeof(req), BC_FD_PROTO, "ZZZ",
                                        "pkg.teste");
        bool sent = n > 0 && bc_fd_send_data(sp[0], req, (size_t)n) >= 0;
        bool parsed = false;
        if (sent) {
            char line[64];
            ssize_t r = bc_fd_read_line(sp[0], line, sizeof(line));
            int e = 0;
            parsed = r > 0 && bc_fd_parse_error(line, &e) && e == EINVAL;
        }
        check("verbo desconhecido: respondeu com errno em linha, sem travar", parsed);
    }

    // 10. BENCH do caminho SEM mods (o custo do connectCompanion por
    // partida genérica é a mesma classe: canal REQ + uma lista vazia —
    // nada de espera bloqueante, timeouts curtos, respostas na hora).
    {
        char list[4096];
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        int rounds = 200;
        for (int i = 0; i < rounds; i++) {
            int n = bc_req_ask_bc_list(sp_bc[0], &io, list, sizeof(list), why, sizeof(why));
            if (n != 1) {
                // a fixture ainda tem mod_a; o bench mede a ROTA, não o vazio
                check("bench: lista respondeu", false);
                break;
            }
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);
        double avg_us = ((t1.tv_sec - t0.tv_sec) * 1e6 +
                         (t1.tv_nsec - t0.tv_nsec) / 1e3) / (double)rounds;
        printf("  [INFO] bench: %d listagens de %d mod: %.1f µs/pedido\n",
               rounds, 1, avg_us);
        check("bench: pedido+resposta sem espera bloqueante (< 5ms/pedido)",
              avg_us < 5000.0);
    }

    close(sp[0]);
    close(sp[1]);
    close(sp_bc[0]);
    close(sp_bc[1]);
    pthread_join(srv, NULL);
    pthread_join(srv_bc, NULL);
    alarm(0);
    unlink(sopath);
    unlink(confpath);
    unlink(bca_path);
    unlink(bcb_path);
    unlink(bcconf_path);
    rmdir(pkgdir);
    rmdir(bcdir);

    printf("== Resultado: %s (%d falhas) ==\n",
           g_fail == 0 ? "TODOS PASSARAM" : "HOUVE FALHAS", g_fail);
    return g_fail == 0 ? 0 : 1;
}
