#include <cstdio>
#include <cstring>
#include "../mods/common/il2cpp_wait.h"

static int g_fail = 0;

static void check(const char *name, bool ok) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) g_fail++;
}

struct fake_clock {
    uint64_t now;
    unsigned sleeps;
    uint32_t late_by;
};

static uint64_t fake_now(void *ctx) {
    return static_cast<fake_clock *>(ctx)->now;
}

static void fake_sleep(void *ctx, uint32_t ms) {
    auto *clock = static_cast<fake_clock *>(ctx);
    clock->now += ms + clock->late_by;
    clock->sleeps++;
}

struct fake_probe {
    unsigned calls;
    unsigned succeeds_on;
};

static bool probe_after(void *ctx) {
    auto *probe = static_cast<fake_probe *>(ctx);
    probe->calls++;
    return probe->succeeds_on != 0 && probe->calls >= probe->succeeds_on;
}

struct wait_log {
    unsigned count;
    bool last_timed_out;
    uint32_t last_elapsed_s;
    char last_phase[64];
    char last_reason[96];
    char last_message[160];
};

static void capture_log(void *ctx, const char *phase, bool timed_out,
                        uint32_t elapsed_s, const char *reason) {
    auto *log = static_cast<wait_log *>(ctx);
    log->count++;
    log->last_timed_out = timed_out;
    log->last_elapsed_s = elapsed_s;
    snprintf(log->last_phase, sizeof(log->last_phase), "%s", phase ? phase : "");
    snprintf(log->last_reason, sizeof(log->last_reason), "%s", reason ? reason : "");
    if (mod_il2cpp_wait_format(log->last_message, sizeof(log->last_message),
                               phase, timed_out, elapsed_s, reason) < 0)
        log->last_message[0] = '\0';
}

struct open_probe {
    void *private_handle;
    void *fallback_handle;
    unsigned private_calls;
    unsigned fallback_calls;
};

static void *try_private_open(void *ctx) {
    auto *open = static_cast<open_probe *>(ctx);
    open->private_calls++;
    return open->private_handle;
}

static void *try_fallback_open(void *ctx) {
    auto *open = static_cast<open_probe *>(ctx);
    open->fallback_calls++;
    return open->fallback_handle;
}

