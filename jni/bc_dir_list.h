// bc_dir_list.h — a listagem da árvore de mods, em UM ponto só.
//
// ============================================================================
// POR QUE ESTE HEADER EXISTE (S2 da revisão do kilo em companion-followups-4)
// ============================================================================
// As DUAS listagens do companion ("BL" da árvore BC e "mod_list" da genérica)
// tinham o MESMO bloco duplicado: readdir -> filtro de nome -> lstat ->
// S_ISREG -> emitir. Duplicado, o filtro não tinha teste nenhum: o kilo
// trocou o `!S_ISREG(st.st_mode)` por `continue` nas duas e o gate inteiro
// seguiu verde — o código passava a listar FIFO e symlink, e nenhum teste
// de host alcançava a listagem (o req_channel_test substitui os handlers, o
// mods-reloc-test cobre a MIGRAÇÃO, outro caminho).
//
// Agora o bloco existe UMA vez, AQUI, e as duas listagens chamam ISTO. O
// teste de host (test/dir_list_test.cpp) chama o MESMO núcleo com uma
// fixture que tem FIFO, symlink, diretório e arquivo regular: se o S_ISREG
// sair, o fifo.so e o link.so aparecem na lista e o teste FALHA — no gate,
// de agora em diante, não só na revisão.
//
// POR QUE SÓ ARQUIVO REGULAR: o que não é regular NÃO é entregável ao jogo
// — o loader pede por FD (bc_fd_open_ro: O_NOFOLLOW + fstat S_ISREG) o que
// a LISTAGEM diz, e dlopen de fifo/link/pasta não é mod. Listar isso seria
// mandar o jogo pedir algo que a entrega recusa — ou pior, antes do O1,
// entregar. O lstat (não stat) por entrada é o que não segue o link.
//
// Puro de propósito: opendir/readdir/lstat rodam igual no host e no
// device; a única ponta que não dá para testar aqui é o emitter do
// companion (bc_fd_send_data no socket), que é fino de propósito.
#ifndef BC_DIR_LIST_H
#define BC_DIR_LIST_H

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "bc_loader.h"  // bc_loader_is_mod_filename: o filtro de nome é o MESMO do loader

// Callback de emissão: devolve 0 para continuar, != 0 para PARAR a
// enumeração (o companion para quando o send falha — cliente sumiu).
typedef int (*bc_dir_list_emit_fn)(void *ctx, const char *name);

// Lista os .so REGULARES de `dir_path`. Devolve o total EMITIDO (>= 0), ou
// -1 se opendir falhou (quem chama decide: para as listagens do companion,
// pasta ausente é "0\n", não erro — o jogo só não carrega nada).
static inline int bc_dir_list_regular_mods(const char *dir_path,
                                           bc_dir_list_emit_fn emit, void *ctx) {
    if (dir_path == NULL) return -1;
    DIR *d = opendir(dir_path);
    if (d == NULL) return -1;
    int total = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;  // "." ".." e escondidos
        if (!bc_loader_is_mod_filename(de->d_name)) continue;
        char full[1024];
        int fw = snprintf(full, sizeof(full), "%s/%s", dir_path, de->d_name);
        if (fw <= 0 || (size_t)fw >= sizeof(full)) continue;
        struct stat st;
        // lstat + S_ISREG: o FIFO, o symlink e a pasta com cara de .so NÃO
        // entram na lista. Este é o S_ISREG que o kilo removeu sem que
        // nenhum teste pescasse — agora é a linha que o teste vigia.
        if (lstat(full, &st) != 0 || !S_ISREG(st.st_mode)) continue;
        if (emit != NULL && emit(ctx, de->d_name) != 0) break;
        total++;
    }
    closedir(d);
    return total;
}

#endif  // BC_DIR_LIST_H
