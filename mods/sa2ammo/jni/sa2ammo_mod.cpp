// sa2ammo — Swamp Attack 2 (com.hyperdotstudios.swampattack2 1.3.9):
// munição ilimitada nas armas primárias do jogador.
//
// Con mais citado pela comunidade (reviews Play/App Store): rifle fica sem
// munição no meio da fase e o jogo cobra gema pra comprar mais.
//
// O jogo já tem a flag pronta: WeaponInfo.unlimitedAmmo. ComplexCreature.
// ReloadWeaponClip lê essa flag e, se ligada, trata a reserva como 1.000.000.
// O mod só liga a flag nas armas primárias equipáveis quando o jogador as
// seleciona (ComplexCreature.SelectWeapon) ou recarrega (ReloadWeaponClip).
// Nada de offset fixo: classe, método e campos saem da API il2cpp exportada
// pela libil2cpp.so.
//
// Carregado pelo caminho genérico do loader (/data/local/tmp/mods/<pkg>/).
#include <android/log.h>
#include <dlfcn.h>
#include <pthread.h>
#include <unistd.h>
#include <cstdint>
#include <cstring>
#include "dobby.h"

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "sa2ammo", __VA_ARGS__)

typedef void *(*domain_get_t)();
typedef void *(*thread_attach_t)(void *);
typedef void **(*domain_get_assemblies_t)(void *, size_t *);
typedef void *(*assembly_get_image_t)(void *);
typedef void *(*class_from_name_t)(void *, const char *, const char *);
typedef void *(*method_from_name_t)(void *, const char *, int);
typedef void *(*field_from_name_t)(void *, const char *);
typedef size_t (*field_offset_t)(void *);

#define WEAPON_TYPE_PRIMARY 0  // WeaponType 0: armas de fogo (Shotgun, Kalashnikov...)

static size_t off_unlimited, off_equip, off_type, off_cur_ammo, off_amount, off_selected;
static unsigned n_patched;
static void (*orig_select)(void *, void *, void *);
static void (*orig_reload)(void *, bool, void *);

// Liga a flag só em arma primária equipável (arma do jogador). Com a flag:
// Shoot() pula o "currentAmmo.Amount -= 1" e ReloadWeaponClip trata a
// reserva como 1.000.000. HasAmmo() olha só currentAmmo.Amount, então
// inventário já zerado sobe pra 1 pra arma não ficar travada.
static void patch_weapon(void *w) {
    uint8_t *p = (uint8_t *)w;
    if (!p || !p[off_equip] || *(int32_t *)(p + off_type) != WEAPON_TYPE_PRIMARY) return;
    uint8_t *inv = *(uint8_t **)(p + off_cur_ammo);
    if (inv && *(int32_t *)(inv + off_amount) <= 0) *(int32_t *)(inv + off_amount) = 1;
    if (p[off_unlimited]) return;
    p[off_unlimited] = 1;
    LOG("arma primária com munição ilimitada (#%u)", ++n_patched);
}

static void fake_select(void *self, void *w, void *method) {
    patch_weapon(w);
    orig_select(self, w, method);
}

// Algumas trocas de arma escrevem selectedWeapon direto (inline, sem
// SelectWeapon), por isso a recarga também garante a flag.
static void fake_reload(void *self, bool reload_ammo, void *method) {
    if (self) patch_weapon(*(void **)((uint8_t *)self + off_selected));
    orig_reload(self, reload_ammo, method);
}

// Procura a classe em todas as imagens (WeaponInfo e ComplexCreature ficam no
// Assembly-CSharp, namespace global).
static void *find_class(void *domain, domain_get_assemblies_t get_asm, assembly_get_image_t get_img,
                        class_from_name_t from_name, const char *name) {
    size_t n = 0;
    void **asms = get_asm(domain, &n);
    for (size_t i = 0; i < n; i++) {
        void *k = from_name(get_img(asms[i]), "", name);
        if (k) return k;
    }
    return nullptr;
}

