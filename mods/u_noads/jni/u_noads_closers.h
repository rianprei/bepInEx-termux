// u_noads_closers.h — os fechamentos de cada SDK, isolados do resto do mod
// (sem Android, sem Dobby) para o teste host exercitar de verdade.
//
// REGRA (revisao): NUNCA suprimir sem fechar. Se o delegate/callback do jogo nao
// for encontrado ou nao puder ser invocado, o closer devolve false e o
// dispatch repassa pro Show original — o anuncio aparece. Ad que some sem o
// evento de fechamento trava o fluxo do jogo (loading infinito), o que e pior
// que o anuncio.
//
// Tipos conferidos no fonte oficial de cada SDK (ver ADAPTERS.md para a URL e a
// linha). Resumo do que cada Invoke espera, que e o que o `nargs` precisa ser:
//   GoogleMobileAds v6  InterstitialAd.OnAdClosed            EventHandler<EventArgs> -> 2
//   GoogleMobileAds v7+ InterstitialAd.OnAdFullScreenContentClosed  Action        -> 0
//   UnityAds            IUnityAdsShowListener.OnUnityAdsShowComplete(string, enum) -> 2
//   LevelPlay           InterstitialAd.OnAdClosed            Action<LevelPlayAdInfo>  -> 1
//   AppLovin MAX        MaxSdkCallbacks.Interstitial.onAdHiddenEvent  Action<string, AdInfo> -> 2
//   Metica              MeticaAdsCallbacks/Interstitial.OnAdHidden    Action<MeticaAd>       -> 1
#pragma once

#include <cstring>
#include <string>

#include "../../common/il2cpp_min.h"
#include "u_noads_pure.h"  // uno_split (host-testável, sem Android)
#include "u_noads_targets.h"

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

// Chama Delegate.Invoke com N args (N = aridade real do delegate: o lookup do
// il.call usa nargs, então passar N a mais nao é inofensivo — o método não é
// encontrado e o repasse acontece). Delegate null = ninguém inscrito: não há o
// que notificar, então o fechamento não pode ser garantido.
static bool uno_invoke(const Il2Cpp &il, void *dlg, void **args, int nargs) {
    if (!dlg) return false;
    bool ok = false;
    il.call(dlg, "Invoke", args, nargs, &ok);
    return ok;
}

static void *uno_event_args(const Il2Cpp &il) {
    void *k = il.find_class("System", "EventArgs");
    return k ? il.object_new(k) : nullptr;
}

