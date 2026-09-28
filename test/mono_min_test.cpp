#include "../jni/mono_min.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures = 0;

static void check(const char *name, bool condition) {
    printf("  [%s] %s\n", condition ? "PASS" : "FAIL", name);
    if (!condition) failures++;
}

struct LogCapture {
    char message[128];
    int count;
};

static void capture_log(void *context, const char *message) {
    LogCapture *capture = static_cast<LogCapture *>(context);
    snprintf(capture->message, sizeof(capture->message), "%s", message);
    capture->count++;
}

typedef unsigned int (*call_count_fn)(void);

static call_count_fn get_call_count(void *handle) {
    return reinterpret_cast<call_count_fn>(dlsym(handle, "mono_test_call_count"));
}

static bool api_is_clear(const bc_mono_api &api) {
    return !api.valid && !api.get_root_domain && !api.thread_attach
            && !api.domain_assembly_open && !api.assembly_get_image
            && !api.class_from_name && !api.class_get_method_from_name
            && !api.runtime_invoke;
}

static void test_resolver(const char *complete_path, const char *missing_path) {
    void *complete = dlopen(complete_path, RTLD_NOW | RTLD_LOCAL);
    void *missing = dlopen(missing_path, RTLD_NOW | RTLD_LOCAL);
    check("abre .so fixture completa", complete != nullptr);
    check("abre .so fixture sem mono_runtime_invoke", missing != nullptr);
    if (!complete || !missing) {
        if (complete) dlclose(complete);
        if (missing) dlclose(missing);
        return;
    }

    LogCapture log = {};
    bc_mono_api api = {};
    bool resolved = bc_mono_resolve_api(complete, &api, capture_log, &log);
    check("resolve os sete símbolos Mono", resolved && api.valid
            && api.get_root_domain && api.thread_attach && api.domain_assembly_open
            && api.assembly_get_image && api.class_from_name
            && api.class_get_method_from_name && api.runtime_invoke);
    call_count_fn complete_calls = get_call_count(complete);
    check("resolução completa não chama nenhuma API Mono",
            complete_calls && complete_calls() == 0);

    memset(&api, 0xA5, sizeof(api));
    log = {};
    resolved = bc_mono_resolve_api(missing, &api, capture_log, &log);
    check("símbolo ausente deixa struct toda inválida",
            !resolved && api_is_clear(api));
    check("símbolo ausente registra qual API falta",
            log.count == 1 && strstr(log.message, "mono_runtime_invoke") != nullptr);
    call_count_fn missing_calls = get_call_count(missing);
    check("falha parcial não chama nenhum símbolo resolvido",
            missing_calls && missing_calls() == 0);

    log = {};
    check("handle nulo também falha de forma atômica",
            !bc_mono_resolve_api(nullptr, &api, capture_log, &log)
                    && api_is_clear(api) && log.count == 1);
    log = {};
    check("ponteiro de saída nulo é recusado",
            !bc_mono_resolve_api(complete, nullptr, capture_log, &log)
                    && log.count == 1 && strstr(log.message, "struct de saída nula") != nullptr);
    bc_mono_api runtime_api = {};
    log = {};
    bool runtime_resolved = bc_mono_resolve_runtime_api(
            complete, &runtime_api, capture_log, &log);
    check("resolução de runtime respeita o guard AArch64",
            runtime_resolved == bc_mono_min_arch_supported()
                    && (runtime_resolved ? runtime_api.valid
                                         : api_is_clear(runtime_api) && log.count == 1));
    dlclose(missing);
    dlclose(complete);
}

