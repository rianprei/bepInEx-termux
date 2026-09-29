// bc_launch_check.h — o root só executa arquivo que o root controla.
//
// ============================================================================
// POR QUE ESTE HEADER EXISTE (companion-followups #2)
// ============================================================================
// O console do Termux (bepin-console) é executado pelo companion, que é ROOT,
// via RunCommandService do Termux. O caminho antigo apontava para dentro do
// dado de OUTRO app (/data/data/com.termux/files/home/battlecats-mods/...):
// não reproduzível de um clone e — o pior — um arquivo gravável por appuid.
// Root executando arquivo gravável por app é ESCALADA: qualquer app reescreve
// o script e o root roda o que ele quiser.
//
// O dono do console agora é o MÓDULO (/data/adb/modules/<id>/termux-console/,
// root:root, empacotado pelo build_module.sh — reproduzível de um clone), e
// este check é a barreira de DEFESA EM PROFUNDIDADE antes de executar: se
// qualquer coisa um dia relaxar a permissão do arquivo (bug de empacotamento,
// montagem errada, manipulação direta do /data/adb), o companion RECUSA e
// loga — não executa root em cima de arquivo que app escreve.
//
// Puro de propósito: o MESMO veredito no companion (root) e no teste de host.

#ifndef BC_LAUNCH_CHECK_H
#define BC_LAUNCH_CHECK_H

#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

// O veredito sobre um arquivo que o ROOT está prestes a executar via shell.
// Requisitos (TODOS):
//   - arquivo REGULAR (não symlink, não diretório — O_NOFOLLOW equivalente);
//   - dono uid 0 (root) — appuid como dono é recusa na hora;
//   - NADA gravável por grupo ou outros (mode & 0022): quem não é root não
//     pode ter escrito nele desde a instalação.
// Devolve 0 = pode executar; -1 = recusa (motivo em `why`).
static inline int bc_root_script_ok(const struct stat *st, char *why, size_t whycap) {
    if (st == NULL) {
        snprintf(why, whycap, "stat ausente");
        return -1;
    }
    if (!S_ISREG(st->st_mode)) {
        snprintf(why, whycap,
                 "nao e arquivo regular (symlink/diretorio?) — root nao executa");
        return -1;
    }
    if (st->st_uid != 0) {
        snprintf(why, whycap,
                 "dono e uid %ld, nao root — arquivo gravado por app e escalada",
                 (long)st->st_uid);
        return -1;
    }
    if (st->st_mode & 0022) {
        snprintf(why, whycap,
                 "modo %04o gravavel por grupo/outros — root nao executa o que app reescreve",
                 (unsigned)(st->st_mode & 07777));
        return -1;
    }
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif // BC_LAUNCH_CHECK_H
