// sa2content — Swamp Attack 2 (com.hyperdotstudios.swampattack2 1.3.9):
// conteúdo novo usando o sistema de balanceamento do próprio jogo, só em
// memória (nada de arquivo do jogo):
//   - armas primárias cortadas (nenhum personagem usava) liberadas, uma por
//     personagem, no nível 1 dele;
//   - fusões: arma ganha o efeito de outra (gelo, veneno, choque, radiação);
//   - fases L05/L10/L15 dos capítulos 2+ ganham o chefe do capítulo anterior
//     no fim da onda final;
//   - sem anúncio forçado entre fases (os opcionais com recompensa continuam);
//   - Unknown (card "?" sem corpo) jogável com o corpo do Slow Joe e uma
//     Shotgun clonada com todos os efeitos de dano.
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

// Unknown jogável: DESLIGADO. Com o corpo do Slow Joe o jogo crasha ao abrir
// o mapa (SIGSEGV em MapLevelIcon.TrySetupRewardIcon, fault 0x60004523),
// mesmo com availableFromLevel vazio. Ver README "Unknown".
#ifndef SA2_ENABLE_UNKNOWN
#define SA2_ENABLE_UNKNOWN 0
#endif

static const char *const CATEGORIES[] = {"wep", "red", "ld"};

static Il2Cpp il;
static void *f_available, *k_stringbuilder, *k_unity_object;
static size_t off_cat_items, off_item_key, off_item_data;
static pthread_mutex_t apply_lock = PTHREAD_MUTEX_INITIALIZER;

// Aplica os patches de uma categoria. Retorna quantos itens foram trocados
// (-1 = categoria sem snapshot).
static int apply_category(void *info, const char *cat, void *sb, bool with_unknown) {
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
            // Unknown só ganha dados junto com o corpo (thread principal), senão
            // fica jogável sem prefab.
            if (!with_unknown && strcmp(p.key, SA2_UNKNOWN_KEY) == 0) break;
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
static bool apply_all(bool with_unknown) {
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
                if (apply_category(info, cat, sb, with_unknown) >= 0) found++;
            }
        }
        all = found == (int)(sizeof(CATEGORIES) / sizeof(CATEGORIES[0]));
    }
    pthread_mutex_unlock(&apply_lock);
    return all;
}

#if SA2_ENABLE_UNKNOWN
static void *category(const char *type) {
    void *arr = nullptr;
    il.field_static_get_value(f_available, &arr);
    if (!arr) return nullptr;
    size_t len = *(size_t *)((uint8_t *)arr + 0x18);
    for (size_t i = 0; i < len; i++) {
        void *info = ((void **)((uint8_t *)arr + 0x20))[i];
        if (il2cpp_str_eq(il.call(info, "get_type", nullptr, 0), type)) return info;
    }
    return nullptr;
}

static void *get_item(void *cat, const char *key) {
    void *args[1] = {il.string_new(key)};
    bool ok = false;
    void *r = il.call(cat, "GetItem", args, 1, &ok);
    return ok ? r : nullptr;
}

// Parser do próprio jogo (GameBalancePatch_*.Deserialize), retorno bool boxed.
static bool deserialize(void *cat, void *item, const char *json) {
    void *args[2] = {item, il.string_new(json)};
    bool ok = false;
    void *boxed = il.call(cat, "Deserialize", args, 2, &ok);
    return ok && boxed && *((uint8_t *)boxed + 0x10);
}

// Campos do RedneckInfo que vêm do corpo emprestado. Nome, ícone "?" e save
// (campo data, RedneckData pelo nome) continuam do Unknown.
static const char *const BODY_FIELDS[] = {
    "characterGamePrefab", "characterUpgradePrefab", "presentationScreenImage", "type", "frameRarity",
    "skins", "skinCameraSettings", "pictureWeapon", "tutorialIcon", "upgradeGroups"};
static void *super_clone;

