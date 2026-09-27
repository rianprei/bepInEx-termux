// u_noads — supressão forçada de interstitial/app-open em runtime (F11b).
// rewarded fica de fora por escopo. Pra cada Show suprimido, dispara o
// MESMO caminho que o SDK usa ao fechar (delegates/eventos atuais lidos na
// hora — nunca método inventado; ver ADAPTERS.md por SDK: alvo, assinatura,
// fonte oficial, como fecha). Se o fechamento falhar, REPASSA pro original
// (o anúncio aparece; o jogo nunca trava). Sem PC, sem patch de APK.
#include <dobby.h>
#include <pthread.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../../common/il2cpp_min.h"
#include "../../common/mod_common.h"
#include "u_noads_closers.h"  // closers por SDK (host-testável)
#include "u_noads_pure.h"
#include "u_noads_targets.h"

// Os closers vivem em u_noads_closers.h (fora de namespace{} anônimo: a tabela
// em u_noads_targets.h guarda o endereço deles, então declaração e definição têm
// que ser o mesmo símbolo) e num header separado para o teste host exercitá-los
// com stubs de il2cpp.
namespace {

constexpr const char *TAG = "u_noads";

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
    // atomic: o worker escreve ao terminar de instalar E o auto-cura no
    // dispatch escreve da thread do jogo (revisão: "escritor único" era falso).
    // relaxed basta: é um flag de direção segura (false = repassa).
    std::atomic<bool> active;
};

Il2Cpp g_il;
std::vector<Hook *> g_hooks;  // atomic no Hook => não copia

// Anexa a thread atual ao runtime (idempotente em thread já anexada).
// Todo runtime_invoke vindo de thread do jogo passa daqui: sem attach,
// il2cpp crasha (achado da revisão).
void attach() { g_il.thread_attach(g_il.domain); }

// (helpers de closer vivem no escopo global, acima do namespace {})

bool dispatch(std::size_t i, void *self, void *a, void *b, void *c, void *d) {
    if (i >= g_hooks.size()) return false;
    Hook *h = g_hooks[i];
    if (!h->active.load(std::memory_order_relaxed)) return h->original(self, a, b, c, d);
    attach();
    UNoAdsFireCtx ctx{&g_il, h->klass, self, {a, b, c, d}, h->show_argc};
    // closer null (Meta): Show retorna bool — false = caminho "sem fill".
    bool closed = h->closer ? h->closer(ctx) : true;
    if (!closed) {
        h->active.store(false);  // auto-cura: cumpre o que o log promete
        // A nota do closer entra no fim. Montada em buffer: std::string em
        // varargs é UB (e o -Werror do gate acusa).
        char why[256] = {0};
        if (ctx.note) snprintf(why, sizeof(why), " (%s)", ctx.note);
        mod_log(TAG, "%s: fechamento falhou%s; hook recusado, anúncio volta a aparecer", h->label, why);
        return h->original(self, a, b, c, d);
    }
    static bool logged = false;
    if (!logged) {
        logged = true;
        mod_log(TAG, "intersticial/app-open suprimido com fechamento do próprio SDK");
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
        mod_log(TAG, "%s: classe %s nao encontrada", label, s.klass);
        return;
    }
    void *method = g_il.class_get_method_from_name(klass, s.method, s.argc);
    if (!method) {
        mod_log(TAG, "%s: %s sem %s(%d args) — versão do plugin diferente?", label, s.klass, s.method,
                 s.argc);
        return;
    }
    void *code = *(void **)method;  // MethodInfo::methodPointer é o 1º campo
    if (!code || !uno_method_fits((const uint32_t *)code)) {
        mod_log(TAG, "%s: %s.%s curto demais pro hook, pulando", label, s.klass, s.method);
        return;
    }
    Hook *hook = new Hook{klass, method, label, s.method, s.argc, s.closer, nullptr, {false}};
    if (DobbyHook(code, reinterpret_cast<void *>(TRAMPOLINES[index]),
                  reinterpret_cast<void **>(&hook->original)) != 0) {
        mod_log(TAG, "%s: DobbyHook falhou em %s.%s", label, s.klass, s.method);
        delete hook;
        return;
    }
    g_hooks.push_back(hook);
    mod_log(TAG, "%s: hook armado em %s.%s", label, s.klass, s.method);
}

void *worker(void *) {
    if (!il2cpp_boot(g_il)) {
        mod_log(TAG, "boot IL2CPP falhou; nenhum SDK hookado (motivo detalhado no log IL2CPP)");
        return nullptr;
    }
    mod_log(TAG, "boot ok; pacote=%s; rewarded fora do escopo", mod_pkg() ? mod_pkg() : "unknown");
    // Sem realocação depois do 1º hook: reserve antes (fake lê por índice
    // enquanto o worker ainda instala — realloc moveria o vetor).
    g_hooks.reserve(U_NOADS_TRAMP_N);
    std::size_t index = 0;
    for (std::size_t i = 0; i < U_NOADS_TARGET_COUNT; i++) {
        const UNoAdsTarget &t = U_NOADS_TARGETS[i];
        for (int j = 0; j < t.show_count; j++) {
            if (index >= U_NOADS_TRAMP_N) {
                mod_log(TAG, "%s: teto de trampolins; %s nao hookado", t.label, t.shows[j].klass);
                continue;
            }
            try_show(t.label, t.shows[j], index);
            if (g_hooks.size() > index) index++;
        }
    }
    for (std::size_t i = 0; i < g_hooks.size(); i++) g_hooks[i]->active.store(true);
    return nullptr;
}

} // namespace

__attribute__((constructor)) static void u_noads_init() {
    // Sem pacote resolvido não há pasta de mods nem log C1: não hooka nada
    // (mesma decisão do u_patch/u_frida). logcat sempre.
    const char *env = std::getenv("BEPINEX_PKG");
    if (!env || !*env) {
        __android_log_print(ANDROID_LOG_INFO, TAG, "sem BEPINEX_PKG — inerte");
        return;
    }
    pthread_t thread;
    if (pthread_create(&thread, nullptr, worker, nullptr) == 0) {
        pthread_detach(thread);
    } else {
        mod_log(TAG, "pthread_create falhou; mod desativado");
    }
}
