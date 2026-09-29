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

static inline bool bc_elf_file_soname_is_frida(const char *soname) {
    return soname != nullptr &&
           (strstr(soname, "frida-gadget") != nullptr ||
            strstr(soname, "frida-agent") != nullptr);
}

typedef struct {
    bool found;
} bc_elf_file_probe_callback_state;

static inline void bc_elf_file_probe_callback(const char *, uint64_t, void *user) {
    ((bc_elf_file_probe_callback_state *)user)->found = true;
}

static inline bc_elf_file_probe bc_elf_file_has_bc_mod_register(const char *path) {
#if UINTPTR_MAX == UINT32_MAX
    (void)path;
    bc_elf_file_probe unsupported = {BC_ELF_FILE_ERROR, ENOTSUP};
    return unsupported;
#else
    static const uint64_t max_section_bytes = 64 * 1024 * 1024;
    bc_elf_file_probe result = {BC_ELF_FILE_ERROR, 0};
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
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
#endif
}

static inline bc_elf_file_probe bc_elf_file_read_soname32(const char *path,
                                                           char *soname,
                                                           size_t soname_size) {
    bc_elf_file_probe result = {BC_ELF_FILE_ERROR, 0};
    if (soname != nullptr && soname_size > 0) soname[0] = '\0';
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        result.error_number = errno;
        return result;
    }
    struct stat st;
    Elf32_Ehdr ehdr;
    if (fstat(fd, &st) != 0) {
        result.error_number = errno;
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
        ehdr.e_ident[EI_CLASS] != ELFCLASS32 ||
        ehdr.e_ident[EI_DATA] != ELFDATA2LSB ||
        ehdr.e_shentsize != sizeof(Elf32_Shdr) || ehdr.e_shnum == 0 ||
        (uint64_t)ehdr.e_shoff > (uint64_t)st.st_size ||
        ehdr.e_shnum > ((uint64_t)st.st_size - ehdr.e_shoff) / ehdr.e_shentsize) {
        result.result = BC_ELF_FILE_NO_SYMBOL;
        close(fd);
        return result;
    }
    size_t shdr_bytes = (size_t)ehdr.e_shnum * sizeof(Elf32_Shdr);
    Elf32_Shdr *shdrs = (Elf32_Shdr *)malloc(shdr_bytes);
    if (shdrs == nullptr ||
        !bc_elf_file_read_exact(fd, shdrs, shdr_bytes, (off_t)ehdr.e_shoff)) {
        result.error_number = errno;
        free(shdrs);
        close(fd);
        return result;
    }
    result.result = BC_ELF_FILE_NO_SYMBOL;
    const uint64_t max_section_bytes = 64 * 1024 * 1024;
    for (size_t i = 0; i < ehdr.e_shnum; i++) {
        const Elf32_Shdr *dynamic = &shdrs[i];
        if (dynamic->sh_type != SHT_DYNAMIC ||
            dynamic->sh_entsize != sizeof(Elf32_Dyn) ||
            dynamic->sh_size == 0 || dynamic->sh_size > max_section_bytes ||
            dynamic->sh_size % dynamic->sh_entsize != 0 ||
            dynamic->sh_link >= ehdr.e_shnum) continue;
        const Elf32_Shdr *strsec = &shdrs[dynamic->sh_link];
        if (strsec->sh_type != SHT_STRTAB || strsec->sh_size == 0 ||
            strsec->sh_size > max_section_bytes ||
            !bc_elf_file_range_ok(st.st_size, dynamic->sh_offset, dynamic->sh_size) ||
            !bc_elf_file_range_ok(st.st_size, strsec->sh_offset, strsec->sh_size)) continue;
        size_t dynamic_bytes = (size_t)dynamic->sh_size;
        size_t str_bytes = (size_t)strsec->sh_size;
        Elf32_Dyn *entries = (Elf32_Dyn *)malloc(dynamic_bytes);
        char *strings = (char *)malloc(str_bytes);
        if (entries == nullptr || strings == nullptr ||
            !bc_elf_file_read_exact(fd, entries, dynamic_bytes, (off_t)dynamic->sh_offset) ||
            !bc_elf_file_read_exact(fd, strings, str_bytes, (off_t)strsec->sh_offset)) {
            result.error_number = errno;
            free(strings);
            free(entries);
            continue;
        }
        for (size_t j = 0; j < dynamic_bytes / sizeof(*entries); j++) {
            uint32_t soname_offset = (uint32_t)entries[j].d_un.d_val;
            if (entries[j].d_tag != DT_SONAME || soname_offset >= str_bytes) continue;
            const char *name = strings + soname_offset;
            size_t max_len = str_bytes - soname_offset;
            size_t name_len = strnlen(name, max_len);
            if (name_len >= max_len) continue;
            if (soname != nullptr && soname_size > 0) {
                size_t copy_len = name_len < soname_size - 1 ? name_len : soname_size - 1;
                memcpy(soname, name, copy_len);
                soname[copy_len] = '\0';
            }
            result.result = BC_ELF_FILE_HAS_SYMBOL;
            free(strings);
            free(entries);
            free(shdrs);
            close(fd);
            return result;
        }
        free(strings);
        free(entries);
    }
    free(shdrs);
    close(fd);
    return result;
}

