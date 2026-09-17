// bc_generic_allowlist.h — controla em quais apps (além do Battle Cats) o
// módulo genérico Cocos2d-x pode ATUAR. Pedido do usuário (2026-09-16):
// "da um jeito dele detectar todos mais so atuar no que voce selecionar".
//
// Detectar em TODO processo do device tem custo real: bc_detect_cocos2dx()
// (sinal 2/3) enumera símbolo ELF, o que soma latência de boot em CADA app
// aberto, não só nos que interessam. Compromisso honesto: só escaneia (e só
// atua) nos pacotes listados aqui — não é "detecta todos, silenciosamente
// grátis", é "você escolhe a lista, e só ela paga o custo de detecção".
//
// Lista em /data/local/tmp/bc_generic_allowlist.conf — um pacote por linha,
// linhas vazias/'#' ignoradas. Editável via push_mod-like write do Termux
// (mesmo canal de companion) ou diretamente por adb shell como root.

#ifndef BC_GENERIC_ALLOWLIST_H
#define BC_GENERIC_ALLOWLIST_H

#include <stdio.h>
#include <string.h>

#define BC_GENERIC_ALLOWLIST_PATH "/data/local/tmp/bc_generic_allowlist.conf"
#define BC_GENERIC_ALLOWLIST_MAX_LINE 256

// Pura, host-testável: dado o conteúdo do arquivo já lido pra memória,
// diz se "pkg" está na lista. Separado do I/O pra poder testar sem device.
static inline bool bc_generic_allowlist_contains_buf(const char *buf, const char *pkg) {
    if (buf == nullptr || pkg == nullptr) return false;
    size_t pkg_len = strlen(pkg);
    const char *line = buf;
    while (*line) {
        const char *eol = strchr(line, '\n');
        size_t len = eol ? (size_t)(eol - line) : strlen(line);
        // trim \r final (arquivo editado no Windows/adb push antigo)
        while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == ' ')) len--;
        size_t start = 0;
        while (start < len && line[start] == ' ') start++;
        if (start < len && line[start] != '#') {
            size_t trimmed_len = len - start;
            if (trimmed_len == pkg_len && strncmp(line + start, pkg, pkg_len) == 0) {
                return true;
            }
        }
        if (!eol) break;
        line = eol + 1;
    }
    return false;
}

#ifdef __ANDROID__
// Lê o arquivo do disco e checa. Fail-safe: arquivo ausente/ilegível → lista
// vazia → nenhum app genérico é atuado (nunca falha aberto).
static inline bool bc_generic_allowlist_contains(const char *pkg) {
    FILE *f = fopen(BC_GENERIC_ALLOWLIST_PATH, "r");
    if (!f) return false;
    char buf[8192];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    return bc_generic_allowlist_contains_buf(buf, pkg);
}
#endif // __ANDROID__

#endif // BC_GENERIC_ALLOWLIST_H
