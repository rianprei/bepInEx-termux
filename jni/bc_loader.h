// bc_loader.h — Carregamento dinâmico de mods .so (o "loader de plugins").
//
// O que faz: descobre arquivos *.so num diretório, dlopen() cada um,
// dlsym(bc_mod_register) e chama com a bc_mod_api. Roda DENTRO do processo
// do jogo (main.cpp) — é ali que o libnative-lib.so alvo está carregado e
// onde o Dobby atua; o companion é um processo separado e NÃO consegue
// hookar o jogo.
//
// ISOLAMENTO DE FALHA (honesto, domínio nativo):
//   - dlopen() que falha           → BC_LOAD_ERR_OPEN, skip, segue o próximo
//   - dlsym(bc_mod_register) nulo  → BC_LOAD_ERR_NOSYM, skip (≠ mod deste loader)
//   - entry retorna false          → BC_LOAD_INACTIVE (mod tampado, não é crash)
//   - entry CRASHA no próprio init → NÃO contemos: em domínio nativo um
//     segfault dentro do code do mod mata o processo (igual um DobbyHook mal
//     feito). O loader isola FALHA DE CARGA, não código hostil/crashado.
//
// Testabilidade: primitivas dlopen/dlsym/dlclose + o entry_runner são
// injetados via struct bc_loader_ops. Em device main.cpp passa as reais; no
// selftest_harness passa stubs. A LÓGICA (discovery por sufixo, decisão de
// load, chamada do entry) é o MESMO código nos dois.
//
// Diretório: /data/local/tmp/bc_mods (mesma área do bc_mods.conf). Só a
// raiz, arquivos terminando em ".so", sem oculto. Ordem de carga: o readdir
// é imprevisível → o CALLER deve sort por nome antes de carregar (prefixo
// numérico no nome garante ordem: "01_core.so" antes de "02_extra.so").
// Esse sort é só DESEMPATE determinístico de descoberta — a ordem real de
// carga vem do grafo de dependência (bc_mod_graph.h), aplicado pelo caller.
//
// Contrato de nome pra `requires`/`conflicts` (achado na review do
// hermes): um mod que EXPORTA bc_mod_manifest é referenciado pelo NOME
// declarado no manifest; um mod SEM manifest vira nó independente cujo
// nome é o NOME DO ARQUIVO .so. Um `requires` tem que bater com qual dos
// dois o dependido usa — se o dependido não exporta manifest, o
// dependente deve declarar `requires` = nome do arquivo, não um nome
// arbitrário.

#ifndef BC_LOADER_H
#define BC_LOADER_H

#include <stddef.h>
#include <string.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BC_MODS_DIR "/data/local/tmp/bc_mods"

// Primitivas de DL injetáveis (device: reais; harness: stubs).
typedef void *(*bc_dlopen_fn)(const char *path, int flags);
typedef void *(*bc_dlsym_fn)(void *handle, const char *symbol);
typedef int   (*bc_dlclose_fn)(void *handle);
// Chamador do entry point do mod (no device chama a função resolvida; no
// harness chama um stub e observa se foi invocado). Retorna o que o entry
// retornou (true=carregou, false=inativo).
typedef bool (*bc_entry_runner_fn)(void *api, void *sym);

typedef struct bc_loader_ops {
    bc_dlopen_fn     dlopen;
    bc_dlsym_fn      dlsym;
    bc_dlclose_fn    dlclose;
    bc_entry_runner_fn run_entry;  // invoca (bc_mod_register_fn)sym(api)
} bc_loader_ops;

// Resultado de carregar UM arquivo.
typedef enum {
    BC_LOAD_OK,           // dlopen+dlsym ok; entry foi chamado e retornou true
    BC_LOAD_INACTIVE,     // entry retornou false (mod tampado) — não é erro
    BC_LOAD_ERR_OPEN,     // dlopen falhou (corrompido/ABI/undef refs)
    BC_LOAD_ERR_NOSYM,    // dlsym(bc_mod_register) nulo — não é mod deste loader
} bc_load_status;

typedef struct bc_loaded_mod {
    bc_load_status status;
    const char *path;   // aponta pro buffer do chamador (não copiado aqui)
    void *handle;       // válido se status==BC_LOAD_OK (pra dlclose no unload)
    bool entry_called;  // run_entry foi invocado (independente do retorno)
} bc_loaded_mod;

// Decisão pura e testável: nome é candidato a mod? ".so" na raiz, sem oculto.
static inline bool bc_loader_is_mod_filename(const char *name) {
    if (name == nullptr || name[0] == '\0') return false;
    if (name[0] == '.') return false;               // oculto / . / ..
    // BUG REAL achado por revisão (hermes): sem checar '/', um nome tipo
    // "foo/../../../../data/local/tmp/evil.so" passava aqui (não começa
    // com '.', termina em ".so") — path traversal real, porque
    // companion.cpp::handle_push_mod concatena esse nome direto em
    // BC_MODS_DIR e escreve como ROOT (é o ponto inteiro do push_mod).
    // Mods vivem em diretório FLAT por design (sem subpasta) — qualquer
    // '/' no nome já é inválido, não só sequência ".." especificamente.
    if (strchr(name, '/') != nullptr) return false;
    size_t len = strlen(name);
    if (len < 3) return false;
    return (strcmp(name + len - 3, ".so") == 0);    // termina em ".so"
}

// Carrega UM path .so. NÃO crasha em falha de dlopen/dlsym — retorna status.
// run_entry é chamada SÓ se dlopen+dlsym ok; o valor de retorno dela vira
// OK ou INACTIVE. Chamador é dono do handle (dlclose quando quiser descarregar).
static inline bc_load_status bc_loader_load_one(const bc_loader_ops *ops,
                                                const char *path, void *api,
                                                bc_loaded_mod *out) {
    if (out != nullptr) memset(out, 0, sizeof(*out));
    if (ops == nullptr || ops->dlopen == nullptr || ops->dlsym == nullptr)
        return BC_LOAD_ERR_OPEN;
    void *h = ops->dlopen(path, 2 /*RTLD_NOW*/);
    if (h == nullptr) {
        if (out != nullptr) out->status = BC_LOAD_ERR_OPEN;
        return BC_LOAD_ERR_OPEN;
    }
    void *sym = ops->dlsym(h, "bc_mod_register");
    if (sym == nullptr) {
        if (ops->dlclose != nullptr) ops->dlclose(h);
        if (out != nullptr) out->status = BC_LOAD_ERR_NOSYM;
        return BC_LOAD_ERR_NOSYM;
    }
    bool registered = true;
    bool ran_entry = ops->run_entry != nullptr;
    if (ran_entry) registered = ops->run_entry(api, sym);
    if (out != nullptr) {
        out->status = registered ? BC_LOAD_OK : BC_LOAD_INACTIVE;
        out->handle = h;
        // achado de review: antes marcava true sempre, mesmo quando
        // ops->run_entry era nullptr e a entry NUNCA foi chamada de fato.
        out->entry_called = ran_entry;
        out->path = path;
    }
    return out != nullptr ? out->status : (registered ? BC_LOAD_OK : BC_LOAD_INACTIVE);
}

#ifdef __cplusplus
}
#endif

#endif // BC_LOADER_H