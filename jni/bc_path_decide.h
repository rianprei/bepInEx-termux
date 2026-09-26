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
#include <string.h>

typedef enum {
    BC_PATH_BC = 0,    // Battle Cats
    BC_PATH_PKG_MODS,  // mods/<pkg>/ existe
    BC_PATH_COCOS,     // allowlist, sem pasta (legado)
    BC_PATH_NONE,      // nada a fazer
} bc_path_kind;

// BC casa por substring (o nome do processo pode vir com sufixo de processo
// auxiliar) — era o is_bc() de main.cpp, morou aqui pra ter cópia única: o
// harness testa exatamente o matcher que roda no device.
static inline bool bc_path_is_bc(const char *pkg) {
    return pkg && strstr(pkg, "jp.co.ponos.battlecatsen") != NULL;
}

static inline bc_path_kind bc_decide_path(const char *pkg, bool dir_exists, bool in_allowlist) {
    if (bc_path_is_bc(pkg)) return BC_PATH_BC;
    if (dir_exists) return BC_PATH_PKG_MODS;
    if (in_allowlist) return BC_PATH_COCOS;
    return BC_PATH_NONE;
}

#endif // BC_PATH_DECIDE_H
