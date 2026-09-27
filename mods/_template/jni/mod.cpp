// __MOD_ID__ — template de mod nativo do caminho genérico (mods/<pkg>/).
// Gere um mod novo com: tools/new_mod.sh <id>
//
// Estrutura mínima: o constructor solta uma thread; a thread espera o
// runtime il2cpp subir (il2cpp_boot espera biblioteca + runtime em até 240s
// totais e registra a thread) e loga via mod_log (C1: logcat + /data/data/<pkg>/files/bepinex/
// log.txt). Opções do Manager: mod_conf_get/mod_conf_int/... leem o
// <id>.conf (C3) que o Manager instala junto com o .so.
//
// Pra hookar: inclua "dobby.h" e copie o padrão do mods/sa2ammo (e o bloco
// do dobby pré-built do Android.mk dele). Classe/método por nome saem da
// API il2cpp exportada (il2cpp_min.h) — nada de offset fixo.
#include <pthread.h>
#include "../../common/mod_common.h"
#include "../../common/il2cpp_min.h"

#define TAG "__MOD_ID__"

static void *worker(void *) {
    mod_log(TAG, "carregado, esperando libil2cpp.so");
    Il2Cpp il;
    if (!il2cpp_boot(il)) {
        mod_log(TAG, "boot IL2CPP falhou; motivo detalhado no log IL2CPP");
        return nullptr;
    }
    // Daqui pra frente: il.find_class("<ns>", "Classe") etc. (il2cpp_min.h).
    mod_log(TAG, "il2cpp ok");
    return nullptr;
}

__attribute__((constructor)) static void mod_template_init() {
    pthread_t t;
    if (pthread_create(&t, nullptr, worker, nullptr) == 0) pthread_detach(t);
}