// Precisa da thread principal (clone de objeto Unity), por isso só roda no
// hook de TryApplyPendingPatches, logo depois do apply_all(true) que já deu
// ao Unknown vida/armas/isPlayable pelo Apply do jogo. Idempotente: roda de
// novo a cada chamada porque o Apply re-serializa o Unknown com a Shotgun
// original (o clone tem o mesmo persistentGuid). Deserialize direto no
// Unknown crashava o jogo (SIGSEGV dentro do parser), por isso o JSON dele
// vai pelo Apply.
static void setup_unknown() {
    static bool logged_ok;
    void *red = category("red"), *wep = category("wep");
    void *un = red ? get_item(red, SA2_UNKNOWN_KEY) : nullptr;
    void *body = red ? get_item(red, SA2_UNKNOWN_BODY) : nullptr;
    void *base = wep ? get_item(wep, SA2_SUPER_BASE_GUID) : nullptr;
    if (!un || !body || !base) {
        LOG("Unknown: item ausente (unknown=%p corpo=%p shotgun=%p)", un, body, base);
        return;
    }
    if (!super_clone) {
        void *args[1] = {base};
        bool ok = false;
        void *c = il.call_static(k_unity_object, "Internal_CloneSingle", args, 1, &ok);
        if (!ok || !c) { LOG("Unknown: clone da Shotgun falhou"); return; }
        il.gchandle_new(c, false);  // só o array de armas do Unknown aponta pro clone
        void *name[1] = {il.string_new("Shotgun")};  // mesmo termo de tradução da original
        il.call(c, "set_name", name, 1);
        if (!deserialize(wep, c, SA2_SUPER_WEP)) { LOG("Unknown: dados da super arma recusados"); return; }
        super_clone = c;
    }
    void *k = il.object_get_class(un);
    for (const char *name : BODY_FIELDS) {
        void *f = il.class_get_field_from_name(k, name);
        if (!f) { LOG("Unknown: campo %s ausente", name); return; }
        il.copy_field(body, un, f);
    }
    void *f_weapons = il.class_get_field_from_name(k, "weapons");
    uint8_t *arr = f_weapons ? *(uint8_t **)((uint8_t *)un + il.field_get_offset(f_weapons)) : nullptr;
    if (!arr || *(size_t *)(arr + 0x18) == 0) { LOG("Unknown: sem lista de armas"); return; }
    // weapons[0] = RedneckWeapon {int unlockedOnLevel; int dropWeaponWeight; ref weapon}, 16 bytes.
    il.gc_wbarrier_set_field(arr, (void **)(arr + 0x20 + 8), super_clone);
    if (!logged_ok) { logged_ok = true; LOG("Unknown: jogável (corpo %s) com a super Shotgun", SA2_UNKNOWN_BODY); }
}

#endif

static void (*orig_try_apply)(void *, void *);
static void fake_try_apply(void *self, void *method) {
    orig_try_apply(self, method);
    apply_all(SA2_ENABLE_UNKNOWN);
#if SA2_ENABLE_UNKNOWN
    pthread_mutex_lock(&apply_lock);
    setup_unknown();
    pthread_mutex_unlock(&apply_lock);
#endif
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
    k_unity_object = il.find_class("UnityEngine", "Object");
    f_available = gb ? il.class_get_field_from_name(gb, "AvailableCategories") : nullptr;
    void *f_items = cat ? il.class_get_field_from_name(cat, "items") : nullptr;
    void *f_key = item ? il.class_get_field_from_name(item, "key") : nullptr;
    void *f_data = item ? il.class_get_field_from_name(item, "data") : nullptr;
    void *t_try = gb ? il2cpp_method_ptr(il, gb, "TryApplyPendingPatches", 0) : nullptr;
    void *t_ads = ads ? il2cpp_method_ptr(il, ads, "TryShowInterstitial", 0) : nullptr;
    if (!k_stringbuilder || !k_unity_object || !f_available || !f_items || !f_key || !f_data || !t_try || !t_ads) {
        LOG("classe/campo/método ausente (gb=%p cat=%p item=%p ads=%p sb=%p uobj=%p) — versão nova do jogo?",
            gb, cat, item, ads, k_stringbuilder, k_unity_object);
        return nullptr;
    }
    off_cat_items = il.field_get_offset(f_items);
    off_item_key = il.field_get_offset(f_key);
    off_item_data = il.field_get_offset(f_data);

    // Hooks independentes: sem o de reaplicação o conteúdo ainda entra no
    // boot (só pode ser sobrescrito por patch remoto); sem o de anúncio o
    // conteúdo segue igual.
    if (DobbyHook(t_try, (void *)fake_try_apply, (void **)&orig_try_apply) != 0)
        LOG("DobbyHook falhou em TryApplyPendingPatches @%p — sem reaplicação após patch remoto", t_try);
    if (DobbyHook(t_ads, (void *)fake_try_show_interstitial, (void **)&orig_try_show_interstitial) != 0)
        LOG("DobbyHook falhou em TryShowInterstitial @%p — anúncio forçado continua", t_ads);
    LOG("ativo: %zu patches embutidos", sizeof(SA2_PATCHES) / sizeof(SA2_PATCHES[0]));

    // Primeira aplicação assim que os dados existirem (até ~60s).
    for (int i = 0; i < 120 && !apply_all(false); i++) usleep(500 * 1000);
    return nullptr;
}

__attribute__((constructor)) static void sa2content_init() {
    pthread_t t;
    if (pthread_create(&t, nullptr, worker, nullptr) == 0) pthread_detach(t);
}
