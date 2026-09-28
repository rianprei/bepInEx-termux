// dump_core.h — núcleo PURO do u_dump (F3): sem Android, sem il2cpp. Tudo
// que dá pra testar em host (g++ -std=c++17) vive aqui; u_dump_mod.cpp só
// cola isso na API il2cpp e no filesystem. Formato C5 é contrato — mudou
// aqui, mudou o docs/ROADMAP-UNIVERSAL.md (e vice-versa).
#pragma once
#include <cstdio>
#include <cstdint>
#include <cstring>

// Formato C5 (dump.tsv):
//   # pkg=<pkg> il2cpp_size=<bytes> unity=<versão se achar>
//   C  <assembly>  <Namespace.Classe>
//   M  <Namespace.Classe>  <método>  <nargs>  <tipo_retorno>  <static 0|1>
//   F  <Namespace.Classe>  <campo>  <tipo>  <static 0|1>  <offset>
// Aninhada (C5 2026-09-26+): <Namespace.Classe> = "Namespace.Externa/Interna"
// — namespace vem da raiz (top-level), caminho de nesting com '/'.
// Separador TAB. Grava-se em "<arquivo>.tmp" e rename() no fim — o leitor
// (Manager) nunca vê dump pela metade, e uma falha no meio da escrita deixa
// o dump.tsv anterior intacto.
static inline int dump_write_header(char *out, size_t cap, const char *pkg,
                                    long long il2cpp_size, const char *unity) {
    return snprintf(out, cap, "# pkg=%s il2cpp_size=%lld unity=%s\n",
                    pkg ? pkg : "", il2cpp_size, unity ? unity : "");
}

// Linha de classe C5. cls já vem montado como "Ns.Nome" (aninhada com
// "/", como no metadata; aninhada é fora do C4 v1, mas o dump mostra tudo).
static inline int dump_write_class(char *out, size_t cap, const char *assembly,
                                   const char *cls) {
    return snprintf(out, cap, "C\t%s\t%s\n", assembly ? assembly : "?", cls);
}

// Monta o nome de classe do C5 com aninhada: "Namespace.Externa/Interna".
// ns = namespace da RAIZ; parts = nomes raiz→fundo (parts[0] = top-level,
// parts[nparts-1] = a própria classe). Sem namespace: "Externa/Interna".
// parte null dentro do caminho vira "?"; sem partes, "?".
static inline int dump_join_class_name(char *out, size_t cap, const char *ns,
                                       const char *const *parts, int nparts) {
    if (!parts || nparts <= 0) {
        snprintf(out, cap, "?");
        return (int)strlen(out);
    }
    size_t used = 0;
    if (ns && ns[0]) {
        int w = snprintf(out, cap, "%s.", ns);
        if (w < 0 || (size_t)w >= cap) { snprintf(out, cap, "?"); return 1; }
        used = (size_t)w;
    }
    for (int i = 0; i < nparts; i++) {
        if (used + 1 >= cap) break;  // truncado (C5 real não deveria chegar aqui)
        int w = snprintf(out + used, cap - used, "%s%s", i ? "/" : "",
                         parts[i] ? parts[i] : "?");
        if (w < 0) break;
        used += (size_t)w;
    }
    out[cap - 1] = '\0';
    return (int)used;
}

// Flag de atributo serializada pelo dump C5: 0x0010 static.
static inline uint32_t dump_attr_static_mask() { return 0x0010; }

// Linha de método C5. method_get_flags devolve flags + iflags (2º out);
// o static vem dos flags de atributo (0x0010), não do iflags.
static inline int dump_write_method(char *out, size_t cap, const char *cls,
                                    const char *method, uint32_t nargs,
                                    const char *ret_type, bool is_static) {
    return snprintf(out, cap, "M\t%s\t%s\t%u\t%s\t%d\n", cls, method, nargs,
                    ret_type, is_static ? 1 : 0);
}

// Linha de campo C5. offset é o que field_get_offset der (0 é offset real
// de estático/literal; C5 não tem representação pra "desconhecido").
static inline int dump_write_field(char *out, size_t cap, const char *cls,
                                   const char *field, const char *type,
                                   bool is_static, int offset) {
    return snprintf(out, cap, "F\t%s\t%s\t%s\t%d\t%d\n", cls, field, type,
                    is_static ? 1 : 0, offset);
}
