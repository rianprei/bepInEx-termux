// test/console_launch_test.cpp — o veredito de bc_root_script_ok: root só
// executa arquivo regular, do root, sem escrita de app (companion-followups #2).
//
// O check é o MESMO código que o companion (root) roda antes de subir o
// console do Termux — se divergir, este teste cai.

#include <sys/stat.h>
#include <sys/types.h>

#include <cstdio>
#include <cstring>

#include "../jni/bc_launch_check.h"

static int g_fail = 0;
static void check(const char *name, bool ok) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) g_fail++;
}

static bool aceita(mode_t mode, uid_t uid) {
    struct stat st = {};
    st.st_mode = mode;
    st.st_uid = uid;
    char why[128];
    return bc_root_script_ok(&st, why, sizeof(why)) == 0;
}

static bool recusa_com(mode_t mode, uid_t uid, const char *sub) {
    struct stat st = {};
    st.st_mode = mode;
    st.st_uid = uid;
    char why[128] = {0};
    int rc = bc_root_script_ok(&st, why, sizeof(why));
    return rc == -1 && strstr(why, sub) != nullptr;
}

int main() {
    printf("== console_launch: root só executa o que o root controla ==\n");

    check("arquivo regular, root, 0755 (o padrão do módulo): ACEITA",
          aceita(S_IFREG | 0755, 0));
    check("arquivo regular, root, 0644: ACEITA", aceita(S_IFREG | 0644, 0));

    check("gravável pelo GRUPO (0664): RECUSA ('gravavel por grupo')",
          recusa_com(S_IFREG | 0664, 0, "gravavel por grupo"));
    check("gravável por OUTROS (0757): RECUSA", recusa_com(S_IFREG | 0757, 0, "grupo/outros"));
    check("dono é APPUID (10123): RECUSA ('escalada')",
          recusa_com(S_IFREG | 0755, 10123, "escalada"));
    check("SYMLINK: RECUSA ('nao e arquivo regular')",
          recusa_com(S_IFLNK | 0777, 0, "nao e arquivo regular"));
    check("DIRETÓRIO: RECUSA", recusa_com(S_IFDIR | 0755, 0, "nao e arquivo regular"));
    {
        char w[64] = {0};
        check("stat nulo: RECUSA", bc_root_script_ok(nullptr, w, sizeof(w)) == -1);
    }

    printf("== Resultado: %s (%d falhas) ==\n",
           g_fail == 0 ? "TODOS PASSARAM" : "HOUVE FALHAS", g_fail);
    return g_fail == 0 ? 0 : 1;
}
