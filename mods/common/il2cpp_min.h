// il2cpp_min.h — o mínimo da API il2cpp (exportada pela libil2cpp.so) pra
// mods autônomos do caminho genérico (mods/<pkg>/). Header-only; cada mod
// inclui e chama il2cpp_boot() na thread dele. Biblioteca + runtime compartilham
// um deadline total de 240s; consumidores podem fornecer um limite menor.
#pragma once
#include <dlfcn.h>
#include <link.h>
#include <unistd.h>
#include <cstdint>
#include <cstring>
#include <time.h>
#include "il2cpp_wait.h"
#include "mod_common.h"

struct Il2Cpp {
    void *(*domain_get)();
    void *(*thread_attach)(void *);
    void **(*domain_get_assemblies)(void *, size_t *);
    void *(*assembly_get_image)(void *);
    void *(*class_from_name)(void *, const char *, const char *);
    void *(*class_get_method_from_name)(void *, const char *, int);
    void *(*class_get_field_from_name)(void *, const char *);
    size_t (*field_get_offset)(void *);
    void (*field_static_get_value)(void *, void *);
    void *(*object_get_class)(void *);
    void *(*object_new)(void *);
    void *(*runtime_invoke)(void *, void *, void **, void **);
    void *(*string_new)(const char *);
    // Escrita de estático (u_patch fixa campo static e reaplica a cada 2s).
    void (*field_static_set_value)(void *, void *);
    // Escrita de referência em campo de objeto gerenciado: o GC do Unity 6
    // pode ser incremental, então tem que passar pela write barrier.
    void (*gc_wbarrier_set_field)(void *obj, void **field, void *value);
    void (*field_get_value)(void *obj, void *field, void *out);
    void (*field_set_value)(void *obj, void *field, void *value);
    const void *(*field_get_type)(void *field);
    void *(*class_from_type)(const void *type);
    bool (*class_is_valuetype)(void *klass);
    // Handle forte: segura objeto criado pelo mod contra o GC.
    uint32_t (*gchandle_new)(void *obj, bool pinned);
    // --- Fase F3 (u_dump): enumeração completa pra dump/reflexão. Resolvidos
    // de forma OPCIONAL no boot: um símbolo faltando (il2cpp velho) não pode
    // derrubar os mods que já existem — quem usa checa != nullptr.
    // type_get_name devolve string alocada pelo runtime: copie e solte com
    // free (il2cpp_free).
    const char *(*class_get_name)(void *);
    const char *(*class_get_namespace)(void *);
    void *(*class_get_declaring_type)(void *);
    void *(*class_get_methods)(void *, void **);
    void *(*class_get_fields)(void *, void **);
    const char *(*field_get_name)(void *);
    const char *(*method_get_name)(void *);
    uint32_t (*method_get_param_count)(void *);
    const void *(*method_get_return_type)(void *);
    uint32_t (*method_get_flags)(void *, uint32_t *);
    char *(*type_get_name)(const void *);
    int (*field_get_flags)(void *);
    const char *(*image_get_name)(void *);
    // Tipos exatos do il2cpp-api (BepInEx Il2CppInterop IL2CPP.cs: uint
    // il2cpp_image_get_class_count / IntPtr il2cpp_image_get_class(IntPtr,
    // uint)): size_t leria metade alta indefinida do x0 no arm64.
    uint32_t (*image_get_class_count)(void *);
    void *(*image_get_class)(void *, uint32_t);
    void (*free)(void *);
    // --- Opcionais (u_noads): classes aninhadas e nomes. Resolução
    // TOLERANTE no boot: símbolo faltando (il2cpp velho) só desliga o
    // recurso que usa — quem usa checa != nullptr.
    void *(*class_get_nested_types)(void *, void **);
    void *domain;

