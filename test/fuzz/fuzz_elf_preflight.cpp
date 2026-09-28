// fuzz_elf_preflight — alvo de fuzzing do PREFLIGHT de ELF que roda no .so
// do disco ANTES de dlopen (jni/bc_elf_file.h), incluindo a guarda de SONAME
// do frida-gadget.
//
// Por que importa: o preflight roda DENTRO do processo do jogo. Um .so
// malformado (mod baixado, .so truncado por escrita interrompida, ou um
// binário hostil) que passe longe dos bounds do parser lê memória arbitrária
// e derruba o jogo; 2 mortes em 20s travam o jogo por completo via crashguard.
//
// O harness cobre DUAS camadas:
//
//   A) camada pura, chamada direto com o input do fuzzer (sem I/O):
//      bc_elf_file_range_ok, bc_elf_file_symbol_filter,
//      bc_elf_file_soname_is_frida e bc_elf_symtab_scan_filtered_ex — o
//      scanner de .dynsym que recebe symtab/strtab/strtab_size e um filtro.
//      O resto do input serve de strtab, com strtab_size FUZZADO a partir do
//      input — é daí que sai o st_name relativo.
//
//   B) camada de arquivo, idêntica à de produção: o input é gravado num
//      tempfile e bc_elf_file_has_bc_mod_register + bc_elf_file_read_soname
//      abrem, fstat, leem o Ehdr, a tabela de section headers, a .dynsym, a
//      .dynstr e a SHT_DYNAMIC. É aqui que e_shoff/e_shnum/e_shentsize/
//      sh_offset/sh_size/sh_link/d_un.d_val/st_name são lidos de um arquivo
//      que o usuário controla — o preflight tem que recusar TUDO fora do
//      arquivo, não confiar em nenhum campo.
//
// O tempfile é reaproveitado (criado uma vez, reescrito por exec) para não
// deixar lixo no /tmp nem custar open/mkstemp por exec. O conteúdo exato do
// arquivo é o input do fuzzer, byte a byte — nenhum padding, senão o parser
// nunca veria o sh_size maior que o arquivo.
//
// Compilar:
//   clang++ -std=c++17 -g -O1 -fsanitize=fuzzer,address,undefined \
//           -I../../jni fuzz_elf_preflight.cpp -o fuzz_elf_preflight

#include <elf.h>
#include <fcntl.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "../../jni/bc_elf_file.h"

namespace {

constexpr size_t kMaxInput = 256 * 1024;

// Tempfile único, reescrito por exec. -1 = ainda não criado.
int g_fd = -1;
char g_path[64];

int ensure_tmp() {
    if (g_fd >= 0) return g_fd;
    snprintf(g_path, sizeof(g_path), "/tmp/bc-fuzz-elf-XXXXXX");
    g_fd = mkstemp(g_path);
    return g_fd;
}

void reset_tmp() {
    if (ftruncate(g_fd, 0) != 0) {
        // Sem ftruncate, execs seguintes concatenariam e o parser leria um
        // arquivo maior que o input (falso "fora do arquivo" mascarado).
        abort();
    }
}

struct SymCbState {
    int calls;
    char last[64];
};

void sym_cb(const char *name, uint64_t addr, void *user) {
    SymCbState *s = (SymCbState *)user;
    s->calls++;
    s->last[0] = name ? name[0] : '?';
    (void)addr;
}

// strtab plausível: um blob de bytes com NULs esparsos, do jeito que um
// .dynstr real é. Serve de entrada E de nome que o filtro vai olhar.
void build_strtab(const uint8_t *data, size_t size, std::vector<char> *out, size_t *reported) {
    out->assign(size + 1, '\0');
    for (size_t i = 0; i < size; i++) (*out)[i] = (char)data[i];
    (*out)[size] = '\0';
    // strtab_size declarado: o input escolhe (0 = strtab vazio, resto = o
    // tamanho real). Divergência entre o buffer alocado e o declarado é
    // exatamente a leitura-fora-dos-limites que o scanner precisa recusar.
    *reported = size ? (size_t)data[size - 1] : 0;
    if (*reported > size) *reported = size;
}

// Deriva o número de símbolos e o st_name de cada um do fim do input, para o
// scanner de .dynsym receber st_name que aponta pra qualquer lugar do strtab.
void build_symtab(const uint8_t *data, size_t size, size_t strtab_size,
                  std::vector<bc_elf64_sym> *out) {
    size_t n = size / 8;
    if (n < 1) n = 1;
    if (n > 4096) n = 4096;
    out->assign(n, bc_elf64_sym{});
    // at() em vez de p[k]: input de 0..7 bytes dá n == 1 e o bloco de 4
    // bytes do símbolo cairia fora do buffer do fuzzer.
    auto byte = [&](size_t k) -> uint8_t { return k < size ? data[k] : 0; };
    for (size_t i = 0; i < n; i++) {
        const uint8_t b0 = byte(i * 8 + 0);
        const uint8_t b1 = byte(i * 8 + 1);
        const uint8_t b2 = byte(i * 8 + 2);
        const uint8_t b3 = byte(i * 8 + 3);
        (*out)[i].st_name = (uint32_t)b0;
        // 1/4 dos símbolos com st_name absurdo (fora do strtab) e 1/4 com
        // st_name 0 e 1/4 no último byte válido do strtab: os caminhos de
        // recusa precisam ser alcançados, não só o caminho feliz.
        if (i % 4 == 1) (*out)[i].st_name = 0xfffffff0u;
        if (i % 4 == 2) (*out)[i].st_name = 0;
        if (i % 4 == 3) (*out)[i].st_name = strtab_size ? (strtab_size - 1) : 0;
        (*out)[i].st_info = (uint8_t)(b1 ? b1 : (BC_ELF_STB_GLOBAL << 4 | BC_ELF_STT_FUNC));
        (*out)[i].st_shndx = (uint16_t)(b2 ? b2 : 1);
        (*out)[i].st_value = b3 ? (uint64_t)b3 : 0x1000;
    }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size > kMaxInput) size = kMaxInput;

