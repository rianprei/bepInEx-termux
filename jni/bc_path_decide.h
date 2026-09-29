// bc_path_decide.h — qual caminho o loader toma pra um app, decidido uma vez e
// só. Puro e host-testável (test/selftest_harness.cpp inclui este header), sem
// I/O: quem chama traz os dois dados de filesystem já resolvidos (1 stat da
// pasta de mods + leitura da allowlist).
//
// Prioridade (F1, docs/ROADMAP-UNIVERSAL.md):
//   1. Battle Cats      → caminho BC, intocado (hooks hardcoded, bc_mods/,
//                         companion). Vence tudo: é app único, com dono.
//   2. mods/<pkg>/      → caminho de mods autônomos, SEM allowlist (F1
//                         "ativação zero-config": pasta basta). Nada de
//                         companion e nada da cascata Cocos2d-x — jogo Unity/
//                         IL2CPP não expõe Java_* e o hook de log genérico já
//                         crashou o Swamp Attack 2 3s depois de abrir.
//   3. allowlist        → experimento Cocos2d-x legado, só quando não há pasta.
//   4. nada             → DLCLOSE_MODULE_LIBRARY.
#ifndef BC_PATH_DECIDE_H
#define BC_PATH_DECIDE_H

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

// Identidade canônica do Battle Cats — a MESMA constante do bc_signal.h, sem
// literal repetido. O include é de bloco #ifdef para não criar dependência
// circular (bc_signal.h inclui coisas do Android que o host não tem).
#ifndef BC_BC_PKG
#define BC_BC_PKG "jp.co.ponos.battlecatsen"
#endif

typedef enum {
    BC_PATH_BC = 0,    // Battle Cats
    BC_PATH_PKG_MODS,  // mods/<pkg>/ existe
    BC_PATH_COCOS,     // allowlist, sem pasta (legado)
    BC_PATH_NONE,      // nada a fazer
} bc_path_kind;

// BC casa por IGUALDADE EXATA com a constante — nunca substring (F1/X1,
// reprovado pelo kimi: com.evil.jp.co.ponos.battlecatsen passava no strstr
// e lia a árvore root-only bc_mods: lista, FD dos .so, conf).
// O lado in-process (nice_name do zygote) aceita "BC_BC_PKG:processo" porque
// o Android registra subprocessos como "pkg:svc" — o limite É o ':' e o
// prefixo tem que ser O pacote inteiro. O lado peer (packages.list) só tem
// nomes de pacote puros, então o ':' lá é no-op.
static inline bool bc_path_is_bc(const char *pkg) {
    if (pkg == NULL) return false;
    const size_t len = strlen(pkg);
    const size_t canon = sizeof(BC_BC_PKG) - 1;  // sem o NUL
    if (len != canon) {
        // "pkg:svc" (processo): o prefixo é o pacote EXATO e o ':' vem logo
        // depois. "com.evil.jp.co.ponos.battlecatsen" (len > canon) e
        // "jp.co.ponos.battlecatsenx" (len > canon) caem aqui e são RECUSADOS.
        return len > canon && pkg[canon] == ':' &&
               memcmp(pkg, BC_BC_PKG, canon) == 0;
    }
    return memcmp(pkg, BC_BC_PKG, canon) == 0;
}

static inline bc_path_kind bc_decide_path(const char *pkg, bool dir_exists, bool in_allowlist) {
    if (bc_path_is_bc(pkg)) return BC_PATH_BC;
    if (dir_exists) return BC_PATH_PKG_MODS;
    if (in_allowlist) return BC_PATH_COCOS;
    return BC_PATH_NONE;
}

#endif // BC_PATH_DECIDE_H
