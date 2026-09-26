// u_noads — supressão forçada de interstitial/app-open em runtime (F11b).
// rewarded fica de fora por escopo. Pra cada Show suprimido, dispara o
// MESMO caminho que o SDK usa ao fechar (delegates/eventos atuais lidos na
// hora — nunca método inventado; ver ADAPTERS.md por SDK: alvo, assinatura,
// fonte oficial, como fecha). Se o fechamento falhar, REPASSA pro original
// (o anúncio aparece; o jogo nunca trava). Sem PC, sem patch de APK.
#include <android/log.h>
#include <dobby.h>
#include <pthread.h>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include "../../common/il2cpp_min.h"
#include "u_noads_pure.h"
#include "u_noads_targets.h"

// --- closers no escopo GLOBAL (fora de namespace{} anônimo): a tabela em
// u_noads_targets.h guarda o endereço deles, então declaração e definição
// têm que ser o mesmo símbolo. Definidos em anônimo seriam outros símbolos:
// o clang acusa unused e o link falharia com undefined reference.
static bool uno_read_ifield(const Il2Cpp &il, void *klass, void *obj, const char *name,
                            void *&out) {
    void *f = il.class_get_field_from_name(klass, name);
    if (!f) return false;
    il.field_get_value(obj, f, &out);
    return true;
}

static bool uno_read_sfield(const Il2Cpp &il, void *klass, const char *name, void *&out) {
    void *f = il.class_get_field_from_name(klass, name);
    if (!f) return false;
    il.field_static_get_value(f, &out);
    return true;
}

// Chama Delegate.Invoke com N args. Delegate null = sem inscritos: nada a
// notificar, sucesso (suprime sem disparar).
static bool uno_invoke(const Il2Cpp &il, void *dlg, void **args, int nargs) {
    if (!dlg) return true;
    bool ok = false;
    il.call(dlg, "Invoke", args, nargs, &ok);
    return ok;
}

static void *uno_event_args(const Il2Cpp &il) {
    void *k = il.find_class("System", "EventArgs");
    return k ? il.object_new(k) : nullptr;
}

// Enum boxed via membro estático (sem chutar valor numérico).
static void *uno_boxed_enum(const Il2Cpp &il, const char *ns, const char *klass,
                            const char *member) {
    void *k = il.find_class(ns, klass);
    if (!k) return nullptr;
    void *boxed = nullptr;
    return uno_read_sfield(il, k, member, boxed) ? boxed : nullptr;
}

// Classe aninhada C# ("Fora/Dentro"): acha a externa e percorre nested.
static void *uno_find_nested(const Il2Cpp &il, const char *outer_fqn, const char *nested) {
    if (!il.class_get_nested_types || !il.class_get_name) return nullptr;
    char ns[128], nm[128];
    uno_split(outer_fqn, ns, sizeof(ns), nm, sizeof(nm));
    void *outer = il.find_class(ns, nm);
    if (!outer) return nullptr;
    void *iter = nullptr, *k = nullptr;
    while ((k = il.class_get_nested_types(outer, &iter)) != nullptr) {
        const char *n = il.class_get_name(k);
        if (n && std::strcmp(n, nested) == 0) return k;
    }
    return nullptr;
}

// --- closers (um por SDK; fontes e assinaturas em ADAPTERS.md) ---

bool uno_close_google(UNoAdsFireCtx &c) {
    const char *fields[] = {"OnAdClosed", "OnAdDidDismissFullScreenContent"};
    void *dlg = nullptr;
    for (int i = 0; i < 2 && !dlg; i++) uno_read_ifield(*c.il, c.klass, c.self, fields[i], dlg);
    if (!dlg) return true;
    void *args[2] = {c.self, uno_event_args(*c.il)};
    return uno_invoke(*c.il, dlg, args, 2);
}

bool uno_close_unity(UNoAdsFireCtx &c) {
    // listener = último arg do Show; adUnit = arg0 (string do jogo).
    void *listener = c.args[c.show_argc - 1];
    void *ad_unit = c.args[0];
    if (!listener || !ad_unit) return false;
    void *completed =
        uno_boxed_enum(*c.il, "UnityEngine.Advertisements", "UnityAdsShowCompletionState",
                       "COMPLETED");
    if (!completed) return false;
    void *args[2] = {ad_unit, completed};
    bool ok = false;
    c.il->call(listener, "OnUnityAdsShowComplete", args, 2, &ok);
    return ok;
}

bool uno_close_levelplay(UNoAdsFireCtx &c) {
    void *dlg = nullptr;
    uno_read_ifield(*c.il, c.klass, c.self, "OnAdClosed", dlg);
    if (!dlg) return true;
    void *args[1] = {nullptr};
    return uno_invoke(*c.il, dlg, args, 1);
}

