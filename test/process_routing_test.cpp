#include <cstdio>
#include <cstring>
#include <string>
#include "../jni/bc_process.h"
#include "../mods/u_patch/jni/u_patch_pkg.h"
#include "../jni/bc_path_decide.h"

static int g_fail = 0;

static void check(const char *name, bool ok) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) g_fail++;
}

struct process_case {
    const char *name;
    int uid;
    bool child_zygote;
    const char *nice_name;
    const char *app_data_dir;
    enum bc_process_status expected;
    const char *expected_package;
    bool expected_load;
};

int main() {
    printf("[Caso 79] bc_process_select: pacote canônico e filtro de processo\n");
    const process_case cases[] = {
        {"principal usa app_data_dir", 10123, false, "com.foo", "/data/user/0/com.foo",
         BC_PROCESS_READY, "com.foo", true},
        {"processo :unity usa pacote do app_data_dir", 10123, false, "com.foo:unity",
         "/data/user/0/com.foo", BC_PROCESS_READY, "com.foo", true},
        {"processo :remote remove sufixo quando app_data_dir falta", 10123, false,
         "com.foo:remote", nullptr, BC_PROCESS_READY, "com.foo", true},
        {"processo :unity remove sufixo no fallback de nice_name", 10123, false,
         "com.foo:unity", nullptr, BC_PROCESS_READY, "com.foo", true},
        {"UID isolado e recusado", 90000, false, "com.foo:unity", "/data/user/0/com.foo",
         BC_PROCESS_SKIP_ISOLATED_UID, "", false},
        {"UID isolado no usuário secundário e recusado", 190000, false, "com.foo:remote",
         "/data/user/10/com.foo", BC_PROCESS_SKIP_ISOLATED_UID, "", false},
        {"child zygote e recusado", 10123, true, "com.foo:unity", "/data/user/0/com.foo",
         BC_PROCESS_SKIP_CHILD_ZYGOTE, "", false},
        {"nome app_zygote e recusado", 10123, false, "com.foo:app_zygote",
         "/data/user/0/com.foo", BC_PROCESS_SKIP_APP_ZYGOTE, "", false},
        {"app_zygote sem prefixo de pacote e recusado", 10123, false, "app_zygote64",
         nullptr, BC_PROCESS_SKIP_APP_ZYGOTE, "", false},
        {"underscore, dígitos e maiúsculas preservados", 10123, false, "com_3.Foo:remote",
         "/data/user/0/com_3.Foo", BC_PROCESS_READY, "com_3.Foo", true},
        {"app_data_dir tem precedência sobre nice_name", 10123, false, "com.other:remote",
         "/data/user/10/Com_3.Foo", BC_PROCESS_READY, "Com_3.Foo", true}
    };

    for (const process_case &c : cases) {
        char package[BC_PROCESS_PACKAGE_CAP] = {};
        enum bc_process_status got = bc_process_select(
            c.uid, c.child_zygote, c.nice_name, c.app_data_dir,
            package, sizeof(package));
        check(c.name, got == c.expected && strcmp(package, c.expected_package) == 0);
        const bc_path_kind path = got == BC_PROCESS_READY
            ? bc_decide_path(package, true, false) : BC_PATH_NONE;
        check("carga coincide com elegibilidade esperada",
              (path != BC_PATH_NONE) == c.expected_load);
        printf("    nice=%s app_data=%s => %s, package=%s\n",
               c.nice_name, c.app_data_dir ? c.app_data_dir : "(ausente)",
               bc_process_status_name(got), package);
    }

    {
        const std::string max_package(BC_PROCESS_PACKAGE_CAP - 1, 'x');
        const std::string max_path = "/data/user/0/" + max_package;
        char package[BC_PROCESS_PACKAGE_CAP] = {};
        enum bc_process_status got = bc_process_select(
            10123, false, max_package.c_str(), max_path.c_str(), package, sizeof(package));
        check("nome de 255 chars cabe sem truncar",
              got == BC_PROCESS_READY && max_package == package);

        const std::string oversized(BC_PROCESS_PACKAGE_CAP, 'x');
        const std::string oversized_path = "/data/user/0/" + oversized;
        got = bc_process_select(10123, false, oversized.c_str(), oversized_path.c_str(),
                                package, sizeof(package));
        check("nome de 256 chars e recusado explicitamente",
              got == BC_PROCESS_REJECT_PACKAGE_TOO_LONG && package[0] == '\0');

        char old_pkg_buffer[128] = {};
        char mods_dir[UP_MODS_DIR_CAP] = {};
        got = upatch_package_prepare(max_package.c_str(), old_pkg_buffer,
                                     sizeof(old_pkg_buffer), mods_dir, sizeof(mods_dir));
        check("u_patch recusa sem truncar se seu buffer não comporta o nome",
              got == BC_PROCESS_REJECT_PACKAGE_TOO_LONG && old_pkg_buffer[0] == '\0');
        char patch_pkg[UP_PACKAGE_CAP] = {};
        got = upatch_package_prepare(max_package.c_str(), patch_pkg, sizeof(patch_pkg),
                                     mods_dir, sizeof(mods_dir));
        check("u_patch conserva pacote de 255 chars e caminho completo",
              got == BC_PROCESS_READY && max_package == patch_pkg &&
              std::string(mods_dir) == "/data/local/tmp/mods/" + max_package);
    }

    {
        char package[BC_PROCESS_PACKAGE_CAP] = {};
        enum bc_process_status got = bc_process_select(
            10123, false, "jp.co.ponos.battlecatsen:worker",
            "/data/user/0/jp.co.ponos.battlecatsen", package, sizeof(package));
        check("Battle Cats mantém package id para o matcher legado",
              got == BC_PROCESS_READY && strcmp(package, "jp.co.ponos.battlecatsen") == 0 &&
              bc_decide_path(package, false, false) == BC_PATH_BC);
    }

    printf("\n== Resultado: %s (%d falhas) ==\n",
           g_fail == 0 ? "TODOS PASSARAM" : "HOUVE FALHAS", g_fail);
    return g_fail == 0 ? 0 : 1;
}
