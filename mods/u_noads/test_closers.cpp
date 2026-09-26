// u_noads: teste host dos closers com stubs de il2cpp.
//
// Antes os 6 closers eram stubados com "return false" e o CI só contava a
// tabela — o bug do enum (passar o int cru como ponteiro do enum pro
// runtime_invoke) era invisível. Aqui cada closer roda de verdade contra um
// Il2Cpp falso que REGISTRA as chamadas, então:
//   - o enum tem que chegar no Invoke como caixa (object_new + value__), nunca
//     como o número cru (era o SIGSEGV);
//   - "sem callback => não suprime" é regra testada (revisão);
//   - a aridade (nargs) é conferida: o lookup do il.call usa nargs, então um
//     aridade errada faz o método não ser encontrado e o repasse acontecer.
//
// Os stubs imitam duas coisas do runtime que o bug dependia:
//   - field_static_get_value copia os BYTES do valor do campo (8 de uma
//     referência, 4 do int de um enum) — não devolve objeto nenhum;
//   - runtime_invoke recebe ponteiro pros valores: o invoker desreferencia o
//     ponteiro do enum, então um int cru como ponteiro é_address inválida.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <tuple>
#include <vector>

#include "u_noads_closers.h"
#include "u_noads_pure.h"
#include "u_noads_targets.h"