static void *worker(void *) {
    void *h = nullptr;
    for (int i = 0; i < 600 && !h; i++) {  // até 120s pra libil2cpp carregar
        h = dlopen("libil2cpp.so", RTLD_NOW | RTLD_NOLOAD);
        if (!h) usleep(200 * 1000);
    }
    if (!h) { LOG("libil2cpp.so não carregou em 120s — desistindo"); return nullptr; }

    auto domain_get = (domain_get_t)dlsym(h, "il2cpp_domain_get");
    auto thread_attach = (thread_attach_t)dlsym(h, "il2cpp_thread_attach");
    auto get_asm = (domain_get_assemblies_t)dlsym(h, "il2cpp_domain_get_assemblies");
    auto get_img = (assembly_get_image_t)dlsym(h, "il2cpp_assembly_get_image");
    auto from_name = (class_from_name_t)dlsym(h, "il2cpp_class_from_name");
    auto method_from_name = (method_from_name_t)dlsym(h, "il2cpp_class_get_method_from_name");
    auto field_from_name = (field_from_name_t)dlsym(h, "il2cpp_class_get_field_from_name");
    auto field_offset = (field_offset_t)dlsym(h, "il2cpp_field_get_offset");
    if (!domain_get || !thread_attach || !get_asm || !get_img || !from_name || !method_from_name ||
        !field_from_name || !field_offset) {
        LOG("API il2cpp incompleta — desistindo");
        return nullptr;
    }

    void *weapon = nullptr, *creature = nullptr;
    for (int i = 0; i < 600 && !(weapon && creature); i++) {  // até 120s pro domínio subir
        void *domain = domain_get();
        if (domain) {
            thread_attach(domain);
            weapon = find_class(domain, get_asm, get_img, from_name, "WeaponInfo");
            creature = find_class(domain, get_asm, get_img, from_name, "ComplexCreature");
        }
        if (!(weapon && creature)) usleep(200 * 1000);
    }
    if (!weapon || !creature) { LOG("WeaponInfo/ComplexCreature não achadas — desistindo"); return nullptr; }

    void *f_unl = field_from_name(weapon, "unlimitedAmmo");
    void *f_eq = field_from_name(weapon, "canBeEquipped");  // herdado de GenericShopItem
    void *f_type = field_from_name(weapon, "type");
    void *f_cur = field_from_name(weapon, "currentAmmo");  // InventoryItem, herdado de GenericShopItem
    void *f_sel = field_from_name(creature, "selectedWeapon");
    void *inv_item = find_class(domain_get(), get_asm, get_img, from_name, "InventoryItem");
    void *f_amt = inv_item ? field_from_name(inv_item, "Amount") : nullptr;
    void *m_sel = method_from_name(creature, "SelectWeapon", 1);
    void *m_rel = method_from_name(creature, "ReloadWeaponClip", 1);
    if (!f_unl || !f_eq || !f_type || !f_cur || !f_sel || !f_amt || !m_sel || !m_rel) {
        LOG("campo/método ausente (unl=%p eq=%p type=%p cur=%p sel=%p amt=%p SelectWeapon=%p Reload=%p) — versão nova do jogo?",
            f_unl, f_eq, f_type, f_cur, f_sel, f_amt, m_sel, m_rel);
        return nullptr;
    }
    off_unlimited = field_offset(f_unl);
    off_equip = field_offset(f_eq);
    off_type = field_offset(f_type);
    off_cur_ammo = field_offset(f_cur);
    off_amount = field_offset(f_amt);
    off_selected = field_offset(f_sel);
    void *t_sel = *(void **)m_sel;  // MethodInfo::methodPointer é o 1º campo
    void *t_rel = *(void **)m_rel;
    if (DobbyHook(t_sel, (void *)fake_select, (void **)&orig_select) != 0 ||
        DobbyHook(t_rel, (void *)fake_reload, (void **)&orig_reload) != 0) {
        LOG("DobbyHook falhou (SelectWeapon @%p / ReloadWeaponClip @%p)", t_sel, t_rel);
        return nullptr;
    }
    LOG("ativo: SelectWeapon @%p + ReloadWeaponClip @%p (unlimitedAmmo+0x%zx canBeEquipped+0x%zx type+0x%zx currentAmmo+0x%zx selectedWeapon+0x%zx)",
        t_sel, t_rel, off_unlimited, off_equip, off_type, off_cur_ammo, off_selected);
    return nullptr;
}

__attribute__((constructor)) static void sa2ammo_init() {
    pthread_t t;
    if (pthread_create(&t, nullptr, worker, nullptr) == 0) pthread_detach(t);
}