static bool uno_close_max_in(UNoAdsFireCtx &c, const char *nested) {
    void *cb = uno_find_nested(*c.il, "MaxSdkCallbacks", nested);
    if (!cb) return false;
    void *action = nullptr;
    const char *fields[] = {"onAdHiddenEvent", "OnAdHiddenEvent"};
    for (int i = 0; i < 2 && !action; i++) uno_read_sfield(*c.il, cb, fields[i], action);
    if (!action) return true;
    void *args[2] = {c.args[0], nullptr};
    return uno_invoke(*c.il, action, args, 2);
}

bool uno_close_max_interstitial(UNoAdsFireCtx &c) { return uno_close_max_in(c, "Interstitial"); }
bool uno_close_max_appopen(UNoAdsFireCtx &c) { return uno_close_max_in(c, "AppOpen"); }

bool uno_close_metica(UNoAdsFireCtx &c) {
    void *cb = uno_find_nested(*c.il, "Metica.Ads.MeticaAdsCallbacks", "Interstitial");
    if (!cb) return false;
    void *action = nullptr;
    uno_read_sfield(*c.il, cb, "OnAdHidden", action);
    if (!action) return true;
    void *ad = nullptr;
    void *ad_klass = c.il->find_class("Metica.Ads", "MeticaAd");
    if (ad_klass) ad = c.il->object_new(ad_klass);
    void *args[1] = {ad};
    return uno_invoke(*c.il, action, args, 1);
}

namespace {

constexpr const char *TAG = "u_noads";
constexpr unsigned UF_LOG_MAX = 256u * 1024u;
constexpr unsigned UF_LOG_KEEP = 128u * 1024u;

struct Hook {
    void *klass;
    void *show;
    const char *label;
    const char *show_name;
    int show_argc;
    UNoAdsCloser closer;  // nullptr = Show retorna bool (Meta)
    // original com 5 slots: cobre Show 0..3 args + MethodInfo (fake recebe
    // (self,a,b,c,d); o que o método real não lê é lixo inofensivo).
    bool (*original)(void *, void *, void *, void *, void *);
    // Plain bool de propósito: escritor único (worker, após instalar tudo),
    // leitores toleram valor velho (false = repassa = direção segura).
    bool active;
};

Il2Cpp g_il;
std::vector<Hook> g_hooks;

const char *pkg() {
    const char *value = std::getenv("BEPINEX_PKG");
    return value && *value ? value : "unknown";
}

// Log C1: logcat + append em log.txt. files/ 0771, bepinex/ 0700, teto
// 256KB mantendo a metade final, linha num write() único com O_APPEND.
void log_line(const char *fmt, ...) {
    char message[768];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);
    __android_log_print(ANDROID_LOG_INFO, TAG, "%s", message);

    char files[384], dir[384], path[512];
    std::snprintf(files, sizeof(files), "/data/data/%s/files", pkg());
    std::snprintf(dir, sizeof(dir), "%s/bepinex", files);
    std::snprintf(path, sizeof(path), "%s/log.txt", dir);
    chmod(files, 0771);
    mkdir(dir, 0700);
    struct stat st;
    if (stat(path, &st) == 0 && (uint64_t)st.st_size > UF_LOG_MAX) {
        FILE *rf = std::fopen(path, "r");
        if (rf) {
            char *tail = (char *)std::malloc(UF_LOG_KEEP);
            std::size_t got = 0;
            if (tail) {
                std::fseek(rf, st.st_size - (off_t)UF_LOG_KEEP, SEEK_SET);
                got = std::fread(tail, 1, UF_LOG_KEEP, rf);
            }
            std::fclose(rf);
            FILE *wf = std::fopen(path, "w");
            if (wf) {
                if (got) std::fwrite(tail, 1, got, wf);
                std::fclose(wf);
            }
            std::free(tail);
        }
    }
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) return;
    std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    char line[896];
    int n = std::snprintf(line, sizeof(line), "%02d:%02d:%02d [%s] %s\n", local.tm_hour,
                           local.tm_min, local.tm_sec, TAG, message);
    if (n > 0) {
        std::size_t len = (std::size_t)n < sizeof(line) ? (std::size_t)n : sizeof(line) - 1;
        ::write(fd, line, len);  // write único
    }
    close(fd);
}

// Anexa a thread atual ao runtime (idempotente em thread já anexada).
// Todo runtime_invoke vindo de thread do jogo passa daqui: sem attach,
// il2cpp crasha (achado da revisão).
void attach() { g_il.thread_attach(g_il.domain); }

// (helpers de closer vivem no escopo global, acima do namespace {})