static void test_runtime_detection() {
    const char *mono_maps =
            "7f0000-7f1000 r-xp 00000000 00:00 0 /system/lib64/libmonobdwgc-2.0.so\n";
    const char *mono_alias_maps =
            "7f0000-7f1000 r-xp 00000000 00:00 0 /system/lib/libmono.so\n";
    const char *il2cpp_maps =
            "7f0000-7f1000 r-xp 00000000 00:00 0 /data/app/libil2cpp.so\n";
    const char *none_maps =
            "7f0000-7f1000 r-xp 00000000 00:00 0 /system/lib64/libunity.so\n";
    const char *both_maps =
            "7f0000-7f1000 r-xp 00000000 00:00 0 /system/lib64/libmono.so\n"
            "7f1000-7f2000 r-xp 00000000 00:00 0 /data/app/libil2cpp.so\n";
    const char *false_positive_maps =
            "7f0000-7f1000 r-xp 00000000 00:00 0 /data/notlibmono.so.backup\n";
    check("mapa Mono detecta libmonobdwgc-2.0.so",
            bc_mono_runtime_from_maps(mono_maps) == BC_MANAGED_RUNTIME_MONO);
    check("mapa Mono detecta libmono.so",
            bc_mono_runtime_from_maps(mono_alias_maps) == BC_MANAGED_RUNTIME_MONO);
    check("mapa IL2CPP permanece IL2CPP",
            bc_mono_runtime_from_maps(il2cpp_maps) == BC_MANAGED_RUNTIME_IL2CPP);
    check("mapa sem runtime permanece desconhecido",
            bc_mono_runtime_from_maps(none_maps) == BC_MANAGED_RUNTIME_NONE);
    check("mapa com Mono e IL2CPP é ambíguo, não escolhe Mono",
            bc_mono_runtime_from_maps(both_maps) == BC_MANAGED_RUNTIME_AMBIGUOUS);
    check("nome aproximado não é falso positivo",
            bc_mono_runtime_from_maps(false_positive_maps) == BC_MANAGED_RUNTIME_NONE);
}

static void test_long_maps_line() {
    // Linha de maps maior que o buffer de leitura, com a lib no FIM: o
    // nome cortado na fronteira não pode virar NONE falso.
    char tmpl[] = "/tmp/mono-maps-XXXXXX";
    int fd = mkstemp(tmpl);
    check("maps sintético criado", fd >= 0);
    if (fd < 0) return;
    FILE *fp = fdopen(fd, "w");
    check("maps sintético aberto", fp != nullptr);
    if (!fp) {
        close(fd);
        return;
    }
    fputs("7f0000-7f1000 r-xp 00000000 00:00 0 /system/lib64/libunity.so\n", fp);
    // 4070 Xs + " /system/lib64/" (16) = agulha "libmonobdwgc-2.0.so" começa
    // em 4086 e a fronteira do fgets (4095) cai DENTRO dela: nenhum chunk
    // sozinho contém o nome inteiro com fronteira válida.
    for (int i = 0; i < 4070; i++) fputc('X', fp);
    fputs(" /system/lib64/libmonobdwgc-2.0.so\n", fp);
    fclose(fp);
    check("linha > 4096 com a lib no fim detecta Mono",
            bc_mono_runtime_from_maps_file(tmpl) == BC_MANAGED_RUNTIME_MONO);
    unlink(tmpl);

    // Teto de 1MB: linha maior que o teto, mesmo com a lib no fim, é
    // truncada e NÃO casa — sem alocar acima do teto, sem travar.
    char tmpl2[] = "/tmp/mono-maps-big-XXXXXX";
    int fd2 = mkstemp(tmpl2);
    check("maps gigante criado", fd2 >= 0);
    if (fd2 >= 0) {
        FILE *fp2 = fdopen(fd2, "w");
        check("maps gigante aberto", fp2 != nullptr);
        if (fp2) {
            for (size_t i = 0; i < 1024 * 1024 + 128; i++) fputc('Y', fp2);
            fputs(" /system/lib64/libmonobdwgc-2.0.so\n", fp2);
            fclose(fp2);
            check("linha > 1MB com a lib no fim NÃO casa (teto, sem OOM)",
                    bc_mono_runtime_from_maps_file(tmpl2) == BC_MANAGED_RUNTIME_NONE);
        } else {
            close(fd2);
        }
        unlink(tmpl2);
    }
}

int main(int argc, char **argv) {
    printf("[Caso 92] mono_min: resolução atômica e detecção por mapas\n");
    printf("[Caso 94] mono_min: linha de maps maior que o buffer não vira NONE falso\n");
    if (argc != 3) {
        fprintf(stderr, "uso: mono_min_test <complete.so> <missing.so>\n");
        return 2;
    }
    test_resolver(argv[1], argv[2]);
    test_runtime_detection();
    test_long_maps_line();
    check("Mono runtime helper não habilita chamadas em arquitetura não AArch64",
            bc_mono_min_arch_supported() == (
#if defined(__aarch64__)
                    true
#else
                    false
#endif
                    ));
    printf("=== mono_min_test: %d falha(s) ===\n", failures);
    return failures == 0 ? 0 : 1;
}
