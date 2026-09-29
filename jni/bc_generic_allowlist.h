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

#include "bc_loader.h"   // BC_GENERIC_ALLOWLIST_FILE: fonte unica da raiz
#define BC_GENERIC_ALLOWLIST_PATH BC_GENERIC_ALLOWLIST_FILE
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
#    include <unistd.h>
// Lê o arquivo do disco e checa. Fail-safe: arquivo ausente/ilegível → lista
// vazia → nenhum app genérico é atuado (nunca falha aberto).
//
// F1c: testa a existência ANTES do fopen, para o caso normal depois do F1 (a
// pasta de mods basta, a allowlist é opcional) sair em silêncio, sem syscall
// inútil. A leitura em si é permitida: o post-fs-data chcona este arquivo pro
// tipo bepinex_mod_file e o zygote tem getattr/open/read nesse tipo — não
// abrimos shell_data_file:file pra ninguém.
// O JOGO nao abre este arquivo: a arvore e root-only (/data/adb) e ele nao tem
// nem search nela. Quem le e o companion (root), que devolve o CONTEUDO pelo
// socket; o jogo chama bc_generic_allowlist_contains_buf() com o que chegou
// (ver bc_mod_text_request em main.cpp, e a unica chamada agora).
//
// bc_generic_allowlist_contains() foi REMOVIDA de proposito: manter uma
// versao que faz access()+fopen() num caminho que o jogo nao alcanca e deixar
// um leitor morto que parece funcionar.
#endif // __ANDROID__

#endif // BC_GENERIC_ALLOWLIST_H
