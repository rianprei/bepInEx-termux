// Teste mínimo host-only das fórmulas de escala inteira usadas pelo mod.
// Não builda pro device (o resto do mod usa ARM64 hook install, sem
// sentido no host) — só a matemática pura que decide o valor final.
#include <cassert>
#include <cstdint>
#include <cstdio>

static int32_t scale(int32_t raw, int64_t num, int64_t den) {
    return (int32_t)(((int64_t)raw * num) / den);
}

int main() {
    // HP/ATK: *9/5 = *1.8 exato
    assert(scale(300, 9, 5) == 540);
    assert(scale(0, 9, 5) == 0);       // ATK base 0 continua 0
    assert(scale(1, 9, 5) == 1);       // arredonda pra baixo, nao pra 2

    // Range: 190 -> 250 (raw190 vira 250 exato)
    assert(scale(190, 250, 190) == 250);

    // Recarga: 2536f -> 2136f
    assert(scale(2536, 2136, 2536) == 2136);

    // Attack interval: 32f -> 26f
    assert(scale(32, 26, 32) == 26);

    // Negativo nao trava (nao deveria ocorrer em stat real, mas a formula
    // nao deve corromper memoria nem estourar)
    assert(scale(-10, 9, 5) == -18);

    printf("mechabun scale math: TODOS PASSARAM\n");
    return 0;
}