bool dispatch(std::size_t i, void *self, void *a, void *b, void *c, void *d) {
    if (i >= g_hooks.size()) return false;
    Hook &h = g_hooks[i];
    if (!h.active) return h.original(self, a, b, c, d);
    attach();
    UNoAdsFireCtx ctx{&g_il, h.klass, self, {a, b, c, d}, h.show_argc};
    // closer null (Meta): Show retorna bool — false = caminho "sem fill".
    bool closed = h.closer ? h.closer(ctx) : true;
    if (!closed) {
        h.active = false;  // auto-cura: cumpre o que o log promete
        log_line("%s: fechamento falhou; hook recusado, anúncio volta a aparecer", h.label);
        return h.original(self, a, b, c, d);
    }
    static bool logged = false;
    if (!logged) {
        logged = true;
        log_line("intersticial/app-open suprimido com fechamento do próprio SDK");
    }
    return false;  // void ignora; Meta lê false = não exibido
}

#define U_NOADS_TRAMPOLINE(n)                                                        \
    static bool fake_show_##n(void *self, void *a, void *b, void *c, void *d) {       \
        return dispatch(n, self, a, b, c, d);                                         \
    }
U_NOADS_TRAMPOLINE(0)
U_NOADS_TRAMPOLINE(1)
U_NOADS_TRAMPOLINE(2)
U_NOADS_TRAMPOLINE(3)
U_NOADS_TRAMPOLINE(4)
U_NOADS_TRAMPOLINE(5)
U_NOADS_TRAMPOLINE(6)
U_NOADS_TRAMPOLINE(7)
U_NOADS_TRAMPOLINE(8)
U_NOADS_TRAMPOLINE(9)
U_NOADS_TRAMPOLINE(10)
U_NOADS_TRAMPOLINE(11)
U_NOADS_TRAMPOLINE(12)
U_NOADS_TRAMPOLINE(13)
U_NOADS_TRAMPOLINE(14)
U_NOADS_TRAMPOLINE(15)
#undef U_NOADS_TRAMPOLINE

using Trampoline = bool (*)(void *, void *, void *, void *, void *);
constexpr Trampoline TRAMPOLINES[U_NOADS_TRAMP_N] = {
    fake_show_0, fake_show_1, fake_show_2, fake_show_3, fake_show_4, fake_show_5,
    fake_show_6, fake_show_7, fake_show_8, fake_show_9, fake_show_10, fake_show_11,
    fake_show_12, fake_show_13, fake_show_14, fake_show_15,
};

void try_show(const char *label, const UNoAdsShow &s, std::size_t index) {
    char ns[128], nm[128];
    uno_split(s.klass, ns, sizeof(ns), nm, sizeof(nm));
    void *klass = g_il.find_class(ns, nm);
    if (!klass) {
        log_line("%s: classe %s nao encontrada", label, s.klass);
        return;
    }
    void *method = g_il.class_get_method_from_name(klass, s.method, s.argc);
    if (!method) {
        log_line("%s: %s sem %s(%d args) — versão do plugin diferente?", label, s.klass, s.method,
                 s.argc);
        return;
    }
    void *code = *(void **)method;  // MethodInfo::methodPointer é o 1º campo
    if (!code || !uno_method_fits((const uint32_t *)code)) {
        log_line("%s: %s.%s curto demais pro hook, pulando", label, s.klass, s.method);
        return;
    }
    Hook hook{klass, method, label, s.method, s.argc, s.closer, nullptr, false};
    if (DobbyHook(code, reinterpret_cast<void *>(TRAMPOLINES[index]),
                  reinterpret_cast<void **>(&hook.original)) != 0) {
        log_line("%s: DobbyHook falhou em %s.%s", label, s.klass, s.method);
        return;
    }
    g_hooks.push_back(hook);
    log_line("%s: hook armado em %s.%s", label, s.klass, s.method);
}

void *worker(void *) {
    if (!il2cpp_boot(g_il)) {
        log_line("il2cpp_boot falhou; nenhum SDK hookado");
        return nullptr;
    }
    log_line("boot ok; pacote=%s; rewarded fora do escopo", pkg());
    // Sem realocação depois do 1º hook: reserve antes (fake lê por índice
    // enquanto o worker ainda instala — realloc moveria o vetor).
    g_hooks.reserve(U_NOADS_TRAMP_N);
    std::size_t index = 0;
    for (std::size_t i = 0; i < U_NOADS_TARGET_COUNT; i++) {
        const UNoAdsTarget &t = U_NOADS_TARGETS[i];
        for (int j = 0; j < t.show_count; j++) {
            if (index >= U_NOADS_TRAMP_N) {
                log_line("%s: teto de trampolins; %s nao hookado", t.label, t.shows[j].klass);
                continue;
            }
            try_show(t.label, t.shows[j], index);
            if (g_hooks.size() > index) index++;
        }
    }
    for (std::size_t i = 0; i < g_hooks.size(); i++) g_hooks[i].active = true;
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
