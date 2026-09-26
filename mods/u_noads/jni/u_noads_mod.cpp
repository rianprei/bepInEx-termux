#include <android/log.h>
#include <dobby.h>
#include <pthread.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include "../../common/il2cpp_min.h"
#include "u_noads_targets.h"

namespace {

constexpr const char *TAG = "u_noads";
constexpr long MAX_LOG_BYTES = 256 * 1024;

struct Hook {
    void *klass;
    void *show;
    const UNoAdsTarget *target;
    const char *class_name;
    const char *show_name;
    const char *close_name;
    void (*original)(void *, void *);
};

Il2Cpp g_il;
std::vector<Hook> g_hooks;
thread_local std::size_t g_dispatch_index = 0;

const char *pkg() {
    const char *value = std::getenv("BEPINEX_PKG");
    return value && *value ? value : "unknown";
}

void log_line(const char *fmt, ...) {
    char message[768];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);
    __android_log_print(ANDROID_LOG_INFO, TAG, "%s", message);

    char path[512];
    std::snprintf(path, sizeof(path), "/data/data/%s/files/bepinex/log.txt", pkg());
    FILE *file = std::fopen(path, "a+");
    if (!file) return;
    std::fseek(file, 0, SEEK_END);
    long size = std::ftell(file);
    if (size >= MAX_LOG_BYTES) {
        std::fclose(file);
        file = std::fopen(path, "w");
        if (!file) return;
    }
    std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    std::fprintf(file, "%02d:%02d:%02d [%s] %s\n", local.tm_hour, local.tm_min, local.tm_sec,
                 TAG, message);
    std::fclose(file);
}

void *find_class_name(const char *full_name) {
    const char *last_dot = std::strrchr(full_name, '.');
    if (!last_dot) return nullptr;
    std::string ns(full_name, static_cast<std::size_t>(last_dot - full_name));
    return g_il.find_class(ns.c_str(), last_dot + 1);
}

void *find_method(void *klass, const char *const *names, std::size_t count, int args,
                  const char **found_name) {
    for (std::size_t i = 0; i < count; ++i) {
        void *method = g_il.class_get_method_from_name(klass, names[i], args);
        if (method) {
            *found_name = names[i];
            return method;
        }
    }
    return nullptr;
}

void fake_show(void *self, void *arg) {
    (void)arg;
    if (g_dispatch_index >= g_hooks.size()) return;
    const Hook &hook = g_hooks[g_dispatch_index];
    static bool logged = false;
    if (!logged) {
        logged = true;
        log_line("intersticial/app-open suprimido; callback de fechado resolvido");
    }
    bool ok = false;
    if (hook.target->close_is_static)
        g_il.call_static(hook.klass, hook.close_name, nullptr, hook.target->close_args, &ok);
    else
        g_il.call(self, hook.close_name, nullptr, hook.target->close_args, &ok);
    if (!ok)
        log_line("%s: callback %s falhou; hook recusado em proxima execucao", hook.target->label,
                 hook.close_name);
}

#define U_NOADS_TRAMPOLINE(n)                                                    \
    static void fake_show_##n(void *self, void *arg) {                           \
        g_dispatch_index = n;                                                    \
        fake_show(self, arg);                                                     \
    }
U_NOADS_TRAMPOLINE(0)
U_NOADS_TRAMPOLINE(1)
U_NOADS_TRAMPOLINE(2)
U_NOADS_TRAMPOLINE(3)
#undef U_NOADS_TRAMPOLINE

using Trampoline = void (*)(void *, void *);
constexpr Trampoline TRAMPOLINES[] = {fake_show_0, fake_show_1, fake_show_2, fake_show_3};

void try_target(const UNoAdsTarget &target) {
    for (std::size_t class_index = 0; class_index < target.class_count; ++class_index) {
        const char *full_name = target.class_names[class_index];
        void *klass = find_class_name(full_name);
        if (!klass) {
            log_line("%s: classe %s nao encontrada", target.label, full_name);
            continue;
        }
        const char *show_name = nullptr;
        void *show_method = find_method(klass, target.show_names, target.show_count,
                                        target.show_args, &show_name);
        if (!show_method) {
            log_line("%s: %s encontrada, Show intersticial/app-open nao encontrado", target.label,
                     full_name);
            continue;
        }
        const char *close_name = nullptr;
        void *close_method = find_method(klass, target.close_names, target.close_count,
                                         target.close_args, &close_name);
        if (!close_method) {
            log_line("%s: %s encontrada, sem callback de fechado seguro; nao hookando", target.label,
                     full_name);
            continue;
        }
        if (g_hooks.size() >= sizeof(TRAMPOLINES) / sizeof(TRAMPOLINES[0])) {
            log_line("%s: limite de hooks atingido; %s nao hookado", target.label, full_name);
            continue;
        }
        Hook hook{klass, show_method, &target, full_name, show_name, close_name, nullptr};
        const std::size_t index = g_hooks.size();
        if (DobbyHook(show_method, reinterpret_cast<void *>(TRAMPOLINES[index]),
                      reinterpret_cast<void **>(&hook.original)) != 0) {
            log_line("%s: DobbyHook falhou em %s.%s", target.label, full_name, show_name);
            continue;
        }
        g_hooks.push_back(hook);
        log_line("%s: hook ativo em %s.%s; fechamento=%s", target.label, full_name, show_name,
                 close_name);
    }
}

void *worker(void *) {
    if (!il2cpp_boot(g_il)) {
        log_line("il2cpp_boot falhou; nenhum SDK hookado");
        return nullptr;
    }
    log_line("boot ok; pacote=%s; rewarded fora do escopo", pkg());
    for (std::size_t i = 0; i < U_NOADS_TARGET_COUNT; ++i) try_target(U_NOADS_TARGETS[i]);
    return nullptr;
}

} // namespace

__attribute__((constructor)) static void u_noads_init() {
    pthread_t thread;
    if (pthread_create(&thread, nullptr, worker, nullptr) == 0) {
        pthread_detach(thread);
    } else {
        log_line("pthread_create falhou; mod desativado");
    }
}
