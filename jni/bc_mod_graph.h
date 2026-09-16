// bc_mod_graph.h — grafo de dependência entre mods (gap 2 do BepInEx portado).
//
// Conceito (espelho do BepInEx, ver context/bepinex-gaps-bcpoc.md §2):
//   - Declaração: [BepInDependency(GUID, HardDependency)]  → nosso `requires`
//   -             (sem análogo BepInEx — é extensão nossa) → nosso `conflicts`
//   - Ordem: BaseChainloader.ModifyLoadOrder → SortedDictionary + TopologicalSort
//     (Utility.cs joga "Cyclic Dependency")                        → bc_mod_graph_sort
//   - Falha: dependência Hard ausente → plugin NÃO carrega (BaseChainloader.cs
//     356-379: DependencyErrors, sem derrubar os outros)            → status rejected
//
// O QUE É UM "MOD" HOJE (honesto): os 4 hooks fixos do PLANS[] + callbacks
// registrados no dispatcher (bc_hook_logic.h). .so de terceiros continua FORA
// do escopo de segurança (decisión de design, ver HANDOVER.md) — mas o ponto
// de encaixe futuro já existe: um .so de mod declararia o seu manifest com um
// símbolo exportado `bc_mod_manifest` (mesmo struct), e o loader o consumiria
// via dlsym SEM mudar nada aqui. Hoje os manifests são estáticos (main.cpp).
//
// Semântica de rejeição (nunca crasha, nunca derruba os irmãos):
//   MOD_REJ_CONFLICT   — um `conflicts` declarado está presente no conjunto
//   MOD_REJ_MISSING    — um `requires` declarado NÃO está no conjunto
//                        (nem em mods presentes nem em rejected com
//                        requires opcional? NÃO: requires é Hard-only,
//                        igual BepInEx HardDependency. Soft não existe aqui
//                        — quando precisar, vira campo `requires_soft`.)
//   MOD_REJ_CYCLE      — grupo fechado de requires mutuamente insatisfazíveis
//                        (Kahn esgota com nós não-emitidos)
// Um mod rejeitado não entra na ordem de carga; os demais são ordenados
// normalmente (isolamento por mod, igual Pattern 11 de try_install).
//
// Determinismo: desempate é SEMPRE menor índice de declaração primeiro
// (BepInEx usa SortedDictionary pro mesmo fim — BaseChainloader.cs:222-224).
// Ordem estável = mesmo config → mesma sequência de instalação, toda boot.
//
// Host-testável: header puro, sem dependência de Android/Dobby — o
// selftest_harness.cpp inclui este arquivo direto (padrão bc_mods_conf.h).
#ifndef BC_MOD_GRAPH_H
#define BC_MOD_GRAPH_H

#include <stddef.h>
#include <string.h>

#define BC_MOD_NAME_MAX 32     // mesmo limite do bc_mod_entry.name
#define BC_MOD_DEPS_MAX 4      // máx. de requires/conflicts por mod (domínio pequeno)
#ifndef BC_MOD_GRAPH_MAX_MODS
#define BC_MOD_GRAPH_MAX_MODS 16
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Manifest de um mod. Arrays terminados em NULL (não contados) — tolerante a
// declarar menos que BC_MOD_DEPS_MAX. Struct C simples: exportável por um
// futuro .so de mod via dlsym("bc_mod_manifest") sem ABI magic.
struct bc_mod_manifest {
    const char *name;                       // nome canônico (== HookPlan.shortname hoje)
    const char *requires_[BC_MOD_DEPS_MAX]; // todos PRECISEM existir no conjunto final
    const char *conflicts[BC_MOD_DEPS_MAX]; // NENHUM pode existir no conjunto final
};

// Status de resolução por mod (ordem de invalidação do resolver:
// conflict é checado antes de missing — um mod com ambos loga conflict,
// que é a violação mais grave pois envolve dois mods presentes).
enum bc_mod_status {
    BC_MOD_OK = 0,
    BC_MOD_REJ_CONFLICT = 1,
    BC_MOD_REJ_MISSING = 2,
    BC_MOD_REJ_CYCLE = 3,
};

// Resultado: order[i] = índice (na entrada) do i-ésimo mod a carregar;
// status[i] = por que o mod i foi rejeitado (ignorado se BC_MOD_OK).
// n_order = quantos entraram na ordem. Nunca escreve índice inválido.
struct bc_mod_graph_result {
    int order[BC_MOD_GRAPH_MAX_MODS];
    enum bc_mod_status status[BC_MOD_GRAPH_MAX_MODS];
    int n_order;
};

// true se `name` está declarado em algum mod do conjunto (presença explícita).
static inline int bc_mod_graph_declared(const struct bc_mod_manifest *mods, int n,
                                        const char *name) {
    for (int i = 0; i < n; i++)
        if (mods[i].name != nullptr && strcmp(mods[i].name, name) == 0) return 1;
    return 0;
}

