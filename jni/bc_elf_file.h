#ifndef BC_ELF_FILE_H
#define BC_ELF_FILE_H

#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "bc_elf_symtab.h"

typedef enum {
    BC_ELF_FILE_ERROR = -1,
    BC_ELF_FILE_NO_SYMBOL = 0,
    BC_ELF_FILE_HAS_SYMBOL = 1,
} bc_elf_file_result;

typedef struct {
    bc_elf_file_result result;
    int error_number;
} bc_elf_file_probe;

static inline bool bc_elf_file_read_exact(int fd, void *buf, size_t size, off_t offset) {
    char *cursor = (char *)buf;
    while (size > 0) {
        ssize_t n = pread(fd, cursor, size, offset);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) {
            errno = EIO;
            return false;
        }
        cursor += n;
        size -= (size_t)n;
        offset += n;
    }
    return true;
}

static inline bool bc_elf_file_range_ok(off_t file_size, uint64_t offset, uint64_t size) {
    return offset <= (uint64_t)file_size && size <= (uint64_t)file_size - offset;
}

static inline bool bc_elf_file_symbol_filter(const char *name, size_t name_len) {
    static const char wanted[] = "bc_mod_register";
    return name_len == sizeof(wanted) - 1 && memcmp(name, wanted, name_len) == 0;
}

typedef struct {
    bool found;
} bc_elf_file_probe_callback_state;

static inline void bc_elf_file_probe_callback(const char *, uint64_t, void *user) {
    ((bc_elf_file_probe_callback_state *)user)->found = true;
}

static inline bc_elf_file_probe bc_elf_file_has_bc_mod_register(const char *path) {
    static const uint64_t max_section_bytes = 64 * 1024 * 1024;
    bc_elf_file_probe result = {BC_ELF_FILE_ERROR, 0};
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        result.error_number = errno;
        return result;
    }
    struct stat st;
    Elf64_Ehdr ehdr;
    if (fstat(fd, &st) != 0) {
        result.error_number = errno;
        close(fd);
        return result;
    }
    if (st.st_size == 0) {
        result.result = BC_ELF_FILE_NO_SYMBOL;
        close(fd);
        return result;
    }
    if (st.st_size < (off_t)sizeof(ehdr) ||
        !bc_elf_file_read_exact(fd, &ehdr, sizeof(ehdr), 0)) {
        result.error_number = errno;
        close(fd);
        return result;
    }
    if (memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0 ||
        ehdr.e_ident[EI_CLASS] != ELFCLASS64 ||
        ehdr.e_ident[EI_DATA] != ELFDATA2LSB ||
        ehdr.e_shentsize != sizeof(Elf64_Shdr) || ehdr.e_shnum == 0 ||
        ehdr.e_shoff > (uint64_t)st.st_size ||
        (uint64_t)ehdr.e_shnum > ((uint64_t)st.st_size - ehdr.e_shoff) / ehdr.e_shentsize ||
        (uint64_t)ehdr.e_shnum > SIZE_MAX / sizeof(Elf64_Shdr)) {
        result.result = BC_ELF_FILE_NO_SYMBOL;
        close(fd);
        return result;
    }
    size_t shdr_bytes = (size_t)ehdr.e_shnum * sizeof(Elf64_Shdr);
    Elf64_Shdr *shdrs = (Elf64_Shdr *)malloc(shdr_bytes);
    if (shdrs == nullptr || !bc_elf_file_read_exact(fd, shdrs, shdr_bytes, (off_t)ehdr.e_shoff)) {
        result.error_number = errno;
        free(shdrs);
        close(fd);
        return result;
    }
    result.result = BC_ELF_FILE_NO_SYMBOL;
    for (size_t i = 0; i < ehdr.e_shnum; i++) {
        const Elf64_Shdr *symsec = &shdrs[i];
        if (symsec->sh_type != SHT_DYNSYM || symsec->sh_entsize != sizeof(bc_elf64_sym) ||
            symsec->sh_size == 0 || symsec->sh_size > max_section_bytes ||
            symsec->sh_size % symsec->sh_entsize != 0 || symsec->sh_link >= ehdr.e_shnum) continue;
        const Elf64_Shdr *strsec = &shdrs[symsec->sh_link];
        if (strsec->sh_type != SHT_STRTAB || strsec->sh_size == 0 ||
            strsec->sh_size > max_section_bytes ||
            !bc_elf_file_range_ok(st.st_size, symsec->sh_offset, symsec->sh_size) ||
            !bc_elf_file_range_ok(st.st_size, strsec->sh_offset, strsec->sh_size)) continue;
        size_t sym_bytes = (size_t)symsec->sh_size;
        size_t str_bytes = (size_t)strsec->sh_size;
        bc_elf64_sym *symbols = (bc_elf64_sym *)malloc(sym_bytes);
        char *strings = (char *)malloc(str_bytes);
        if (symbols == nullptr || strings == nullptr ||
            !bc_elf_file_read_exact(fd, symbols, sym_bytes, (off_t)symsec->sh_offset) ||
            !bc_elf_file_read_exact(fd, strings, str_bytes, (off_t)strsec->sh_offset)) {
            result.error_number = errno;
            free(strings);
            free(symbols);
            continue;
        }
        bc_elf_file_probe_callback_state state = {};
        bc_elf_symtab_scan_filtered_ex(symbols, sym_bytes / sizeof(*symbols), strings, str_bytes,
                                        bc_elf_file_symbol_filter, bc_elf_file_probe_callback,
                                        &state, true);
        if (state.found) result.result = BC_ELF_FILE_HAS_SYMBOL;
        free(strings);
        free(symbols);
        if (state.found) break;
    }
    free(shdrs);
    close(fd);
    return result;
}

#endif
