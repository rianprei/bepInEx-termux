// Teste mínimo host-only da fórmula de buff (*9/5 = *1.8 em inteiro).
// Não builda pro device (o resto do mod usa ARM64 hook install, sem
// sentido no host) — só a matemática pura que decide o valor final.
#include <cassert>
#include <cstdint>
#include <cstdio>

static int32_t buff(int32_t raw) {
    return (int32_t)(((int64_t)raw * 9) / 5);
}

int main() {
    assert(buff(300) == 540);      // 300*1.8 exato
    assert(buff(0) == 0);          // ATK base 0 continua 0 (unidade sem dano base)
    assert(buff(1) == 1);          // 1*9/5 = 1 (arredonda pra baixo, não pra 2)
    assert(buff(-10) == -18);      // nao deveria ocorrer (stat negativo), mas nao trava
    printf("mechabun buff math: TODOS PASSARAM\n");
    return 0;
}