    // ---- camada A: núcleo puro, sem I/O --------------------------------
    {
        std::vector<char> strtab;
        size_t reported = 0;
        build_strtab(data, size, &strtab, &reported);

        SymCbState st = {};
        bc_elf_symtab_scan_filtered_ex(nullptr, 0, nullptr, 0, nullptr, nullptr, nullptr, true);
        // O filtro real do preflight (nome exato "bc_mod_register") e o da
        // generalização (prefixo "Java_") — os dois que o jogo chama.
        (void)bc_elf_symtab_scan_filtered_ex(
            nullptr, 0, strtab.data(), reported, bc_elf_file_symbol_filter, sym_cb, &st, true);
        (void)bc_elf_symtab_scan(
            nullptr, 0, strtab.data(), reported,
            bc_elf_filter_jni_prefix, sym_cb, &st, false);

        std::vector<bc_elf64_sym> syms;
        build_symtab(data, size, reported, &syms);
        // require_defined nos dois valores: o preflight usa true, o
        // generalizador usa false (símbolo importado também conta lá).
        (void)bc_elf_symtab_scan_filtered_ex(
            syms.data(), syms.size(), strtab.data(), reported,
            bc_elf_file_symbol_filter, sym_cb, &st, true);
        (void)bc_elf_symtab_scan_filtered_ex(
            syms.data(), syms.size(), strtab.data(), reported,
            bc_elf_filter_jni_prefix, sym_cb, &st, false);
        (void)bc_elf_symtab_scan_filtered(
            syms.data(), syms.size(), strtab.data(), reported,
            bc_elf_file_symbol_filter, sym_cb, &st);

        // Predicados puros com o input cru. offset/size saem dos primeiros
        // bytes quando existem e são 0 quando o input é vazio — o driver de
        // cobertura chama com size == 0 (o libFuzzer nunca chama: entrada
        // vazia nem entra no corpus), então `data[0]` sem guarda aqui dava
        // SEGV no driver e 0% de cobertura no bc_elf_file.h.
        (void)bc_elf_file_range_ok((off_t)size,
                                   (uint32_t)(size > 0 ? data[0] : 0),
                                   (uint32_t)(size > 1 ? data[1] : 0));
        (void)bc_elf_file_symbol_filter("bc_mod_register", 14);
        (void)bc_elf_file_symbol_filter("bc_mod_registe", 13);
        // A guarda de SONAME: o chamador real (main.cpp) só entrega a string
        // que bc_elf_file_read_soname copiou e NUL-terminou, então os testes
        // usam literais. Passar o ponteiro CRU de data seria inventar um
        // contrato que o produto não tem, e o strstr leria depois do fim do
        // buffer — bug do harness acusando o parser à toa.
        (void)bc_elf_file_soname_is_frida("libfrida-gadget-raw.so");
        (void)bc_elf_file_soname_is_frida("libfrida-agent.so");
        (void)bc_elf_file_soname_is_frida("libkungfux.so");
        (void)bc_elf_file_soname_is_frida("");
        (void)bc_elf_file_soname_is_frida(nullptr);
    }

    // ---- camada B: preflight real sobre arquivo --------------------------
    if (size == 0) return 0;  // arquivo de 0 byte: caminho testado no selftest
    if (ensure_tmp() < 0) return 0;
    reset_tmp();
    if (write(g_fd, data, size) != (ssize_t)size) return 0;

    (void)bc_elf_file_has_bc_mod_register(g_path);
    {
        char soname[128];
        (void)bc_elf_file_read_soname(g_path, soname, sizeof(soname));
        // Buffer de SONAME minúsculo: o nome vem do .dynstr do usuário.
        char tiny[2];
        (void)bc_elf_file_read_soname(g_path, tiny, sizeof(tiny));
    }
    return 0;
}
