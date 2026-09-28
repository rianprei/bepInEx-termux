// t_crash — mod de TESTE do crashguard (F1d). Mata o processo 2s depois de
// carregar, para provar a garantia G1: 2 mortes seguidas < 60s ⇒ o loader para
// de carregar mods daquele jogo. NÃO instale isso num jogo que você usa.
//
// Uso: coloque libt_crash.so em /data/adb/bepinex/mods/<pkg>/ (via su; a árvore
// é root-only — o t_crash/README tem o passo a passo com o trânsito) e abre o
// jogo. 1ª e 2ª aberturas: morre 2s depois. 3ª: sobe limpo, sem mod, com o
// aviso "mods desativados: o jogo fechou 2x logo depois de carregar" no log.
// Reativar: rm /data/data/<pkg>/files/bepinex/crashguard
// /data/data/<pkg>/files/bepinex/disabled_by_crashguard (ou o botão do Manager).
//
// Janela do crashguard: 20s (era 60s; ver bc_crashguard.h — 60s dava falso
// positivo).
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include "../../common/mod_common.h"

#define TAG "t_crash"
#define LOG(...) mod_log(TAG, __VA_ARGS__)

#define T_CRASH_DELAY_S 2

static void *t_crash_worker(void *) {
    sleep(T_CRASH_DELAY_S);
    LOG("abortando de proposito: teste do crashguard (F1d)");
    abort();
}

__attribute__((constructor)) static void t_crash_init() {
    LOG("carregado: vou abortar em %ds", T_CRASH_DELAY_S);
    pthread_t t;
    if (pthread_create(&t, nullptr, t_crash_worker, nullptr) == 0) pthread_detach(t);
}