    // Classe pelo nome em todas as imagens carregadas.
    void *find_class(const char *ns, const char *name) const {
        size_t n = 0;
        void **asms = domain_get_assemblies(domain, &n);
        for (size_t i = 0; i < n; i++) {
            void *k = class_from_name(assembly_get_image(asms[i]), ns, name);
            if (k) return k;
        }
        return nullptr;
    }
    // Chamada por nome na classe real do objeto (acha override de virtual).
    // Retorna nullptr também quando o método lança exceção (*ok = false).
    void *call(void *obj, const char *method, void **args, int nargs, bool *ok = nullptr) const {
        void *m = class_get_method_from_name(object_get_class(obj), method, nargs);
        void *exc = nullptr;
        void *r = m ? runtime_invoke(m, obj, args, &exc) : nullptr;
        if (ok) *ok = m && !exc;
        return r;
    }
    // Copia um campo de instância de src pra dst. field_set_value segue a
    // semântica do Mono: struct = ponteiro pros bytes, referência = o próprio
    // ponteiro do objeto. Passar &ptr grava o endereço do buffer no campo
    // (achado no device: objeto lixo no campo, SIGSEGV em chamada de interface).
    // ponytail: buffer de 64 bytes, struct maior estoura; usar
    // il2cpp_class_value_size se aparecer campo desses.
    //
    // A cadeia class_from_type -> class_is_valuetype SEM check era o mesmo
    // buraco do type confusion de u_patch (device SA2): qualquer um dos dois
    // devolvendo null entrava no il2cpp com x0 = 0. Aqui a degradação é
    // silenciosa (trata como referência, que é o caso comum) em vez de
    // derrubar o jogo; quem chama decide se um struct é erro.
    void copy_field(void *src, void *dst, void *field) const {
        if (!src || !dst || !field) return;
        alignas(16) uint8_t buf[64];
        field_get_value(src, field, buf);
        bool vt = false;
        if (field_get_type && class_from_type && class_is_valuetype) {
            const void *t = field_get_type(field);
            void *k = t ? class_from_type((void *)t) : nullptr;
            if (k) vt = class_is_valuetype(k);
        }
        field_set_value(dst, field, vt ? (void *)buf : *(void **)buf);
    }
    // Método estático por nome na classe dada.
    void *call_static(void *klass, const char *method, void **args, int nargs, bool *ok = nullptr) const {
        void *m = class_get_method_from_name(klass, method, nargs);
        void *exc = nullptr;
        void *r = m ? runtime_invoke(m, nullptr, args, &exc) : nullptr;
        if (ok) *ok = m && !exc;
        return r;
    }
};

// Ponteiro de código de um método (MethodInfo::methodPointer é o 1º campo).
static inline void *il2cpp_method_ptr(const Il2Cpp &il, void *klass, const char *name, int nargs) {
    void *m = il.class_get_method_from_name(klass, name, nargs);
    return m ? *(void **)m : nullptr;
}

// System.String -> compara com ASCII sem alocar (UTF-16 em +0x14, tamanho em +0x10).
static inline bool il2cpp_str_eq(const void *s, const char *ascii) {
    if (!s) return false;
    int32_t len = *(const int32_t *)((const uint8_t *)s + 0x10);
    const uint16_t *c = (const uint16_t *)((const uint8_t *)s + 0x14);
    for (int32_t i = 0; i < len; i++)
        if (!ascii[i] || c[i] != (uint8_t)ascii[i]) return false;
    return ascii[len] == '\0';
}

static int il2cpp_find_base(struct dl_phdr_info *info, size_t, void *out) {
    if (mod_il2cpp_name_matches(info->dlpi_name)) {
        *(uintptr_t *)out = info->dlpi_addr;
        return 1;
    }
    return 0;
}

using loader_dlopen_t = void *(*)(const char *, int, const void *);

struct il2cpp_open_context {
    loader_dlopen_t private_open;
    uintptr_t base;
};

static void *il2cpp_private_open(void *opaque) {
    auto *ctx = static_cast<il2cpp_open_context *>(opaque);
    return ctx->private_open("libil2cpp.so", RTLD_NOW | RTLD_NOLOAD,
                             reinterpret_cast<const void *>(ctx->base));
}