int main() {
    printf("[Caso 79] IL2CPP wait: prazo total, intervalo e logs comuns\n");
    {
        const mod_il2cpp_wait_policy normal = mod_il2cpp_default_wait_policy();
        const mod_il2cpp_wait_policy frida = mod_il2cpp_frida_wait_policy();
        check("boot comum tem prazo total de 240s, polling 200ms e log a cada 10s",
              normal.timeout_ms == 240000 && normal.interval_ms == 200 &&
              normal.report_interval_ms == 10000);
        check("Frida preserva orçamento de 10s e log a cada 5s",
              frida.timeout_ms == 10000 && frida.interval_ms == 200 &&
              frida.report_interval_ms == 5000);
    }
    {
        const mod_il2cpp_wait_policy policy{1000, 200, 500};
        const mod_il2cpp_wait_ops ops{fake_now, fake_sleep};
        fake_clock clock{0, 0, 0};
        fake_probe probe{0, 3};
        wait_log log{};
        const auto window = mod_il2cpp_wait_begin(&policy, &ops, &clock);
        bool ready = mod_il2cpp_wait_until(
            probe_after, &probe, &window, &policy, &ops, &clock,
            capture_log, &log, "esperando libil2cpp", "libil2cpp.so não apareceu");
        check("poll usa o intervalo configurado e detecta a condição",
              ready && clock.sleeps == 2 && clock.now == 400);
        check("log mostra a espera em segundos",
              log.count == 1 && !log.last_timed_out &&
              strcmp(log.last_message, "esperando libil2cpp (0s)") == 0);
    }
    {
        const mod_il2cpp_wait_policy policy{1000, 200, 500};
        const mod_il2cpp_wait_ops ops{fake_now, fake_sleep};
        fake_clock clock{0, 0, 0};
        fake_probe probe{0, 0};
        wait_log log{};
        const auto window = mod_il2cpp_wait_begin(&policy, &ops, &clock);
        bool ready = mod_il2cpp_wait_until(
            probe_after, &probe, &window, &policy, &ops, &clock,
            capture_log, &log, "esperando libil2cpp", "libil2cpp.so não apareceu");
        check("prazo único para a biblioteca termina exatamente em 1s",
              !ready && clock.now == 1000 && clock.sleeps == 5);
        check("timeout informa duração e causa",
              log.last_timed_out && log.last_elapsed_s == 1 &&
              strcmp(log.last_message, "desisti após 1s: libil2cpp.so não apareceu") == 0);
    }
    {
        const mod_il2cpp_wait_policy policy{1000, 200, 500};
        const mod_il2cpp_wait_ops ops{fake_now, fake_sleep};
        fake_clock clock{0, 0, 200};
        fake_probe probe{0, 4};
        wait_log log{};
        const auto window = mod_il2cpp_wait_begin(&policy, &ops, &clock);
        bool ready = mod_il2cpp_wait_until(
            probe_after, &probe, &window, &policy, &ops, &clock,
            capture_log, &log, "esperando libil2cpp", "libil2cpp.so não apareceu");
        check("não aceita biblioteca que só aparece depois do deadline",
              !ready && clock.now > window.deadline_ms && log.last_timed_out);
    }
    {
        const mod_il2cpp_wait_policy policy{2500, 500, 1000};
        const mod_il2cpp_wait_ops ops{fake_now, fake_sleep};
        fake_clock clock{0, 0, 0};
        fake_probe probe{0, 0};
        wait_log log{};
        const auto window = mod_il2cpp_wait_begin(&policy, &ops, &clock);
        bool ready = mod_il2cpp_wait_until(
            probe_after, &probe, &window, &policy, &ops, &clock,
            capture_log, &log, "esperando runtime IL2CPP", "runtime não inicializou");
        check("log de espera repete no intervalo configurado antes do timeout",
              !ready && clock.now == 2500 && log.count == 4 &&
              log.last_timed_out);
    }
    {
        const mod_il2cpp_wait_policy policy{1000, 200, 500};
        const mod_il2cpp_wait_ops ops{fake_now, fake_sleep};
        fake_clock clock{0, 0, 0};
        fake_probe library{0, 4};
        fake_probe domain{0, 0};
        wait_log log{};
        const auto window = mod_il2cpp_wait_begin(&policy, &ops, &clock);
        bool lib_ready = mod_il2cpp_wait_until(
            probe_after, &library, &window, &policy, &ops, &clock,
            capture_log, &log, "esperando libil2cpp", "libil2cpp.so não apareceu");
        bool domain_ready = mod_il2cpp_wait_until(
            probe_after, &domain, &window, &policy, &ops, &clock,
            capture_log, &log, "esperando runtime IL2CPP", "domínio não inicializou");
        check("biblioteca e domínio compartilham o mesmo deadline",
              lib_ready && !domain_ready && clock.now == 1000);
        check("domínio timeout preserva motivo próprio",
              log.last_timed_out &&
              strcmp(log.last_message, "desisti após 1s: domínio não inicializou") == 0);
    }

    printf("\n[Caso 80] detecção do nome da IL2CPP em APK e split APK\n");
    check("base.apk!/lib/.../libil2cpp.so casa",
          mod_il2cpp_name_matches("/data/app/base.apk!/lib/arm64-v8a/libil2cpp.so"));
    check("split_config.arm64_v8a.apk!/lib/.../libil2cpp.so casa",
          mod_il2cpp_name_matches("/data/app/split_config.arm64_v8a.apk!/lib/arm64-v8a/libil2cpp.so"));
    check("libil2cpp.so com sufixo extra não casa",
          !mod_il2cpp_name_matches("/data/app/base.apk!/lib/arm64-v8a/libil2cpp.so.backup"));
    check("nome nulo não casa", !mod_il2cpp_name_matches(nullptr));

    printf("\n[Caso 81] abertura IL2CPP: __loader_dlopen e fallback dlopen\n");
    {
        int private_marker = 1, fallback_marker = 2;
        open_probe open{&private_marker, &fallback_marker, 0, 0};
        enum mod_il2cpp_open_route route = MOD_IL2CPP_OPEN_NONE;
        void *handle = mod_il2cpp_open_with(
            try_private_open, try_fallback_open, &open, &route);
        check("private linker com handle é o caminho usado",
              handle == &private_marker && route == MOD_IL2CPP_OPEN_PRIVATE_LINKER &&
              open.private_calls == 1 && open.fallback_calls == 0);
    }
    {
        int fallback_marker = 2;
        open_probe open{nullptr, &fallback_marker, 0, 0};
        enum mod_il2cpp_open_route route = MOD_IL2CPP_OPEN_NONE;
        void *handle = mod_il2cpp_open_with(
            nullptr, try_fallback_open, &open, &route);
        check("sem __loader_dlopen, fallback dlopen retorna handle e é reportável",
              handle == &fallback_marker && route == MOD_IL2CPP_OPEN_DLOPEN_FALLBACK &&
              open.private_calls == 0 && open.fallback_calls == 1);
    }
    {
        int fallback_marker = 2;
        open_probe open{nullptr, &fallback_marker, 0, 0};
        enum mod_il2cpp_open_route route = MOD_IL2CPP_OPEN_NONE;
        void *handle = mod_il2cpp_open_with(
            try_private_open, try_fallback_open, &open, &route);
        check("falha do private linker também tenta fallback",
              handle == &fallback_marker && route == MOD_IL2CPP_OPEN_DLOPEN_FALLBACK &&
              open.private_calls == 1 && open.fallback_calls == 1);
    }
    {
        char message[160];
        enum mod_il2cpp_open_route route = MOD_IL2CPP_OPEN_PRIVATE_LINKER;
        int n = mod_il2cpp_open_route_format(message, sizeof(message), route, true, true);
        check("log identifica o uso de __loader_dlopen",
              n > 0 && strcmp(message,
                  "caminho usado para abrir libil2cpp: __loader_dlopen") == 0);
        route = MOD_IL2CPP_OPEN_DLOPEN_FALLBACK;
        n = mod_il2cpp_open_route_format(message, sizeof(message), route, false, true);
        check("log identifica fallback dlopen sem linker privado",
              n > 0 && strcmp(message,
                  "__loader_dlopen ausente; caminho usado: dlopen fallback (handle obtido)") == 0);
    }

    printf("\n== Resultado: %s (%d falhas) ==\n",
           g_fail == 0 ? "TODOS PASSARAM" : "HOUVE FALHAS", g_fail);
    return g_fail == 0 ? 0 : 1;
}
