// test/elf_symlink_test.cpp — open() com O_NOFOLLOW recusa symlink.
//
// O preflight lê .so do disco; um symlink no caminho tem que dar erro
// (ELOOP), nunca ser seguido em silêncio. Controle positivo: o próprio
// binário de teste é um ELF válido — a sonda pode dizer NO_SYMBOL, mas
// nunca ERROR.
#include "../jni/bc_elf_file.h"

#include <errno.h>
#include <cstdio>
#include <string>
#include <string.h>
#include <unistd.h>

static int failures = 0;

static void check(const char *name, bool condition) {
    printf("  [%s] %s\n", condition ? "PASS" : "FAIL", name);
    if (!condition) failures++;
}

int main() {
    char self[4096];
    ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 1);
    check("lê o próprio executável", n > 0);
    if (n <= 0) return 2;
    self[n] = '\0';

    bc_elf_file_probe p = bc_elf_file_has_bc_mod_register(self);
    check("ELF real não dá erro", p.result != BC_ELF_FILE_ERROR);

    char dir[] = "/tmp/elf-symlink-test-XXXXXX";
    check("dir temporário criado", mkdtemp(dir) != nullptr);
    std::string link = std::string(dir) + "/link.so";
    unlink(link.c_str());
    check("symlink criado", symlink(self, link.c_str()) == 0);

    bc_elf_file_probe q = bc_elf_file_has_bc_mod_register(link.c_str());
    check("symlink recusado sem seguir (ELOOP)",
            q.result == BC_ELF_FILE_ERROR && q.error_number == ELOOP);

    char soname[128];
    bc_elf_file_probe r = bc_elf_file_read_soname(link.c_str(), soname, sizeof(soname));
    check("read_soname recusa symlink",
            r.result == BC_ELF_FILE_ERROR && r.error_number == ELOOP);

    // read_soname32 (caminho ARM32) tem open próprio: sem O_NOFOLLOW só
    // nele, a suíte ficava verde no host 64-bit e o furo passava.
    bc_elf_file_probe s32 = bc_elf_file_read_soname32(link.c_str(), soname, sizeof(soname));
    check("read_soname32 recusa symlink",
            s32.result == BC_ELF_FILE_ERROR && s32.error_number == ELOOP);

    unlink(link.c_str());
    rmdir(dir);
    printf("=== elf_symlink_test: %d falha(s) ===\n", failures);
    return failures == 0 ? 0 : 1;
}
