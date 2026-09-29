// bc_peercred.h — de QUEM é o pedido, e não só o que ele pede.
//
// ============================================================================
// POR QUE (achado critico de seguranca + limite conhecido do anterior)
// ============================================================================
// A correcao anterior trocou "o cliente manda caminho" por "o cliente manda
// (pkg, nome) e o companion valida os dois". Isso fecha a travessia de
// diretorio — o caminho passa a ser MONTADO pelo root. Mas ela nao amarra o
// pacote pedido ao chamador: um jogo A pode pedir os mods do jogo B, e o
// companion entregaria, porque o formato do pacote e valido.
//
// O canal e o MESMO que o codigo do mod dentro do processo usa, e o peer pode
// ser qualquer processo com o mesmo grupo. A amarra correta e o CREDENCIAL do
// socket: getsockopt(SO_PEERCRED) no fd do cliente devolve o uid de quem
// conectou. O uid NAO mente (o kernel preenche), e o companion e root, entao
// consegue mapear uid -> pacote.
//
// REGRA DE MULTIUSUARIO: o appId e uid % 100000. Um uid de app no perfil 10
// tem appId igual ao do perfil 0, e o appId e o que identifica o PACOTE. Ler
// o "uid" cru da coluna de packages.list nao funciona em multiusuario: ali
// vale o appId de coluna.
//
// FAIL-CLOSED: se o packages.list nao puder ser lido, o pedido e RECUSADO. Um
// companion que aceita na duvida e um companion que da a arvore de mods de
// qualquer jogo a qualquer processo — o oposto do que a mudanca de arvore fez.
//
// Isto e um nucleo PURO: recebe o uid e o texto do packages.list e devolve o
// pacote. O companion so faz a E/S (getsockopt + ler o arquivo), que e o que
// nao da para testar no host. Por isso o parse fica aqui e o teste exercita
// este codigo com o uid INJETADO.

#ifndef BC_PEERCRED_H
#define BC_PEERCRED_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define BC_PEERCRED_PKG_CAP 160
// Quantos pacotes de UM appId o companion guarda. sharedUserId tem limites
// (o proprio Android limita), e 16 folga com sobra.
#define BC_PEERCRED_PKG_MAX 16
// "package:com.foo.bar system" -> 32 + 1 + 160 + 1 + 6
#define BC_PEERCRED_LINE_MAX (BC_PEERCRED_PKG_CAP + 64)

static inline int bc_peercred_app_id(int uid) {
    if (uid < 0) return -1;
    return uid % 100000;
}

// ============================================================================
// O GATE DE userId DO VERBO BC — ponto ÚNICO, puro, host-testável (T3)
// ============================================================================
// O verbo BC (bc_peer_is_bc_game no companion) recusa userId≠0 fail-closed
// (multiuser-audit a153b33, F2): a árvore de mods é por pacote, sem dimensão
// de usuário — servir do perfil 0 para o app do perfil 10 seria o oposto de
// isolamento. A DECISÃO mora AQUI porque o companion não compila no host:
// o verbo CHAMA isto, o teste EXERCITA isto. Sabotar a função é pego pelo
// teste dinâmico (T3 chama a função REAL); sabotar a chamada no companion,
// pelo check estático da seção 4 do peercred_test. Antes da extração, o
// `uid / 100000 != 0` vivia inline no companion e NENHUM teste conseguia
// exercitá-lo — o T3 dizia "o VERBO recusa" e só reafirmava o mapeamento
// (achado do kimi no followups-3).
static inline bool bc_peercred_bc_denied_user_id(int uid) {
    if (uid < 0) return true;  // uid inválido: fail-closed, sempre recusa
    return uid / 100000 != 0;
}

// ============================================================================
// appId -> PACOTES (plural), a partir de /data/system/packages.list
// ============================================================================
// PLURAL E O PONTO: varios pacotes podem compartilhar o MESMO appId (sharedUserId
// / android:sharedUserId). A versao anterior devolvia o PRIMEIRO do appId e
// recusava o resto — entao o segundo pacote de um grupo legitimo nao conseguia
// nem pedir os mods DELE MESMO. A regra certa e: o pedido e servido se o pacote
// pedido for QUALQUER pacote daquele appId, e so esses.
//
// FORMATO. O writer do /data/system/packages.list nao foi localizado em nenhum
// espelho do AOSP que eu alcancei (PackageManagerService em master e em
// android14-release nao citam o arquivo; libpackagelistparser nao existe no
// espelho). Entao o parser aceita SO o formato classico — "<nome> <uid> ...",
// com o uid como token solto logo depois do nome — e qualquer outra forma
// resulta em "nao mapeia", que e RECUSA (fail-closed).
//
// A variante "system=<n>" que eu tinha aceitado foi REMOVIDA: nao achei fonte
// AOSP arquivo:linha que a produzisse, e um parser que aceita um formato que
// ninguem confirmou e pior do que um que recusa.
//
// appId e uid %% 100000: a coluna do uid no arquivo e o appid, entao e assim que
// o app do perfil 10 cai na mesma linha do perfil 0.

// appId -> ate `max_out` pacotes. Devolve quantos preencheu (0 = nao mapeia).
static inline int bc_peercred_packages(const char *list, size_t len, int uid,
                                       char (*out)[BC_PEERCRED_PKG_CAP], int max_out) {
    if (list == NULL || out == NULL || max_out <= 0) return 0;
    const int want = bc_peercred_app_id(uid);
    if (want < 0) return 0;
    int n = 0;
    size_t pos = 0;
    while (pos < len) {
        size_t fim = pos;
        while (fim < len && list[fim] != '\n') fim++;
        const size_t l = fim - pos;
        // "package:" opcional (algunsversoes prefixam), depois <nome> <uid> ...
        const char *p = list + pos;
        size_t rest = l;
        if (rest > 8 && memcmp(p, "package:", 8) == 0) { p += 8; rest -= 8; }
        size_t nl = 0;
        while (nl < rest && p[nl] != ' ') nl++;
        if (nl == 0) { pos = fim + 1; continue; }
        size_t k = nl;
        while (k < rest && p[k] == ' ') k++;
        if (k >= rest || p[k] < '0' || p[k] > '9') { pos = fim + 1; continue; }
        int v = 0;
        while (k < rest && p[k] >= '0' && p[k] <= '9') { v = v * 10 + (p[k] - '0'); k++; }
        if (v == want && n < max_out && nl < BC_PEERCRED_PKG_CAP) {
            memcpy(out[n], p, nl);
            out[n][nl] = '\0';
            n++;
        }
        pos = fim + 1;
    }
    return n;
}

// O pedido e de um dos pacotes daquele appId, e so deles.
static inline bool bc_peercred_pkg_matches(char (*caller_pkgs)[BC_PEERCRED_PKG_CAP],
                                          int n, const char *want_pkg) {
    if (caller_pkgs == NULL || want_pkg == NULL) return false;
    if (want_pkg[0] == '\0') return false;
    for (int i = 0; i < n; i++) {
        if (strcmp(caller_pkgs[i], want_pkg) == 0) return true;
    }
    return false;
}

#endif // BC_PEERCRED_H
