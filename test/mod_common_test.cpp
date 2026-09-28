// Teste host (offline) do SDK mod_common.h (F2) — sem device, sem NDK:
//   mod_pkg_from_cmdline — fallback real usado por u_dump::wait_for_pkg;
//                          rejeita zygote* e buffer que não cabe (C1)
//   mod_pkg              — env BEPINEX_PKG primeiro; fallback cmdline
//                          cacheado depois da 1ª leitura boa (C1)
//   mod_dir              — /data/local/tmp/mods/<pkg> (C1)
//   mod_log_format_line  — a linha "HH:MM:SS [tag] msg" do log.txt (C1)
//   mod_log_permission_warn — o aviso de log.txt sem permissão (dono errado)
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
#include <ctime>
#include <string>
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
        char process_name[] = "com.foo:unity";
        check("sufixo de processo vira pacote base",
              mod_pkg_from_cmdline(process_name, sizeof(process_name), out, sizeof(out)) &&
              strcmp(out, "com.foo") == 0);
        const std::string max_package(BC_PROCESS_PACKAGE_CAP - 1, 'x');
        char full_out[BC_PROCESS_PACKAGE_CAP] = {};
        check("fallback aceita nome no limite sem truncar",
              mod_pkg_from_cmdline(max_package.c_str(), max_package.size(),
                                   full_out, sizeof(full_out)) &&
              max_package == full_out);
        const std::string oversized(BC_PROCESS_PACKAGE_CAP, 'x');
        check("fallback recusa nome acima do limite",
              !mod_pkg_from_cmdline(oversized.c_str(), oversized.size(),
                                    full_out, sizeof(full_out)));
    }

    printf("[mod_pkg] (C1: env BEPINEX_PKG, fallback cmdline)\n");
    {
        unsetenv("BEPINEX_PKG");
        char expected[BC_PROCESS_PACKAGE_CAP] = {};
        const bool argv_is_package =
            bc_process_copy_from_nice_name(argv[0], expected, sizeof(expected)) == BC_PROCESS_READY;
        const char *fb = mod_pkg();
        check("fallback cmdline aceita só um nome de pacote",
              argv_is_package ? fb != nullptr && strcmp(fb, expected) == 0 : fb == nullptr);
        check("fallback cacheado (mesmo ponteiro)", mod_pkg() == fb);
        setenv("BEPINEX_PKG", "com.env.pkg", 1);
        check("env BEPINEX_PKG vence", strcmp(mod_pkg(), "com.env.pkg") == 0);
        unsetenv("BEPINEX_PKG");
        check("sem env volta ao resultado validado do cmdline",
              argv_is_package ? mod_pkg() != nullptr && strcmp(mod_pkg(), expected) == 0
                              : mod_pkg() == nullptr);
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

    // A linha do log.txt é contrato: o u_dump, o Manager e o kit leem esse
    // arquivo. O formato vem de mod_log_format_line com o time_t injetado,
    // então dá pra conferir no host sem device e sem depender do fuso: o
    // esperado é montado aqui com o PRÓPRIO strftime. São duas cópias
    // independentes da máscara "%H:%M:%S" de propósito — se alguém trocar o
    // separador num dos dois lados, este teste acusa.
    printf("[mod_log_format_line] (C1: linha do log.txt)\n");
    {
        char line[640], want[640], stamp[16];
        time_t when = 1758000000;  // fixo: o host não pode depender do agora
        struct tm tmw;
        localtime_r(&when, &tmw);
        strftime(stamp, sizeof(stamp), "%H:%M:%S", &tmw);

        size_t n = mod_log_format_line(line, sizeof(line), "u_dump", "dump.tsv pronto: 42 linhas", when);
        snprintf(want, sizeof(want), "%s [u_dump] dump.tsv pronto: 42 linhas\n", stamp);
        check("linha completa = HH:MM:SS [tag] msg\\n", n == strlen(want) && strcmp(line, want) == 0);
        check("tamanho devolvido bate com a linha", n == strlen(line));

        // Separador: o campo da hora tem que ter ':' e o tamanho certo.
        check("hora com 2 dígitos e ':'", strlen(stamp) == 8 && stamp[2] == ':' && stamp[5] == ':');

        // Tag e corpo mudam; a moldura não.
        mod_log_format_line(line, sizeof(line), "sa2ammo", "ativo", when);
        snprintf(want, sizeof(want), "%s [sa2ammo] ativo\n", stamp);
        check("tag diferente, mesma moldura", strcmp(line, want) == 0);

        mod_log_format_line(line, sizeof(line), "u_dump", "", when);
        snprintf(want, sizeof(want), "%s [u_dump] \n", stamp);
        check("corpo vazio ainda fecha a linha", strcmp(line, want) == 0);

        // Corpo grande DENTRO do teto (o mod_log trunca em MOD_LOG_BODY_MAX
        // antes de chamar): cabe na linha de 640 e fecha com \n.
        char big[MOD_LOG_BODY_MAX];
        memset(big, 'x', sizeof(big) - 1);
        big[sizeof(big) - 1] = '\0';
        n = mod_log_format_line(line, sizeof(line), "u_dump", big, when);
        check("corpo no teto cabe na linha", n > 0 && n < sizeof(line));
        check("corpo no teto não perde o \n final", line[n - 1] == '\n');

        // Corpo MAIOR que a linha inteira: devolve 0 em vez de truncar meio
        // e escrever lixo. No caminho real não acontece (o vsnprintf do
        // mod_log corta em 512 antes), mas a função é honesta sobre isso.
        char huge[900];
        memset(huge, 'y', sizeof(huge) - 1);
        huge[sizeof(huge) - 1] = '\0';
        check("corpo maior que a linha devolve 0",
              mod_log_format_line(line, sizeof(line), "u_dump", huge, when) == 0);

        // Buffer pequeno demais: devolve 0 e não escreve por fora.
        char tiny[8];
        check("buffer curto devolve 0", mod_log_format_line(tiny, sizeof(tiny), "u_dump", "x", when) == 0);
        check("nulo é recusado", mod_log_format_line(nullptr, 100, "u_dump", "x", when) == 0);
        check("tag nulo é recusado", mod_log_format_line(line, sizeof(line), nullptr, "x", when) == 0);
    }

    // Defesa de permissão (log.txt com dono errado): avisa UMA vez e só
    // quando o erro é EACCES/EPERM. Sem isso o mod fica mudo e o primeiro
    // sintoma é "o mod não funcionou".
    printf("[mod_log_permission_warn] (avisa uma vez, só em EACCES/EPERM)\n");
    {
        check("EACCES avisa (1a vez)", mod_log_permission_warn(EACCES, "u_dump", "/data/data/com.foo/files/bepinex/log.txt") == true);
        check("EACCES de novo NÃO avisa (sem inundar o logcat)",
              mod_log_permission_warn(EACCES, "u_dump", "/data/data/com.foo/files/bepinex/log.txt") == false);
        check("EPERM depois do EACCES também não avisa",
              mod_log_permission_warn(EPERM, "u_dump", "/data/data/com.foo/files/bepinex/log.txt") == false);
        check("ENOENT não é caso de permissão (o app cria a pasta depois)",
              mod_log_permission_warn(ENOENT, "u_dump", "/data/data/com.foo/files/bepinex/log.txt") == false);
        check("EISDIR também não", mod_log_permission_warn(EISDIR, "u_dump", "/x") == false);
    }

    printf("[mod_log] (smoke: nunca derruba mesmo sem conseguir escrever)\n");
    {
        mod_log("test", "n=%d texto=%s", 1, "ok");
        check("mod_log não crasha", true);
    }

    printf("\n== Resultado: %s (%d falhas) ==\n", g_fail == 0 ? "TODOS PASSARAM" : "HOUVE FALHAS", g_fail);
    return g_fail == 0 ? 0 : 1;
}