// Enum REALMENTE boxeado. field_static_get_value copia os bytes do valor do
// campo (o int de 4 bytes do const) — NÃO devolve objeto; passar esse número
// como ponteiro do enum no runtime_invoke faz o invoker desreferenciar
// endereço inválido e o jogo take SIGSEGV (achado da revisão, coberto pelo
// teste host). O jeito é alocar a caixa e gravar o value__ dentro.
static void *uno_box_enum(const Il2Cpp &il, const char *ns, const char *klass,
                          const char *member) {
    void *k = il.find_class(ns, klass);
    if (!k) return nullptr;
    // O valor do const vem cru (4 bytes do enum) — copie antes de boxing.
    void *raw_slot = nullptr;
    if (!uno_read_sfield(il, k, member, raw_slot)) return nullptr;
    int32_t value = 0;
    std::memcpy(&value, &raw_slot, sizeof(value));
    void *vf = il.class_get_field_from_name(k, "value__");
    if (!vf) return nullptr;  // nao é enum: melhor repassar do que quebrar
    void *boxed = il.object_new(k);
    if (!boxed) return nullptr;
    il.field_set_value(boxed, vf, &value);
    return boxed;
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

// --- closers (um por SDK) ---

// Google: dois plugins no mundo. v6 tem OnAdClosed (EventHandler<EventArgs>,
// Invoke com 2 args); v7+Raisa OnAdFullScreenContentClosed (Action, 0 args) a
// partir do evento da ponte interna OnAdDidDismissFullScreenContent — que NÃO é
// campo do InterstitialAd e por isso não é our alvo.
// Fontes: googleads-mobile-unity, Api/InterstitialAd.cs:54 (v7) e o
// googleads-mobile-unity-plugin (v6) Api/InterstitialAd.cs (OnAdClosed).
bool uno_close_google(UNoAdsFireCtx &c) {
    void *dlg = nullptr;
    if (uno_read_ifield(*c.il, c.klass, c.self, "OnAdClosed", dlg)) {
        void *args[2] = {c.self, uno_event_args(*c.il)};
        return uno_invoke(*c.il, dlg, args, 2);
    }
    if (uno_read_ifield(*c.il, c.klass, c.self, "OnAdFullScreenContentClosed", dlg)) {
        void *args[1] = {nullptr};  // Action: 0 params (o 1º é ignorado)
        return uno_invoke(*c.il, dlg, args, 0);
    }
    c.note = "nenhum callback de fechamento encontrado (plugin v6/v7 nao reconhecido)";
    return false;  // sem callback nao tem como fechar: nao suprime
}

// Unity Ads: o listener é o ÚLTIMO arg do Show e o adUnitId o arg0. O estado
// "COMPLETED" é um enum → caixa de verdade (ver uno_box_enum).
// Fonte: docs.unity.com/.../unity-sdk/api/unity-api (Advertisement.Show,
// IUnityAdsShowListener.OnUnityAdsShowComplete, UnityAdsShowCompletionState).
bool uno_close_unity(UNoAdsFireCtx &c) {
    void *listener = c.args[c.show_argc - 1];
    void *ad_unit = c.args[0];
    if (!listener || !ad_unit) {
        c.note = "Show sem listener/adUnitId (assinatura diferente da tabelada?)";
        return false;
    }
    void *completed = uno_box_enum(*c.il, "UnityEngine.Advertisements",
                                  "UnityAdsShowCompletionState", "COMPLETED");
    if (!completed) {
        c.note = "UnityAdsShowCompletionState nao encontrado ou sem value__";
        return false;
    }
    void *args[2] = {ad_unit, completed};
    bool ok = false;
    c.il->call(listener, "OnUnityAdsShowComplete", args, 2, &ok);
    if (!ok) c.note = "OnUnityAdsShowComplete levantou excecao no jogo";
    return ok;
}

// LevelPlay: OnAdClosed é Action<LevelPlayAdInfo> (Invoke com 1 arg). AdInfo não
// é fabricável → null; se o handler do jogo dereferenciar, vira exceção no
// invoke, o repassa e o log mostra "fechamento falhou".
// Fonte: docs.unity.com/en-us/grow/levelplay/sdk/unity/interstitial-integration.
bool uno_close_levelplay(UNoAdsFireCtx &c) {
    void *dlg = nullptr;
    if (!uno_read_ifield(*c.il, c.klass, c.self, "OnAdClosed", dlg)) {
        c.note = "OnAdClosed nao encontrado";
        return false;
    }
    void *args[1] = {nullptr};
    bool ok = uno_invoke(*c.il, dlg, args, 1);
    if (!ok) c.note = "OnAdClosed levantou excecao (AdInfo null)";
    return ok;
}

// AppLovin MAX: onAdHiddenEvent é Action<string, AdInfo> (Invoke com 2 args).
// args[0] do Show é o adUnitIdentifier (MaxSdkAndroid.cs:621 — assinatura
// (adUnitIdentifier, placement, customData)); AdInfo não é fabricável → null.
bool uno_close_max_in(UNoAdsFireCtx &c, const char *nested) {
    void *cb = uno_find_nested(*c.il, "MaxSdkCallbacks", nested);
    if (!cb) {
        c.note = "MaxSdkCallbacks/<nested> nao encontrado (versao do plugin diferente?)";
        return false;
    }
    void *action = nullptr;
    const char *fields[] = {"onAdHiddenEvent", "OnAdHiddenEvent"};
    for (int i = 0; i < 2 && !action; i++) uno_read_sfield(*c.il, cb, fields[i], action);
    if (!action) {
        c.note = "nenhum jogo inscrito em onAdHiddenEvent";
        return false;
    }
    void *args[2] = {c.args[0], nullptr};
    bool ok = uno_invoke(*c.il, action, args, 2);
    if (!ok) c.note = "onAdHiddenEvent levantou excecao (AdInfo null)";
    return ok;
}

bool uno_close_max_interstitial(UNoAdsFireCtx &c) { return uno_close_max_in(c, "Interstitial"); }
bool uno_close_max_appopen(UNoAdsFireCtx &c) { return uno_close_max_in(c, "AppOpen"); }

// Metica: OnAdHidden é Action<MeticaAd> (Invoke com 1 arg). O dump do SA2 mostra
// MeticaAd com .ctor de 0 args (e outro de 11): construímos via ctor para o
// handler não receber lixo. Ainda pode faltar campo de referência → NRE, que
// aparece como "fechamento falhou" no log (com a nota).
// Fonte: dump do jogo, F Metica.Ads.MeticaAdsCallbacks/Interstitial OnAdHidden
// System.Action<Metica.Ads.MeticaAd> 1 32 e M Metica.Ads.MeticaAd .ctor 0.
bool uno_close_metica(UNoAdsFireCtx &c) {
    void *cb = uno_find_nested(*c.il, "Metica.Ads.MeticaAdsCallbacks", "Interstitial");
    if (!cb) {
        c.note = "MeticaAdsCallbacks/Interstitial nao encontrado";
        return false;
    }
    void *action = nullptr;
    if (!uno_read_sfield(*c.il, cb, "OnAdHidden", action)) {
        c.note = "OnAdHidden nao encontrado";
        return false;
    }
    if (!action) {
        c.note = "nenhum jogo inscrito em OnAdHidden";
        return false;
    }
    void *ad = nullptr;
    void *ad_klass = c.il->find_class("Metica.Ads", "MeticaAd");
    if (ad_klass) {
        ad = c.il->object_new(ad_klass);
        if (ad) {
            void *ctor = c.il->class_get_method_from_name(ad_klass, ".ctor", 0);
            if (ctor) {
                bool ok = false;
                c.il->call(ad, ".ctor", nullptr, 0, &ok);
                if (!ok) c.note = "MeticaAd: ctor levantou excecao (vai com objeto cru)";
            } else {
                c.note = "MeticaAd sem .ctor de 0 args (vai com objeto cru)";
            }
        }
    }
    void *args[1] = {ad};
    bool ok = uno_invoke(*c.il, action, args, 1);
    if (!ok && !c.note) c.note = "OnAdHidden levantou excecao no jogo";
    return ok;
}
