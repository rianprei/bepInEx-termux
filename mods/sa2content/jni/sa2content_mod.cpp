// sa2content — Swamp Attack 2 (com.hyperdotstudios.swampattack2 1.3.9):
// conteúdo novo usando o sistema de balanceamento do próprio jogo, só em
// memória (nada de arquivo do jogo):
//   - armas primárias cortadas (nenhum personagem usava) liberadas, uma por
//     personagem, no nível 1 dele;
//   - fusões: arma ganha o efeito de outra (gelo, veneno, choque, radiação);
//   - fases L05/L10/L15 dos capítulos 2+ ganham o chefe do capítulo anterior
//     no fim da onda final;
//   - sem anúncio forçado entre fases (os opcionais com recompensa continuam).
//
// Como aplica: GameBalancer.AvailableCategories -> CreateSnapshot() da
// categoria -> troca o dado dos itens da tabela patches.h -> Apply() da
// categoria. É o mesmo parser/aplicação que o jogo usa pros patches remotos.
// Não usa OnReceivedBalanceFromGrid porque ele grava o patch num arquivo
// local do jogo (SaveLocalPatch). Reaplica depois de TryApplyPendingPatches,
// senão um patch remoto do jogo sobrescreveria as mesmas chaves.
//
// patches.h é gerado por tools/gen_patches.py a partir do snapshot lido em
// runtime.
#include <android/log.h>
#include <pthread.h>
#include "dobby.h"
#include "../../common/il2cpp_min.h"
#include "patches.h"

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "sa2content", __VA_ARGS__)

static const char *const CATEGORIES[] = {"wep", "red", "ld"};

static Il2Cpp il;
static void *f_available, *k_stringbuilder;
static size_t off_cat_items, off_item_key, off_item_data;
static pthread_mutex_t apply_lock = PTHREAD_MUTEX_INITIALIZER;

// Aplica os patches de uma categoria. Retorna quantos itens foram trocados
// (-1 = categoria sem snapshot).
static int apply_category(void *info, const char *cat, void *sb) {
    bool ok = false;
    void *snap = il.call(info, "CreateSnapshot", nullptr, 0, &ok);
    if (!ok || !snap) return -1;
    uint8_t *list = *(uint8_t **)((uint8_t *)snap + off_cat_items);
    if (!list) return -1;
    uint8_t *items = *(uint8_t **)(list + 0x10);  // List<T>._items
    int32_t size = *(int32_t *)(list + 0x18);     // List<T>._size
    int changed = 0;
    for (int32_t j = 0; j < size; j++) {
        uint8_t *it = ((uint8_t **)(items + 0x20))[j];
        void *key = *(void **)(it + off_item_key);
        for (const sa2_patch &p : SA2_PATCHES) {
            if (strcmp(p.cat, cat) != 0 || !il2cpp_str_eq(key, p.key)) continue;
            il.gc_wbarrier_set_field(it, (void **)(it + off_item_data), il.string_new(p.data));
            changed++;
            break;
        }
    }
    void *args[2] = {snap, sb};
    void *boxed = il.call(info, "Apply", args, 2, &ok);
    LOG("%s: %d item(ns) trocados, Apply %s (retorno %d)", cat, changed, ok ? "ok" : "LANÇOU EXCEÇÃO",
        ok && boxed ? *(int32_t *)((uint8_t *)boxed + 0x10) : -1);
    return changed;
}

// true = todas as categorias alvo tinham snapshot (dados já carregados).
static bool apply_all() {
    pthread_mutex_lock(&apply_lock);
    void *arr = nullptr;
    il.field_static_get_value(f_available, &arr);
    bool all = arr != nullptr;
    if (arr) {
        void *sb = il.object_new(k_stringbuilder);
        il.call(sb, ".ctor", nullptr, 0);
        size_t len = *(size_t *)((uint8_t *)arr + 0x18);
        int found = 0;
        for (size_t i = 0; i < len; i++) {
            void *info = ((void **)((uint8_t *)arr + 0x20))[i];
            void *type = il.call(info, "get_type", nullptr, 0);
            for (const char *cat : CATEGORIES) {
                if (!il2cpp_str_eq(type, cat)) continue;
                if (apply_category(info, cat, sb) >= 0) found++;
            }
        }
        all = found == (int)(sizeof(CATEGORIES) / sizeof(CATEGORIES[0]));
    }
    pthread_mutex_unlock(&apply_lock);
    return all;
}

static void (*orig_try_apply)(void *, void *);
static void fake_try_apply(void *self, void *method) {
    orig_try_apply(self, method);
    apply_all();
}

static void (*orig_try_show_interstitial)(void *, void *);
static void fake_try_show_interstitial(void *, void *) {
    static bool logged;
    if (!logged) { logged = true; LOG("anúncio forçado entre fases bloqueado"); }
}

static void *worker(void *) {
    if (!il2cpp_boot(il)) { LOG("il2cpp não subiu — desistindo"); return nullptr; }
    void *gb = nullptr, *cat = nullptr, *item = nullptr, *ads = nullptr;
    for (int i = 0; i < 600 && !(gb && cat && item && ads); i++) {
        gb = il.find_class("", "GameBalancer");
        cat = il.find_class("", "GameBalanceCategory");
        item = il.find_class("", "GameBalanceItem");
        ads = il.find_class("", "InterstitialAdManager");
        if (!(gb && cat && item && ads)) usleep(200 * 1000);
    }
    k_stringbuilder = il.find_class("System.Text", "StringBuilder");
    f_available = gb ? il.class_get_field_from_name(gb, "AvailableCategories") : nullptr;
    void *f_items = cat ? il.class_get_field_from_name(cat, "items") : nullptr;
    void *f_key = item ? il.class_get_field_from_name(item, "key") : nullptr;
    void *f_data = item ? il.class_get_field_from_name(item, "data") : nullptr;
    void *t_try = gb ? il2cpp_method_ptr(il, gb, "TryApplyPendingPatches", 0) : nullptr;
    void *t_ads = ads ? il2cpp_method_ptr(il, ads, "TryShowInterstitial", 0) : nullptr;
    if (!k_stringbuilder || !f_available || !f_items || !f_key || !f_data || !t_try || !t_ads) {
        LOG("classe/campo/método ausente (gb=%p cat=%p item=%p ads=%p sb=%p) — versão nova do jogo?",
            gb, cat, item, ads, k_stringbuilder);
        return nullptr;
    }
    off_cat_items = il.field_get_offset(f_items);
    off_item_key = il.field_get_offset(f_key);
    off_item_data = il.field_get_offset(f_data);

    if (DobbyHook(t_try, (void *)fake_try_apply, (void **)&orig_try_apply) != 0 ||
        DobbyHook(t_ads, (void *)fake_try_show_interstitial, (void **)&orig_try_show_interstitial) != 0) {
        LOG("DobbyHook falhou (TryApplyPendingPatches @%p / TryShowInterstitial @%p)", t_try, t_ads);
        return nullptr;
    }
    LOG("ativo: %zu patches embutidos; hooks TryApplyPendingPatches + TryShowInterstitial",
        sizeof(SA2_PATCHES) / sizeof(SA2_PATCHES[0]));

    // Primeira aplicação assim que os dados existirem (até ~60s).
    for (int i = 0; i < 120 && !apply_all(); i++) usleep(500 * 1000);
    return nullptr;
}

__attribute__((constructor)) static void sa2content_init() {
    pthread_t t;
    if (pthread_create(&t, nullptr, worker, nullptr) == 0) pthread_detach(t);
}
