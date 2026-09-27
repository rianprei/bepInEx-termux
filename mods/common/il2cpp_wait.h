#ifndef MOD_IL2CPP_WAIT_H
#define MOD_IL2CPP_WAIT_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct mod_il2cpp_wait_policy {
    uint32_t timeout_ms;
    uint32_t interval_ms;
    uint32_t report_interval_ms;
};

struct mod_il2cpp_wait_window {
    uint64_t start_ms;
    uint64_t deadline_ms;
};

struct mod_il2cpp_wait_ops {
    uint64_t (*now_ms)(void *ctx);
    void (*sleep_ms)(void *ctx, uint32_t ms);
};

typedef bool (*mod_il2cpp_wait_probe_fn)(void *ctx);
typedef void (*mod_il2cpp_wait_log_fn)(void *ctx, const char *phase,
                                       bool timed_out, uint32_t elapsed_s,
                                       const char *reason);
typedef void *(*mod_il2cpp_open_fn)(void *ctx);

enum mod_il2cpp_open_route {
    MOD_IL2CPP_OPEN_NONE,
    MOD_IL2CPP_OPEN_PRIVATE_LINKER,
    MOD_IL2CPP_OPEN_DLOPEN_FALLBACK
};

static inline struct mod_il2cpp_wait_policy mod_il2cpp_default_wait_policy(void) {
    struct mod_il2cpp_wait_policy policy = {240000, 200, 10000};
    return policy;
}

static inline struct mod_il2cpp_wait_policy mod_il2cpp_frida_wait_policy(void) {
    struct mod_il2cpp_wait_policy policy = {10000, 200, 5000};
    return policy;
}

static inline struct mod_il2cpp_wait_window mod_il2cpp_wait_begin(
    const struct mod_il2cpp_wait_policy *policy,
    const struct mod_il2cpp_wait_ops *ops, void *clock_ctx) {
    struct mod_il2cpp_wait_window window = {0, 0};
    if (!policy || !ops || !ops->now_ms) return window;
    window.start_ms = ops->now_ms(clock_ctx);
    window.deadline_ms = window.start_ms + policy->timeout_ms;
    if (window.deadline_ms < window.start_ms) window.deadline_ms = UINT64_MAX;
    return window;
}

static inline bool mod_il2cpp_wait_until(
    mod_il2cpp_wait_probe_fn probe, void *probe_ctx,
    const struct mod_il2cpp_wait_window *window,
    const struct mod_il2cpp_wait_policy *policy,
    const struct mod_il2cpp_wait_ops *ops, void *clock_ctx,
    mod_il2cpp_wait_log_fn log, void *log_ctx,
    const char *phase, const char *timeout_reason) {
    if (!probe || !window || !policy || !ops || !ops->now_ms ||
        !ops->sleep_ms || policy->interval_ms == 0) return false;

    bool reported = false;
    uint64_t last_report_ms = window->start_ms;
    for (;;) {
        const uint64_t before_probe_ms = ops->now_ms(clock_ctx);
        if (before_probe_ms > window->deadline_ms) {
            if (log) {
                const uint64_t elapsed_ms = before_probe_ms >= window->start_ms
                    ? before_probe_ms - window->start_ms : 0;
                log(log_ctx, phase, true,
                    (uint32_t)((elapsed_ms + 999) / 1000), timeout_reason);
            }
            return false;
        }
        if (probe(probe_ctx)) return true;
        const uint64_t now = ops->now_ms(clock_ctx);
        const uint64_t elapsed_ms = now >= window->start_ms
            ? now - window->start_ms : 0;
        if (now >= window->deadline_ms) {
            if (log) {
                const uint32_t elapsed_s = (uint32_t)((elapsed_ms + 999) / 1000);
                log(log_ctx, phase, true, elapsed_s, timeout_reason);
            }
            return false;
        }
        if (log && (!reported || now - last_report_ms >= policy->report_interval_ms)) {
            const uint32_t elapsed_s = (uint32_t)(elapsed_ms / 1000);
            log(log_ctx, phase, false, elapsed_s, nullptr);
            last_report_ms = now;
            reported = true;
        }
        uint64_t remaining = window->deadline_ms - now;
        uint32_t sleep_ms = policy->interval_ms;
        if (remaining < sleep_ms) sleep_ms = (uint32_t)remaining;
        if (sleep_ms == 0) continue;
        ops->sleep_ms(clock_ctx, sleep_ms);
    }
}

static inline int mod_il2cpp_wait_format(char *out, size_t cap,
                                         const char *phase, bool timed_out,
                                         uint32_t elapsed_s,
                                         const char *reason) {
    if (!out || cap == 0) return -1;
    int n = timed_out
        ? snprintf(out, cap, "desisti após %us: %s", elapsed_s,
                   reason ? reason : "motivo desconhecido")
        : snprintf(out, cap, "%s (%us)", phase ? phase : "esperando libil2cpp",
                   elapsed_s);
    return n > 0 && (size_t)n < cap ? n : -1;
}

static inline bool mod_il2cpp_name_matches(const char *name) {
    static const char suffix[] = "/libil2cpp.so";
    if (!name) return false;
    const size_t name_len = strlen(name);
    const size_t suffix_len = sizeof(suffix) - 1;
    return name_len >= suffix_len &&
           strcmp(name + name_len - suffix_len, suffix) == 0;
}

static inline void *mod_il2cpp_open_with(
    mod_il2cpp_open_fn private_open, mod_il2cpp_open_fn fallback_open,
    void *ctx, enum mod_il2cpp_open_route *route) {
    if (route) *route = MOD_IL2CPP_OPEN_NONE;
    if (private_open) {
        void *handle = private_open(ctx);
        if (handle) {
            if (route) *route = MOD_IL2CPP_OPEN_PRIVATE_LINKER;
            return handle;
        }
    }
    if (!fallback_open) return nullptr;
    void *handle = fallback_open(ctx);
    if (route) *route = MOD_IL2CPP_OPEN_DLOPEN_FALLBACK;
    return handle;
}

static inline int mod_il2cpp_open_route_format(
    char *out, size_t cap, enum mod_il2cpp_open_route route,
    bool private_available, bool handle_obtained) {
    if (!out || cap == 0) return -1;
    if (route == MOD_IL2CPP_OPEN_PRIVATE_LINKER) {
        int n = snprintf(out, cap,
                         "caminho usado para abrir libil2cpp: __loader_dlopen");
        return n > 0 && (size_t)n < cap ? n : -1;
    }
    if (route == MOD_IL2CPP_OPEN_DLOPEN_FALLBACK) {
        int n = snprintf(out, cap,
                         "__loader_dlopen %s; caminho usado: dlopen fallback%s",
                         private_available ? "falhou" : "ausente",
                         handle_obtained ? " (handle obtido)" : " (sem handle)");
        return n > 0 && (size_t)n < cap ? n : -1;
    }
    out[0] = '\0';
    return 0;
}

#endif