static void *il2cpp_fallback_open(void *) {
    return dlopen("libil2cpp.so", RTLD_NOW | RTLD_NOLOAD);
}

static inline void *il2cpp_open() {
    uintptr_t base = 0;
    dl_iterate_phdr(il2cpp_find_base, &base);
    if (!base) return nullptr;

    static auto private_open =
        reinterpret_cast<loader_dlopen_t>(dlsym(RTLD_DEFAULT, "__loader_dlopen"));
    il2cpp_open_context ctx{private_open, base};
    enum mod_il2cpp_open_route route = MOD_IL2CPP_OPEN_NONE;
    void *handle = mod_il2cpp_open_with(
        private_open ? il2cpp_private_open : nullptr, il2cpp_fallback_open,
        &ctx, &route);

    static enum mod_il2cpp_open_route logged_route = MOD_IL2CPP_OPEN_NONE;
    char route_message[160];
    int route_message_size = mod_il2cpp_open_route_format(
        route_message, sizeof(route_message), route, private_open != nullptr,
        handle != nullptr);
    static bool fallback_success_logged;
    if (route != logged_route) {
        if (route_message_size > 0) mod_log("il2cpp", "%s", route_message);
        if (route == MOD_IL2CPP_OPEN_DLOPEN_FALLBACK && handle) {
            fallback_success_logged = true;
        }
        logged_route = route;
    } else if (route == MOD_IL2CPP_OPEN_DLOPEN_FALLBACK && handle) {
        if (!fallback_success_logged) {
            mod_log("il2cpp", "%s", route_message);
            fallback_success_logged = true;
        }
    }
    return handle;
}

static uint64_t il2cpp_wait_now_ms(void *) {
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
}

static void il2cpp_wait_sleep_ms(void *, uint32_t ms) {
    usleep((useconds_t)ms * 1000);
}

static void il2cpp_wait_log(void *, const char *phase, bool timed_out,
                            uint32_t elapsed_s, const char *reason) {
    char message[192];
    int n = mod_il2cpp_wait_format(message, sizeof(message), phase,
                                   timed_out, elapsed_s, reason);
    if (n > 0) mod_log("il2cpp", "%s", message);
}

struct il2cpp_library_wait {
    void *handle;
};

static bool il2cpp_library_ready(void *opaque) {
    auto *wait = static_cast<il2cpp_library_wait *>(opaque);
    if (wait->handle) return true;
    wait->handle = il2cpp_open();
    return wait->handle != nullptr;
}

struct il2cpp_corlib_wait {
    void *(*get_corlib)();
};

static bool il2cpp_corlib_ready(void *opaque) {
    auto *wait = static_cast<il2cpp_corlib_wait *>(opaque);
    return wait->get_corlib() != nullptr;
}

static inline void il2cpp_log_boot_failure(
    const mod_il2cpp_wait_window &window, const mod_il2cpp_wait_ops &ops,
    const char *reason) {
    const uint64_t now = ops.now_ms(nullptr);
    const uint64_t elapsed_ms = now >= window.start_ms ? now - window.start_ms : 0;
    const uint32_t elapsed_s = (uint32_t)((elapsed_ms + 999) / 1000);
    il2cpp_wait_log(nullptr, nullptr, true, elapsed_s, reason);
}