namespace {

struct Obj { int tag; };
Obj g_objs[256];
int g_obj_n = 0;
void *new_obj(int tag) {
    g_objs[g_obj_n].tag = tag;
    return &g_objs[g_obj_n++];
}

struct Rec {
    int nargs = -1;        // aridade pedida no lookup de método
    bool invoked = false;  // runtime_invoke chegou a ser chamado
    int object_new = 0;
    int field_set_value = 0;
    std::vector<void *> args;
};
Rec g_rec;

using Key = std::pair<std::string, std::string>;
std::map<void *, std::string> g_class_name;          // classe -> nome
std::map<std::string, void *> g_by_name;             // nome -> classe
std::map<Key, void *> g_fields;                      // (classe, campo) -> FieldInfo
std::map<Key, uint64_t> g_sbytes;                    // (classe, campo) -> bytes do valor
std::map<Key, void *> g_ivalues;                     // (classe, campo) -> referência
std::map<std::string, std::map<std::string, void *>> g_nested;  // externa -> simples -> classe
std::map<std::tuple<std::string, int>, void *> g_methods;       // (nome, nargs) -> MethodInfo
std::map<void *, int> g_method_arity;   // MethodInfo -> aridade (o runtime_invoke sabe)

void reset() {
    g_obj_n = 0;
    g_rec = Rec{};
    g_class_name.clear();
    g_by_name.clear();
    g_fields.clear();
    g_sbytes.clear();
    g_ivalues.clear();
    g_nested.clear();
    g_methods.clear();
    g_method_arity.clear();
}

void *add_class(const std::string &fqn) {
    void *k = new_obj(1);
    g_class_name[k] = fqn;
    g_by_name[fqn] = k;
    return k;
}
void add_field(const std::string &cls, const char *name) { g_fields[{cls, name}] = new_obj(2); }
// valor de campo de INSTÂNCIA (referência guardada em algum lugar do objeto)
void add_ivalue(const std::string &cls, const char *name, void *v) {
    g_ivalues[{cls, name}] = v;
    g_fields[{cls, name}] = new_obj(2);
}
// valor de campo ESTÁTICO: os bytes crus, como o runtime copia
void add_svalue_ref(const std::string &cls, const char *name, void *v) {
    g_sbytes[{cls, name}] = (uint64_t)(uintptr_t)v;
    g_fields[{cls, name}] = new_obj(2);
}
void add_svalue_int(const std::string &cls, const char *name, int32_t v) {
    g_sbytes[{cls, name}] = (uint64_t)(uint32_t)v;
    g_fields[{cls, name}] = new_obj(2);
}
void add_nested(const std::string &outer, const std::string &simple) {
    void *k = add_class(outer + "/" + simple);
    g_nested[outer][simple] = k;
}
void add_method(const char *name, int nargs) {
    void *m = new_obj(3);
    g_methods[{name, nargs}] = m;
    g_method_arity[m] = nargs;
}

// --- stubs ---
// find_class é método do struct (il2cpp_min.h): o stub embaixo é a classe de
// cada "imagem", e class_from_name resolve por nome — é por lá que o teste
// controla o que existe.
void *stub_domain_get() { return new_obj(0); }
void **stub_domain_get_assemblies(void *domain, size_t *count) {
    (void)domain;
    static void *asms[1];
    asms[0] = new_obj(20);
    *count = 1;
    return asms;
}
void *stub_assembly_get_image(void *asms) { return asms; }
void *stub_class_from_name(void *image, const char *ns, const char *name) {
    (void)image;
    std::string fqn = (ns && *ns) ? std::string(ns) + "." + name : std::string(name);
    auto it = g_by_name.find(fqn);
    return it == g_by_name.end() ? nullptr : it->second;
}
void *stub_get_field(void *klass, const char *name) {
    if (!klass) return nullptr;
    auto cn = g_class_name.find(klass);
    if (cn != g_class_name.end()) {
        auto f = g_fields.find({cn->second, name});
        if (f != g_fields.end()) return f->second;
    }
    return nullptr;  // campo existe só em outra classe
}
void stub_field_get_value(void *obj, void *field, void *out) {
    (void)obj;
    (void)field;
    *(void **)out = new_obj(4);
}
void stub_field_static_get_value(void *field, void *out) {
    (void)field;
    uint64_t bytes = 0;
    for (const auto &kv : g_sbytes) {
        bytes = kv.second;
        break;  // único campo estático por teste, dá
    }
    std::memcpy(out, &bytes, sizeof(bytes));  // sizeof(tipo) do campo
}
void stub_field_set_value(void *obj, void *field, void *value) {
    (void)obj;
    (void)field;
    (void)value;
    g_rec.field_set_value++;
}
void *stub_object_new(void *klass) {
    (void)klass;
    g_rec.object_new++;
    return new_obj(5);
}
const char *stub_class_get_name(void *klass) {
    static thread_local std::string tmp;
    auto it = g_class_name.find(klass);
    tmp = it == g_class_name.end() ? "" : it->second.substr(it->second.rfind('/') + 1);
    return tmp.c_str();
}
void *stub_class_get_nested_types(void *outer, void **iter) {
    if (*iter != nullptr) return nullptr;  // uma passada só
    *iter = (void *)1;
    auto it = g_class_name.find(outer);
    if (it == g_class_name.end()) return nullptr;
    auto n = g_nested.find(it->second);
    if (n == g_nested.end() || n->second.empty()) return nullptr;
    return n->second.begin()->second;
}
void *stub_get_method(void *klass, const char *name, int nargs) {
    (void)klass;
    g_rec.nargs = nargs;  // o lookup é por aridade: é o que o bug explorava
    auto m = g_methods.find({name, nargs});
    return m == g_methods.end() ? nullptr : m->second;
}
void *stub_get_class_of(void *obj) {
    (void)obj;
    return new_obj(7);
}
void *stub_runtime_invoke(void *method, void *obj, void **args, void **exc) {
    (void)obj;
    (void)exc;
    g_rec.invoked = true;
    // o runtime sabe a aridade pelo MethodInfo e lê exatamente N valores —
    // é por isso que o stub NÃO pode varrer até achar null.
    auto it = g_method_arity.find(method);
    int n = it == g_method_arity.end() ? 0 : it->second;
    for (int i = 0; i < n && args; i++) g_rec.args.push_back(args[i]);
    return new_obj(9);
}

Il2Cpp make_il() {
    Il2Cpp il{};
    il.domain_get = stub_domain_get;
    il.domain_get_assemblies = stub_domain_get_assemblies;
    il.assembly_get_image = stub_assembly_get_image;
    il.class_from_name = stub_class_from_name;
    il.class_get_field_from_name = stub_get_field;
    il.field_get_value = stub_field_get_value;
    il.field_static_get_value = stub_field_static_get_value;
    il.field_set_value = stub_field_set_value;
    il.object_new = stub_object_new;
    il.object_get_class = stub_get_class_of;
    il.class_get_method_from_name = stub_get_method;
    il.runtime_invoke = stub_runtime_invoke;
    il.class_get_name = stub_class_get_name;
    il.class_get_nested_types = stub_class_get_nested_types;
    return il;
}

// Show de 2 args (adUnitId, listener) — o caso do UnityAds, em que o closer
// lê o ÚLTIMO arg como listener. args[0]=adUnit, args[1]=listener.
UNoAdsFireCtx ctx_for(Il2Cpp &il) {
    void *a0 = new_obj(10), *a1 = new_obj(11);
    UNoAdsFireCtx c{&il, nullptr, new_obj(13), {a0, a1, nullptr, nullptr}, 2};
    return c;
}

void check(bool cond, const char *what) {
    if (!cond) {
        std::printf("FALHOU: %s\n", what);
        std::exit(1);
    }
}

const char *GOOGLE = "GoogleMobileAds.Api.InterstitialAd";

} // namespace

