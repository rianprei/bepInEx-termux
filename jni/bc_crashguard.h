// bc_crashguard.h — proteção contra crash em loop (F1d / garantia G1).
//
// Regra do roadmap: 2 mortes seguidas dentro da janela depois de carregar mod ⇒
// o loader não carrega mais mods daquele jogo até o usuário reativar. O contador mora
// em /data/data/<pkg>/files/bepinex/crashguard ("<count> <unix_ts>"), que o
// jogo escreve no diretório dele (única escrita que o app domain tem de
// folga — property e /data/local/tmp são negados em Enforcing).
//
// Aqui só a decisão, pura e testável no host; o I/O fica no main.cpp.
#ifndef BC_CRASHGUARD_H
#define BC_CRASHGUARD_H

#include <stdbool.h>

// Janela de 20s, não 60s: crash de mod no BOOT acontece nos primeiros segundos
// (o t_crash morre com 2s), enquanto 60s pegava também o usuário que abre o
// jogo e fecha logo em seguida — falso positivo que custa os mods sem mod
// nenhum ter culpa. 20s cobre o boot crash de verdade sem punir a sessão curta.
#define BC_CRASHGUARD_WINDOW_S 20
#define BC_CRASHGUARD_LIMIT 2

// true = não carregar mods deste jogo agora.
static inline bool bc_crashguard_blocks(int count, long long ts, long long now) {
    if (count < BC_CRASHGUARD_LIMIT) return false;
    return (now - ts) < BC_CRASHGUARD_WINDOW_S;
}

// Contador que grava ao carregar: começa em 1 quando não há registro ou o
// registro expirou (o jogo sobreviveu mais da janela), senão incrementa.
static inline int bc_crashguard_next_count(int count, long long ts, long long now) {
    if (count <= 0) return 1;
    if ((now - ts) >= BC_CRASHGUARD_WINDOW_S) return 1;
    return count + 1;
}

#endif // BC_CRASHGUARD_H