static inline bc_elf_file_probe bc_elf_file_read_soname(const char *path,
                                                        char *soname,
                                                        size_t soname_size) {
#if UINTPTR_MAX == UINT32_MAX
    return bc_elf_file_read_soname32(path, soname, soname_size);
#else
    static const uint64_t max_section_bytes = 64 * 1024 * 1024;
    bc_elf_file_probe result = {BC_ELF_FILE_ERROR, 0};
    if (soname != nullptr && soname_size > 0) soname[0] = '\0';
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
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
    if (shdrs == nullptr || !bc_elf_file_read_exact(fd, shdrs, shdr_bytes,
                                                     (off_t)ehdr.e_shoff)) {
        result.error_number = errno;
        free(shdrs);
        close(fd);
        return result;
    }
    result.result = BC_ELF_FILE_NO_SYMBOL;
    for (size_t i = 0; i < ehdr.e_shnum; i++) {
        const Elf64_Shdr *dynamic = &shdrs[i];
        if (dynamic->sh_type != SHT_DYNAMIC ||
            dynamic->sh_entsize != sizeof(Elf64_Dyn) ||
            dynamic->sh_size == 0 || dynamic->sh_size > max_section_bytes ||
            dynamic->sh_size % dynamic->sh_entsize != 0 ||
            dynamic->sh_link >= ehdr.e_shnum) continue;
        const Elf64_Shdr *strsec = &shdrs[dynamic->sh_link];
        if (strsec->sh_type != SHT_STRTAB || strsec->sh_size == 0 ||
            strsec->sh_size > max_section_bytes ||
            !bc_elf_file_range_ok(st.st_size, dynamic->sh_offset, dynamic->sh_size) ||
            !bc_elf_file_range_ok(st.st_size, strsec->sh_offset, strsec->sh_size)) continue;
        size_t dynamic_bytes = (size_t)dynamic->sh_size;
        size_t str_bytes = (size_t)strsec->sh_size;
        Elf64_Dyn *entries = (Elf64_Dyn *)malloc(dynamic_bytes);
        char *strings = (char *)malloc(str_bytes);
        if (entries == nullptr || strings == nullptr ||
            !bc_elf_file_read_exact(fd, entries, dynamic_bytes, (off_t)dynamic->sh_offset) ||
            !bc_elf_file_read_exact(fd, strings, str_bytes, (off_t)strsec->sh_offset)) {
            result.error_number = errno;
            free(strings);
            free(entries);
            continue;
        }
        for (size_t j = 0; j < dynamic_bytes / sizeof(*entries); j++) {
            if (entries[j].d_tag != DT_SONAME ||
                entries[j].d_un.d_val >= str_bytes) continue;
            const char *name = strings + entries[j].d_un.d_val;
            size_t max_len = str_bytes - entries[j].d_un.d_val;
            size_t name_len = strnlen(name, max_len);
            if (name_len >= max_len) continue;
            if (soname != nullptr && soname_size > 0) {
                size_t copy_len = name_len < soname_size - 1 ? name_len : soname_size - 1;
                memcpy(soname, name, copy_len);
                soname[copy_len] = '\0';
            }
            result.result = BC_ELF_FILE_HAS_SYMBOL;
            free(strings);
            free(entries);
            free(shdrs);
            close(fd);
            return result;
        }
        free(strings);
        free(entries);
    }
    free(shdrs);
    close(fd);
    return result;
#endif
}

#endif
