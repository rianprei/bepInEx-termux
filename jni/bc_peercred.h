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
// "package:com.foo.bar system" -> 32 + 1 + 160 + 1 + 6
#define BC_PEERCRED_LINE_MAX (BC_PEERCRED_PKG_CAP + 64)

static inline int bc_peercred_app_id(int uid) {
    if (uid < 0) return -1;
    return uid % 100000;
}

// appId -> pacote, a partir do texto de /data/system/packages.list.
//
// O formato da linha e "package:<nome> system=<uid> ...". A coluna do uid ali
// e o APPID, entao comparar com uid % 100000 e o que funciona em multiusuario.
static inline bool bc_peercred_lookup(const char *list, size_t len, int uid,
                                     char *out, size_t outcap) {
    if (out == NULL || outcap == 0) return false;
    out[0] = '\0';
    const int want = bc_peercred_app_id(uid);
    if (want < 0 || list == NULL) return false;
    size_t pos = 0;
    while (pos < len) {
        size_t fim = pos;
        while (fim < len && list[fim] != '\n') fim++;
        size_t l = fim - pos;
        if (l > 12 && memcmp(list + pos, "package:", 8) == 0) {
            const char *p = list + pos + 8;
            const char *q = p;
            while ((size_t)(q - p) < l && *q != ' ') q++;
            const size_t nl = (size_t)(q - p);
            if (nl == 0) { pos = fim + 1; continue; }
            const size_t rest = l - (size_t)(q - pos);
            // O FORMATO do /data/system/packages.list mudou entre versoes do
            // Android: o uid aparece como "system=<n>" em umas e como token
            // solto ("com.foo 1000 /data/...") em outras. supporting os dois
            // e mais barato que descobrir a versao do aparelho, e o UID que
            // importa e o appId nos dois casos.
            int v = -1;
            for (size_t k = 0; k + 7 <= rest; k++) {
                if (memcmp(q + k, "system=", 7) == 0) {
                    size_t r = k + 7;
                    v = 0;
                    while (r < rest && q[r] >= '0' && q[r] <= '9') { v = v * 10 + (q[r] - '0'); r++; }
                    break;
                }
            }
            if (v < 0) {
                // token solto: "<uid> <dataDir> ..." logo depois do nome
                const char *t = q;
                size_t k = 0;
                if (k < rest && t[k] >= '0' && t[k] <= '9') {
                    v = 0;
                    while (k < rest && t[k] >= '0' && t[k] <= '9') { v = v * 10 + (t[k] - '0'); k++; }
                }
            }
            if (v == want) {
                if (nl >= outcap) return false;
                memcpy(out, p, nl);
                out[nl] = '\0';
                return true;
            }
        }
        pos = fim + 1;
    }
    return false;
}

// O pedido e deste peer. Devolve true quando pode servir.
static inline bool bc_peercred_pkg_matches(const char *caller_pkg, const char *want_pkg) {
    if (caller_pkg == NULL || want_pkg == NULL) return false;
    if (caller_pkg[0] == '\0' || want_pkg[0] == '\0') return false;
    return strcmp(caller_pkg, want_pkg) == 0;
}

#endif // BC_PEERCRED_H