int main() {
    // --- Google v6: OnAdClosed é EventHandler<EventArgs> -> Invoke com 2 ---
    {
        reset();
        Il2Cpp il = make_il();
        void *k = add_class(GOOGLE);
        add_ivalue(GOOGLE, "OnAdClosed", new_obj(20));
        add_class("System.EventArgs");
        add_method("Invoke", 2);
        UNoAdsFireCtx c = ctx_for(il);
        c.klass = k;
        check(uno_close_google(c), "google v6: fecha e suprime");
        check(g_rec.invoked, "google v6: Invoke chamado");
        check(g_rec.nargs == 2, "google v6: aridade 2 (EventHandler<EventArgs>)");
    }
    // --- Google v7+: OnAdFullScreenContentClosed é Action -> Invoke com 0 ---
    {
        reset();
        Il2Cpp il = make_il();
        void *k = add_class(GOOGLE);
        add_ivalue(GOOGLE, "OnAdFullScreenContentClosed", new_obj(20));
        add_method("Invoke", 0);
        UNoAdsFireCtx c = ctx_for(il);
        c.klass = k;
        check(uno_close_google(c), "google v7: fecha e suprime");
        check(g_rec.nargs == 0, "google v7: aridade 0 (Action)");
    }
    // --- Google sem callback nenhum: NÃO suprime (regra da revisão) ---
    {
        reset();
        Il2Cpp il = make_il();
        void *k = add_class(GOOGLE);
        UNoAdsFireCtx c = ctx_for(il);
        c.klass = k;
        check(!uno_close_google(c), "google sem callback: nao suprime");
        check(c.note != nullptr, "google sem callback: nota pro log");
        check(!g_rec.invoked, "google sem callback: nada invocado");
    }

    // --- Unity Ads: o estado tem que chegar como CAIXA ---
    {
        reset();
        Il2Cpp il = make_il();
        const char *ENUM = "UnityEngine.Advertisements.UnityAdsShowCompletionState";
        add_class(ENUM);
        add_svalue_int(ENUM, "COMPLETED", 1);  // Completed = 1
        add_field(ENUM, "value__");
        add_method("OnUnityAdsShowComplete", 2);
        UNoAdsFireCtx c = ctx_for(il);
        check(uno_close_unity(c), "unity: fecha e suprime");
        check(g_rec.nargs == 2, "unity: aridade 2 (adUnitId, estado)");
        check(g_rec.object_new == 1, "unity: caixa criada (object_new)");
        check(g_rec.field_set_value == 1, "unity: value__ gravado na caixa");
        check(g_rec.args.size() == 2, "unity: 2 args no invoke");
        check(g_rec.args[1] != (void *)(uintptr_t)1 && g_rec.args[1] != nullptr,
              "unity: estado NAO e o int cru (1) — era o SIGSEGV");
        check(g_rec.args[1] != nullptr, "unity: caixa nao nula");
    }
    // --- Unity sem value__ (não é enum): NÃO suprime ---
    {
        reset();
        Il2Cpp il = make_il();
        const char *ENUM = "UnityEngine.Advertisements.UnityAdsShowCompletionState";
        add_class(ENUM);
        add_svalue_int(ENUM, "COMPLETED", 1);
        UNoAdsFireCtx c = ctx_for(il);
        check(!uno_close_unity(c), "unity sem value__: nao suprime");
        check(c.note != nullptr, "unity sem value__: nota pro log");
    }

    // --- LevelPlay: Action<AdInfo> -> 1 arg ---
    {
        reset();
        Il2Cpp il = make_il();
        const char *CLS = "LevelPlayInterstitialAd";
        void *k = add_class(CLS);
        add_ivalue(CLS, "OnAdClosed", new_obj(21));
        add_method("Invoke", 1);
        UNoAdsFireCtx c = ctx_for(il);
        c.klass = k;
        check(uno_close_levelplay(c), "levelplay: fecha e suprime");
        check(g_rec.nargs == 1, "levelplay: aridade 1 (Action<LevelPlayAdInfo>)");
    }

    // --- AppLovin MAX: onAdHiddenEvent é Action<string, AdInfo> -> 2 args ---
    {
        reset();
        Il2Cpp il = make_il();
        add_class("MaxSdkCallbacks");
        add_nested("MaxSdkCallbacks", "Interstitial");
        add_svalue_ref("MaxSdkCallbacks/Interstitial", "onAdHiddenEvent", new_obj(22));
        add_method("Invoke", 2);
        UNoAdsFireCtx c = ctx_for(il);
        c.args[0] = new_obj(23);  // adUnitIdentifier (1º arg do Show)
        check(uno_close_max_interstitial(c), "appLovin: fecha e suprime");
        check(g_rec.nargs == 2, "appLovin: aridade 2 (Action<string, AdInfo>)");
        check(g_rec.args.size() == 2 && g_rec.args[0] == c.args[0],
              "appLovin: 1º arg do Invoke é o adUnitId do Show");
    }
    // --- AppLovin sem o campo: NÃO suprime ---
    {
        reset();
        Il2Cpp il = make_il();
        add_class("MaxSdkCallbacks");
        add_nested("MaxSdkCallbacks", "Interstitial");
        UNoAdsFireCtx c = ctx_for(il);
        check(!uno_close_max_interstitial(c), "appLovin sem onAdHiddenEvent: nao suprime");
        check(c.note != nullptr, "appLovin sem campo: nota pro log");
    }

    // --- Metica: OnAdHidden é Action<MeticaAd> -> 1 arg, com MeticaAd com ctor ---
    {
        reset();
        Il2Cpp il = make_il();
        add_class("Metica.Ads.MeticaAdsCallbacks");
        add_nested("Metica.Ads.MeticaAdsCallbacks", "Interstitial");
        add_svalue_ref("Metica.Ads.MeticaAdsCallbacks/Interstitial", "OnAdHidden", new_obj(24));
        add_class("Metica.Ads.MeticaAd");
        add_method(".ctor", 0);
        add_method("Invoke", 1);
        UNoAdsFireCtx c = ctx_for(il);
        check(uno_close_metica(c), "metica: fecha e suprime");
        check(g_rec.nargs == 1 || g_rec.nargs == 0,
              "metica: ctor (0) e depois Invoke (1)");
        check(g_rec.object_new == 1, "metica: MeticaAd criado");
        check(g_rec.args.size() == 1 && g_rec.args[0] != nullptr,
              "metica: Invoke com o MeticaAd");
    }
    // --- Metica sem callback: NÃO suprime ---
    {
        reset();
        Il2Cpp il = make_il();
        add_class("Metica.Ads.MeticaAdsCallbacks");
        add_nested("Metica.Ads.MeticaAdsCallbacks", "Interstitial");
        UNoAdsFireCtx c = ctx_for(il);
        check(!uno_close_metica(c), "metica sem OnAdHidden: nao suprime");
    }

    // --- tabela: sobrecargas do MAX + pool ---
    {
        check(uno_hook_total() == 15, "15 hooks (MAX virou 6)");
        check(uno_hook_total() <= (std::size_t)U_NOADS_TRAMP_N, "pool cobre os hooks");
        for (std::size_t i = 0; i < U_NOADS_TARGET_COUNT; i++)
            check(U_NOADS_TARGETS[i].show_count > 0 && U_NOADS_TARGETS[i].show_count <= 6,
                  "show_count dentro do array");
    }
    std::printf("u_noads closers: OK (enum em caixa, aridades, sem-callback-nao-suprime)\n");
    return 0;
}
