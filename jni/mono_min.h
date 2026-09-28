// mono_min.h — resolução mínima da API Mono e identificação do runtime.
// Esta fatia não carrega assemblies nem chama API Mono no jogo.
#pragma once

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

typedef void *(*bc_mono_get_root_domain_fn)(void);
typedef void *(*bc_mono_thread_attach_fn)(void *domain);
typedef void *(*bc_mono_domain_assembly_open_fn)(void *domain, const char *name);
typedef void *(*bc_mono_assembly_get_image_fn)(void *assembly);
typedef void *(*bc_mono_class_from_name_fn)(void *image, const char *name_space,
                                             const char *name);
typedef void *(*bc_mono_class_get_method_from_name_fn)(void *klass, const char *name,
                                                        int parameter_count);
typedef void *(*bc_mono_runtime_invoke_fn)(void *method, void *object, void **params,
                                            void **exception);

typedef struct bc_mono_api {
    bc_mono_get_root_domain_fn get_root_domain;
    bc_mono_thread_attach_fn thread_attach;
    bc_mono_domain_assembly_open_fn domain_assembly_open;
    bc_mono_assembly_get_image_fn assembly_get_image;
    bc_mono_class_from_name_fn class_from_name;
    bc_mono_class_get_method_from_name_fn class_get_method_from_name;
    bc_mono_runtime_invoke_fn runtime_invoke;
    bool valid;
} bc_mono_api;

typedef void (*bc_mono_log_fn)(void *context, const char *message);

static inline void bc_mono_log_message(bc_mono_log_fn log, void *log_context,
                                       const char *message) {
    if (log) log(log_context, message);
    else fprintf(stderr, "%s\n", message);
}

static inline void bc_mono_clear_api(bc_mono_api *api) {
    if (api) memset(api, 0, sizeof(*api));
}

static inline bool bc_mono_resolve_api(void *loaded_handle, bc_mono_api *api,
                                       bc_mono_log_fn log, void *log_context) {
    if (!api) {
        bc_mono_log_message(log, log_context, "Mono API: struct de saída nula");
        return false;
    }
    bc_mono_clear_api(api);
    if (!loaded_handle) {
        bc_mono_log_message(log, log_context, "Mono API: handle carregado nulo");
        return false;
    }

    bc_mono_api resolved = {};
    resolved.get_root_domain =
            (bc_mono_get_root_domain_fn)dlsym(loaded_handle, "mono_get_root_domain");
    resolved.thread_attach =
            (bc_mono_thread_attach_fn)dlsym(loaded_handle, "mono_thread_attach");
    resolved.domain_assembly_open =
            (bc_mono_domain_assembly_open_fn)dlsym(loaded_handle, "mono_domain_assembly_open");
    resolved.assembly_get_image =
            (bc_mono_assembly_get_image_fn)dlsym(loaded_handle, "mono_assembly_get_image");
    resolved.class_from_name =
            (bc_mono_class_from_name_fn)dlsym(loaded_handle, "mono_class_from_name");
    resolved.class_get_method_from_name =
            (bc_mono_class_get_method_from_name_fn)dlsym(
                    loaded_handle, "mono_class_get_method_from_name");
    resolved.runtime_invoke =
            (bc_mono_runtime_invoke_fn)dlsym(loaded_handle, "mono_runtime_invoke");

    const char *missing = nullptr;
    if (!resolved.get_root_domain) missing = "mono_get_root_domain";
    else if (!resolved.thread_attach) missing = "mono_thread_attach";
    else if (!resolved.domain_assembly_open) missing = "mono_domain_assembly_open";
    else if (!resolved.assembly_get_image) missing = "mono_assembly_get_image";
    else if (!resolved.class_from_name) missing = "mono_class_from_name";
    else if (!resolved.class_get_method_from_name) missing = "mono_class_get_method_from_name";
    else if (!resolved.runtime_invoke) missing = "mono_runtime_invoke";

    if (missing) {
        char message[128];
        snprintf(message, sizeof(message), "Mono API incompleta: falta %s", missing);
        bc_mono_log_message(log, log_context, message);
        return false;
    }

    resolved.valid = true;
    *api = resolved;
    return true;
}

typedef enum bc_managed_runtime {
    BC_MANAGED_RUNTIME_NONE = 0,
    BC_MANAGED_RUNTIME_MONO = 1,
    BC_MANAGED_RUNTIME_IL2CPP = 2,
    BC_MANAGED_RUNTIME_AMBIGUOUS = 3,
} bc_managed_runtime;

static inline bool bc_mono_maps_has_library(const char *maps, const char *library) {
    if (!maps || !library) return false;
    const size_t library_len = strlen(library);
    const char *candidate = maps;
    while ((candidate = strstr(candidate, library)) != nullptr) {
        const bool starts_path = candidate == maps || candidate[-1] == '/';
        const char after = candidate[library_len];
        const bool ends_path = after == '\0' || after == ' ' || after == '\t'
                || after == '\r' || after == '\n';
        if (starts_path && ends_path) return true;
        candidate += library_len;
    }
    return false;
}

static inline bc_managed_runtime bc_mono_runtime_from_maps(const char *maps) {
    const bool mono = bc_mono_maps_has_library(maps, "libmonobdwgc-2.0.so")
            || bc_mono_maps_has_library(maps, "libmono.so");
    const bool il2cpp = bc_mono_maps_has_library(maps, "libil2cpp.so");
    if (mono && il2cpp) return BC_MANAGED_RUNTIME_AMBIGUOUS;
    if (mono) return BC_MANAGED_RUNTIME_MONO;
    if (il2cpp) return BC_MANAGED_RUNTIME_IL2CPP;
    return BC_MANAGED_RUNTIME_NONE;
}

static inline bc_managed_runtime bc_mono_runtime_from_maps_file(const char *maps_path) {
    if (!maps_path) return BC_MANAGED_RUNTIME_NONE;
    FILE *maps = fopen(maps_path, "r");
    if (!maps) return BC_MANAGED_RUNTIME_NONE;

    char line[4096];
    bool mono = false;
    bool il2cpp = false;
    while (fgets(line, sizeof(line), maps)) {
        mono = mono || bc_mono_maps_has_library(line, "libmonobdwgc-2.0.so")
                || bc_mono_maps_has_library(line, "libmono.so");
        il2cpp = il2cpp || bc_mono_maps_has_library(line, "libil2cpp.so");
        if (mono && il2cpp) break;
    }
    fclose(maps);
    if (mono && il2cpp) return BC_MANAGED_RUNTIME_AMBIGUOUS;
    if (mono) return BC_MANAGED_RUNTIME_MONO;
    if (il2cpp) return BC_MANAGED_RUNTIME_IL2CPP;
    return BC_MANAGED_RUNTIME_NONE;
}

static inline bc_managed_runtime bc_mono_runtime_from_process_maps(void) {
    return bc_mono_runtime_from_maps_file("/proc/self/maps");
}

static inline bool bc_mono_min_arch_supported(void) {
#if !defined(__aarch64__)
    return false;
#else
    return true;
#endif
}

static inline bool bc_mono_resolve_runtime_api(void *loaded_handle, bc_mono_api *api,
                                               bc_mono_log_fn log, void *log_context) {
#if !defined(__aarch64__)
    bc_mono_clear_api(api);
    bc_mono_log_message(log, log_context, "Mono runtime não suportado em 32-bit");
    (void)loaded_handle;
    return false;
#else
    return bc_mono_resolve_api(loaded_handle, api, log, log_context);
#endif
}
