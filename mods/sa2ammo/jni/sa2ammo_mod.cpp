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
#include <pthread.h>
#include "dobby.h"
#include "../../common/il2cpp_min.h"

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "sa2ammo", __VA_ARGS__)

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

static void *worker(void *) {
    LOG("carregado, esperando libil2cpp.so");
    Il2Cpp il;
    if (!il2cpp_boot(il)) { LOG("il2cpp não subiu em 120s — desistindo"); return nullptr; }

    void *weapon = nullptr, *creature = nullptr;
    for (int i = 0; i < 600 && !(weapon && creature); i++) {  // até 120s pras classes
        weapon = il.find_class("", "WeaponInfo");
        creature = il.find_class("", "ComplexCreature");
        if (!(weapon && creature)) usleep(200 * 1000);
    }
    if (!weapon || !creature) { LOG("WeaponInfo/ComplexCreature não achadas — desistindo"); return nullptr; }

    void *f_unl = il.class_get_field_from_name(weapon, "unlimitedAmmo");
    void *f_eq = il.class_get_field_from_name(weapon, "canBeEquipped");  // herdado de GenericShopItem
    void *f_type = il.class_get_field_from_name(weapon, "type");
    void *f_cur = il.class_get_field_from_name(weapon, "currentAmmo");  // InventoryItem, herdado de GenericShopItem
    void *f_sel = il.class_get_field_from_name(creature, "selectedWeapon");
    void *inv_item = il.find_class("", "InventoryItem");
    void *f_amt = inv_item ? il.class_get_field_from_name(inv_item, "Amount") : nullptr;
    void *t_sel = il2cpp_method_ptr(il, creature, "SelectWeapon", 1);
    void *t_rel = il2cpp_method_ptr(il, creature, "ReloadWeaponClip", 1);
    if (!f_unl || !f_eq || !f_type || !f_cur || !f_sel || !f_amt || !t_sel || !t_rel) {
        LOG("campo/método ausente (unl=%p eq=%p type=%p cur=%p sel=%p amt=%p SelectWeapon=%p Reload=%p) — versão nova do jogo?",
            f_unl, f_eq, f_type, f_cur, f_sel, f_amt, t_sel, t_rel);
        return nullptr;
    }
    off_unlimited = il.field_get_offset(f_unl);
    off_equip = il.field_get_offset(f_eq);
    off_type = il.field_get_offset(f_type);
    off_cur_ammo = il.field_get_offset(f_cur);
    off_amount = il.field_get_offset(f_amt);
    off_selected = il.field_get_offset(f_sel);
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