static inline bool il2cpp_boot(
    Il2Cpp &il, const mod_il2cpp_wait_policy *requested_policy = nullptr) {
    const mod_il2cpp_wait_policy default_policy = mod_il2cpp_default_wait_policy();
    const mod_il2cpp_wait_policy &policy = requested_policy ? *requested_policy : default_policy;
    const mod_il2cpp_wait_ops ops{il2cpp_wait_now_ms, il2cpp_wait_sleep_ms};
    const mod_il2cpp_wait_window window = mod_il2cpp_wait_begin(&policy, &ops, nullptr);
    il2cpp_library_wait library{nullptr};
    if (!mod_il2cpp_wait_until(
            il2cpp_library_ready, &library, &window, &policy, &ops, nullptr,
            il2cpp_wait_log, nullptr, "esperando libil2cpp",
            "libil2cpp.so não apareceu")) return false;
    void *h = library.handle;

#define IL2CPP_SYM(f) \
    il.f = reinterpret_cast<decltype(il.f)>(dlsym(h, "il2cpp_" #f)); \
    if (!il.f) { il2cpp_log_boot_failure(window, ops, "símbolo il2cpp_" #f " ausente"); return false; }
    IL2CPP_SYM(domain_get); IL2CPP_SYM(thread_attach); IL2CPP_SYM(domain_get_assemblies);
    IL2CPP_SYM(assembly_get_image); IL2CPP_SYM(class_from_name);
    IL2CPP_SYM(class_get_method_from_name); IL2CPP_SYM(class_get_field_from_name);
    IL2CPP_SYM(field_get_offset); IL2CPP_SYM(field_static_get_value); IL2CPP_SYM(object_get_class);
    IL2CPP_SYM(object_new); IL2CPP_SYM(runtime_invoke); IL2CPP_SYM(string_new);
    IL2CPP_SYM(field_static_set_value);
    IL2CPP_SYM(gc_wbarrier_set_field); IL2CPP_SYM(field_get_value); IL2CPP_SYM(field_set_value);
    IL2CPP_SYM(field_get_type); IL2CPP_SYM(class_from_type); IL2CPP_SYM(class_is_valuetype);
    IL2CPP_SYM(gchandle_new);
#undef IL2CPP_SYM

#define IL2CPP_SYM_MAY(f) il.f = reinterpret_cast<decltype(il.f)>(dlsym(h, "il2cpp_" #f))
    IL2CPP_SYM_MAY(class_get_namespace);
    IL2CPP_SYM_MAY(field_get_flags);
    IL2CPP_SYM_MAY(class_get_name); IL2CPP_SYM_MAY(class_get_namespace);
    IL2CPP_SYM_MAY(class_get_declaring_type); IL2CPP_SYM_MAY(class_get_methods);
    IL2CPP_SYM_MAY(class_get_fields); IL2CPP_SYM_MAY(field_get_name);
    IL2CPP_SYM_MAY(method_get_name); IL2CPP_SYM_MAY(method_get_param_count);
    IL2CPP_SYM_MAY(method_get_return_type); IL2CPP_SYM_MAY(method_get_flags);
    IL2CPP_SYM_MAY(type_get_name); IL2CPP_SYM_MAY(field_get_flags);
    IL2CPP_SYM_MAY(image_get_name); IL2CPP_SYM_MAY(image_get_class_count);
    IL2CPP_SYM_MAY(image_get_class); IL2CPP_SYM_MAY(free);
    IL2CPP_SYM_MAY(class_get_nested_types);
#undef IL2CPP_SYM_MAY

    typedef void *(*get_corlib_t)();
    auto get_corlib = reinterpret_cast<get_corlib_t>(dlsym(h, "il2cpp_get_corlib"));
    if (!get_corlib) {
        il2cpp_log_boot_failure(window, ops, "símbolo il2cpp_get_corlib ausente");
        return false;
    }
    il2cpp_corlib_wait corlib{get_corlib};
    if (!mod_il2cpp_wait_until(
            il2cpp_corlib_ready, &corlib, &window, &policy, &ops, nullptr,
            il2cpp_wait_log, nullptr, "esperando runtime IL2CPP",
            "domínio/corlib não inicializou")) return false;
    il.domain = il.domain_get();
    if (!il.domain) {
        il2cpp_log_boot_failure(window, ops, "il2cpp_domain_get retornou nulo");
        return false;
    }
    il.thread_attach(il.domain);
    const uint64_t elapsed_ms = il2cpp_wait_now_ms(nullptr) - window.start_ms;
    mod_log("il2cpp", "runtime pronto após %us", (unsigned)((elapsed_ms + 999) / 1000));
    return true;
}
