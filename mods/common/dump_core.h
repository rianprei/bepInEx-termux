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

// Flags de atributo que interessam pro usuário do dump (escopo/hiding).
// 0x0010 static, 0x0004 public, 0x0001 private, 0x0006 protected, 0x001F visibilidade.
static inline uint32_t dump_attr_static_mask() { return 0x0010; }
static inline uint32_t dump_attr_visibility_mask() { return 0x001F; }

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

// --- pkg (C1 novo): env primeiro, cmdline só fora de zygote* ---
// Loader Zygisk seta BEPINEX_PKG antes do dlopen (contrato C1 2026-09-26+);
// /proc/self/cmdline no constructor do .so ainda é "zygote64" (achado em
// device com o u_patch: a especialização do app_process reescreve argv
// depois do fork). Nada de leitura de arquivo aqui — o chamador passa as
// strings já lidas.
static inline bool dump_zygote_name(const char *n) {
    return n && strncmp(n, "zygote", 6) == 0;  // zygote, zygote64, zygote32
}

// Decide o pkg: env do loader vence; cmdline só se não-vazia e não-zygote.
// false = nada confiável ainda (chamador re-tenta cmdline depois de um
// sleep; fora do constructor o cmdline já virou o do app).
static inline bool dump_pick_pkg(const char *env, const char *cmd, char *out,
                                 size_t cap) {
    if (env && env[0]) { snprintf(out, cap, "%s", env); return true; }
    if (cmd && cmd[0] && !dump_zygote_name(cmd)) {
        snprintf(out, cap, "%s", cmd);
        return true;
    }
    return false;
}