// ---- Resolvedor (Kahn determinístico) -------------------------------------
//
// Algoritmo:
//   1. FILTRO de validação (pass única, estática — sem grafo dinâmico ainda):
//      - conflicts: se o alvo está DECLARADO no conjunto → rejeita o
//        declarador (o conflito é do par; rejeita quem DECLAROU, o alvo
//        fica — política simples e auditável: "quem chega depois e briga,
//        sai"; com manifests estáticos a ordem de declaração é do código).
//      - requires: se o alvo NÃO está declarado → rejeita o requerente
//        (Hard-only; o alvo não precisa estar aprovado, só presente).
//   2. ORDENAÇÃO Kahn nos sobreviventes: repete varreduras emitindo o
//      não-emitido de MENOR ÍNDICE cujos requires já estão todos emitidos.
//      Sobrou gente não-emitida no fim → ciclo (ou require de rejeitado
//      por ciclo — para o usuário é a mesma recusa: MOD_REJ_CYCLE).
//
// Sem alocação, sem recursão. O(n²) no pior caso com n<=16: irrelevante no
// boot (roda 1x antes de instalar hooks), e simplicidade > astúcia aqui.
//
// Retorna n_order (>=0). Parâmetros hostis (null/overflow) → resultado vazio,
// nunca crasha (mesmo contrato fail-safe dos outros headers bc_*).
static inline int bc_mod_graph_sort(const struct bc_mod_manifest *mods, int n,
                                    struct bc_mod_graph_result *out) {
    if (out == nullptr) return 0;
    out->n_order = 0;
    for (int i = 0; i < BC_MOD_GRAPH_MAX_MODS; i++) {
        out->order[i] = -1;
        out->status[i] = BC_MOD_OK;
    }
    if (mods == nullptr || n <= 0 || n > BC_MOD_GRAPH_MAX_MODS) return 0;

    // --- fase 1: filtro estático (conflicts + requires ausentes) ---
    unsigned char alive[BC_MOD_GRAPH_MAX_MODS];
    for (int i = 0; i < n; i++) alive[i] = (mods[i].name != nullptr) ? 1u : 0u;

    for (int i = 0; i < n; i++) {
        if (!alive[i]) continue;
        const struct bc_mod_manifest *m = &mods[i];
        // conflicts: alvo declarado no conjunto → declarador sai
        for (int c = 0; c < BC_MOD_DEPS_MAX; c++) {
            const char *cst = m->conflicts[c];
            if (cst == nullptr) break;
            if (bc_mod_graph_declared(mods, n, cst)) {
                alive[i] = 0u;
                out->status[i] = BC_MOD_REJ_CONFLICT;
                break;
            }
        }
        if (!alive[i]) continue;
        // requires: alto NÃO declarado → requerente sai
        for (int r = 0; r < BC_MOD_DEPS_MAX; r++) {
            const char *req = m->requires_[r];
            if (req == nullptr) break;
            if (!bc_mod_graph_declared(mods, n, req)) {
                alive[i] = 0u;
                out->status[i] = BC_MOD_REJ_MISSING;
                break;
            }
        }
    }

    // --- fase 2: Kahn determinístico nos sobreviventes ---
    unsigned char emitted[BC_MOD_GRAPH_MAX_MODS] = {0};
    int emitted_count = 0;
    int alive_count = 0;
    for (int i = 0; i < n; i++) alive_count += alive[i];

    while (emitted_count < alive_count) {
        int picked = -1;
        for (int i = 0; i < n && picked < 0; i++) {
            if (!alive[i] || emitted[i]) continue;
            // todos os requires emitidos? (declaração filtrada já garante
            // que existem no conjunto; aqui só pode depender de EMITIDOS)
            bool ready = true;
            for (int r = 0; r < BC_MOD_DEPS_MAX && ready; r++) {
                const char *req = mods[i].requires_[r];
                if (req == nullptr) break;
                for (int j = 0; j < n; j++) {
                    if (mods[j].name != nullptr && strcmp(mods[j].name, req) == 0) {
                        if (!emitted[j]) ready = false;  // existe mas ainda não emitido
                        break;
                    }
                }
                // (não declarado não acontece aqui: fase 1 rejeitou)
            }
            if (ready) picked = i;
        }
        if (picked < 0) break;  // esgotou: ciclo entre os restantes
        emitted[picked] = 1u;
        out->order[out->n_order++] = picked;
        emitted_count++;
    }

    // restantes vivos não emitidos = ciclo
    for (int i = 0; i < n; i++) {
        if (alive[i] && !emitted[i]) out->status[i] = BC_MOD_REJ_CYCLE;
    }
    return out->n_order;
}

#ifdef __cplusplus
}
#endif

#endif  // BC_MOD_GRAPH_H
