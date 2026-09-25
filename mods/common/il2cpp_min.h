// il2cpp_min.h — o mínimo da API il2cpp (exportada pela libil2cpp.so) pra
// mods autônomos do caminho genérico (mods/<pkg>/). Header-only; cada mod
// inclui e chama il2cpp_boot() na thread dele.
#pragma once
#include <dlfcn.h>
#include <link.h>
#include <unistd.h>
#include <cstdint>
#include <cstring>

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
    // Escrita de referência em campo de objeto gerenciado: o GC do Unity 6
    // pode ser incremental, então tem que passar pela write barrier.
    void (*gc_wbarrier_set_field)(void *obj, void **field, void *value);
    // Cópia de campo pelo tipo declarado (ref ou struct), sem saber o tamanho.
    void (*field_get_value)(void *obj, void *field, void *out);
    void (*field_set_value)(void *obj, void *field, void *value);
    // Handle forte: segura objeto criado pelo mod contra o GC.
    uint32_t (*gchandle_new)(void *obj, bool pinned);
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
    if (info->dlpi_name && strstr(info->dlpi_name, "/libil2cpp.so")) {
        *(uintptr_t *)out = info->dlpi_addr;
        return 1;
    }
    return 0;
}

// A libil2cpp vive no namespace do classloader do app. Um .so carregado de
// fora (Zygisk, Frida) fica no namespace default, e dlopen("libil2cpp.so")
// não enxerga ela (achado no device: NOLOAD voltava nullptr com a lib já
// mapeada). __loader_dlopen escolhe o namespace pelo endereço do chamador,
// então passar um endereço de dentro da libil2cpp resolve.
static inline void *il2cpp_open() {
    uintptr_t base = 0;
    dl_iterate_phdr(il2cpp_find_base, &base);
    if (!base) return nullptr;
    typedef void *(*loader_dlopen_t)(const char *, int, const void *);
    static auto loader_dlopen = (loader_dlopen_t)dlsym(RTLD_DEFAULT, "__loader_dlopen");
    if (loader_dlopen) {
        void *h = loader_dlopen("libil2cpp.so", RTLD_NOW | RTLD_NOLOAD, (const void *)base);
        if (h) return h;
    }
    return dlopen("libil2cpp.so", RTLD_NOW | RTLD_NOLOAD);
}

// Espera a libil2cpp e o domínio subirem (até ~120s cada), resolve a API e
// registra a thread chamadora no runtime. false = desistiu.
static inline bool il2cpp_boot(Il2Cpp &il) {
    void *h = nullptr;
    for (int i = 0; i < 600 && !h; i++) {
        h = il2cpp_open();
        if (!h) usleep(200 * 1000);
    }
    if (!h) return false;
#define IL2CPP_SYM(f) il.f = (decltype(il.f))dlsym(h, "il2cpp_" #f); if (!il.f) return false
    IL2CPP_SYM(domain_get); IL2CPP_SYM(thread_attach); IL2CPP_SYM(domain_get_assemblies);
    IL2CPP_SYM(assembly_get_image); IL2CPP_SYM(class_from_name);
    IL2CPP_SYM(class_get_method_from_name); IL2CPP_SYM(class_get_field_from_name);
    IL2CPP_SYM(field_get_offset); IL2CPP_SYM(field_static_get_value); IL2CPP_SYM(object_get_class);
    IL2CPP_SYM(object_new); IL2CPP_SYM(runtime_invoke); IL2CPP_SYM(string_new);
    IL2CPP_SYM(gc_wbarrier_set_field); IL2CPP_SYM(field_get_value); IL2CPP_SYM(field_set_value);
    IL2CPP_SYM(gchandle_new);
#undef IL2CPP_SYM
    for (int i = 0; i < 600 && !(il.domain = il.domain_get()); i++) usleep(200 * 1000);
    if (!il.domain) return false;
    il.thread_attach(il.domain);
    return true;
}
