// test/il2cpp_str_eq_test.cpp — il2cpp_str_eq com len hostil da memória do jogo.
//
// O len vem do objeto System.String alheio: negativo ou absurdo = não igual,
// SEM iterar além do esperado. Buffer sintético de 64 bytes; len gigante
// leria megabytes fora do objeto (no código antigo, SIGSEGV).
#include "../mods/common/il2cpp_min.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

static int failures = 0;

static void check(const char *name, bool condition) {
    printf("  [%s] %s\n", condition ? "PASS" : "FAIL", name);
    if (!condition) failures++;
}

// Monta System.String sintética: header de 2 ponteiros + int32 len + chars.
static void make_string(uint8_t *buf, int32_t len, const char *ascii) {
    memset(buf, 0, 64);
    memcpy(buf + 16, &len, sizeof(len));
    size_t n = strlen(ascii);
    if (n > 20) n = 20;
    for (size_t i = 0; i < n; i++) {
        buf[20 + i * 2] = (uint8_t)ascii[i];
    }
}

int main() {
    uint8_t buf[64];
    make_string(buf, 7, "HasAmmo");
    check("igualdade exata", il2cpp_str_eq(buf, "HasAmmo"));
    check("nome diferente recusa", !il2cpp_str_eq(buf, "HasResist"));
    check("nulo recusa", !il2cpp_str_eq(nullptr, "HasAmmo"));

    make_string(buf, 0x7FFFFFFF, "HasAmmo");
    check("len gigante não é igual e não lê fora do buffer",
            !il2cpp_str_eq(buf, "HasAmmo"));

    // ascii[-1] controlado em '\0': o código antigo pula o loop e lê
    // ascii[len] com len == -1, devolvendo "igual" (errado) de forma
    // determinística; o novo recusa sem tocar em ascii[-1].
    static char probe_neg[16];
    probe_neg[0] = '\0';
    memcpy(probe_neg + 1, "HasAmmo", 8);
    make_string(buf, -1, "HasAmmo");
    check("len negativo não é igual (sem ler ascii[-1])",
            !il2cpp_str_eq(buf, probe_neg + 1));

    make_string(buf, 3, "HasAmmo");
    check("len menor que o esperado não é igual", !il2cpp_str_eq(buf, "HasAmmo"));

    printf("=== il2cpp_str_eq_test: %d falha(s) ===\n", failures);
    return failures == 0 ? 0 : 1;
}
