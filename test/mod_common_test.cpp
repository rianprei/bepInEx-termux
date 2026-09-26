// Teste host (offline) do SDK mod_common.h (F2) — sem device, sem NDK:
//   mod_pkg_from_cmdline — rejeita zygote* e buffer que não cabe (C1)
//   mod_pkg              — env BEPINEX_PKG primeiro; fallback cmdline
//                          cacheado depois da 1ª leitura boa (C1)
//   mod_dir              — /data/local/tmp/mods/<pkg> (C1)
//   mod_conf_find        — parser key=value, '#' comenta, trim (C3)
//   conf_as_*            — conversões bool/int/float com default (C3)
//   mod_conf_get         — default quando arquivo/key não existe (C3)
// O caso "pkg ainda zygote no constructor" não dá pra reproduzir no host (o
// argv[0] daqui nunca é zygote): a rejeição é coberta nos testes unitários
// de mod_pkg_from_cmdline, que é exatamente o código que lê o cmdline.
//
// Compilar: g++ -std=c++17 -Wall -Wextra -o mod_common_test mod_common_test.cpp
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include "../mods/common/mod_common.h"

static int g_fail = 0;
static void check(const char *name, bool cond) {
    printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
    if (!cond) g_fail++;
}

int main(int argc, char **argv) {
    (void)argc;
    char out[128], v[128];

    printf("[mod_pkg_from_cmdline] (C1: rejeitar zygote*)\n");
    {
        check("zygote64 rejeitado", !mod_pkg_from_cmdline("zygote64", 9, out, sizeof(out)));
        check("zygote32 rejeitado", !mod_pkg_from_cmdline("zygote32", 9, out, sizeof(out)));
        check("buffer vazio rejeitado", !mod_pkg_from_cmdline("com.a", 0, out, sizeof(out)));
        check("buffer maior que out rejeitado", !mod_pkg_from_cmdline("com.a.b", 8, out, 4));
        check("pkg comum aceito", mod_pkg_from_cmdline("com.foo.bar", 12, out, sizeof(out)) && strcmp(out, "com.foo.bar") == 0);
        char multi[] = "com.foo\0--arg";
        check("cmdline para no 1º NUL", mod_pkg_from_cmdline(multi, sizeof(multi), out, sizeof(out)) && strcmp(out, "com.foo") == 0);
    }

    printf("[mod_pkg] (C1: env BEPINEX_PKG, fallback cmdline)\n");
    {
        unsetenv("BEPINEX_PKG");
        const char *fb = mod_pkg();
        check("fallback cmdline = argv[0]", fb != nullptr && strcmp(fb, argv[0]) == 0);
        check("fallback cacheado (mesmo ponteiro)", mod_pkg() == fb);
        setenv("BEPINEX_PKG", "com.env.pkg", 1);
        check("env BEPINEX_PKG vence", strcmp(mod_pkg(), "com.env.pkg") == 0);
        unsetenv("BEPINEX_PKG");
        check("sem env volta pro cache (argv[0])", strcmp(mod_pkg(), argv[0]) == 0);
    }

    printf("[mod_dir] (C1)\n");
    {
        char dir[320];
        setenv("BEPINEX_PKG", "com.env.pkg", 1);
        check("dir do mod com env", mod_dir(dir, sizeof(dir)) && strcmp(dir, "/data/local/tmp/mods/com.env.pkg") == 0);
        check("buffer curto rejeitado", !mod_dir(out, 8));
        unsetenv("BEPINEX_PKG");
    }

    printf("[mod_conf_find] (C3: key=value, '#' comenta)\n");
    {
        FILE *f = tmpfile();
        if (!f) { check("tmpfile", false); return 1; }
        fputs("# comentário\n"
              "key1=2\n"
              "sem_igual\n"
              " key2 = espaçado \r\n"
              "key1x=9\n"
              "vazio=\n"
              "#key3=0\n", f);
        check("key básica", mod_conf_find(f, "key1", v, sizeof(v)) && strcmp(v, "2") == 0);
        check("key inexistente", !mod_conf_find(f, "nope", v, sizeof(v)));
        check("linha sem '=' ignorada", !mod_conf_find(f, "sem_igual", v, sizeof(v)));
        check("espaços e \\r\\n trimmados", mod_conf_find(f, "key2", v, sizeof(v)) && strcmp(v, "espaçado") == 0);
        check("key1x não casa com key1", mod_conf_find(f, "key1x", v, sizeof(v)) && strcmp(v, "9") == 0);
        check("valor vazio é válido", mod_conf_find(f, "vazio", v, sizeof(v)) && v[0] == '\0');
        check("linha comentada ignorada", !mod_conf_find(f, "key3", v, sizeof(v)));
        fclose(f);
    }

    printf("[conf_as_*] (C3: conversões com default)\n");
    {
        check("bool true", conf_as_bool("true", false));
        check("bool TRUE", conf_as_bool("TRUE", false));
        check("bool 1", conf_as_bool("1", false));
        check("bool false", !conf_as_bool("false", true));
        check("bool 0", !conf_as_bool("0", true));
        check("bool vazio = default", conf_as_bool("", true));
        check("bool nulo = default", conf_as_bool(nullptr, true));
        check("int 42", conf_as_int("42", 0) == 42);
        check("int -3", conf_as_int("-3", 0) == -3);
        check("int sujo = default", conf_as_int("x", 7) == 7);
        check("int vazio = default", conf_as_int("", 7) == 7);
        check("float 2.5", conf_as_float("2.5", 0) == 2.5);
        check("float -0.5", conf_as_float("-0.5", 0) == -0.5);
        check("float sujo = default", conf_as_float("x", 1.5) == 1.5);
        check("float nulo = default", conf_as_float(nullptr, 1.5) == 1.5);
    }

    printf("[mod_conf_get] (C3: default quando arquivo não existe no host)\n");
    {
        setenv("BEPINEX_PKG", "com.env.pkg", 1);
        check("string default", strcmp(mod_conf_get("id", "k", "DEF"), "DEF") == 0);
        check("int default", mod_conf_int("id", "k", 42) == 42);
        check("float default", mod_conf_float("id", "k", 1.5) == 1.5);
        check("bool default true", mod_conf_bool("id", "k", true));
        check("bool default false", !mod_conf_bool("id", "k", false));
        unsetenv("BEPINEX_PKG");
    }

    printf("[mod_log] (smoke: nunca derruba mesmo sem conseguir escrever)\n");
    {
        mod_log("test", "n=%d texto=%s", 1, "ok");
        check("mod_log não crasha", true);
    }

    printf("\n== Resultado: %s (%d falhas) ==\n", g_fail == 0 ? "TODOS PASSARAM" : "HOUVE FALHAS", g_fail);
    return g_fail == 0 ? 0 : 1;
}
