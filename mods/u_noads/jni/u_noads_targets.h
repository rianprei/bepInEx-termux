// u_noads_targets.h — tabela de SDKs de anúncio (só interstitial/app-open;
// rewarded fica de fora por escopo). Cada show carrega seu closer: a função
// que dispara o MESMO caminho que o SDK usa ao fechar (ver ADAPTERS.md pra
// alvo, assinatura, fonte oficial e como fecha, por SDK).
//
// Convenções:
// - closer == nullptr: Show retorna bool (Meta) — suprime retornando false
//   (caminho legítimo "sem fill" do SDK), sem disparar nada.
// - closer != nullptr: Show é void — suprime e dispara o closer; se o closer
//   falhar, o fake REPASSA pro original (anúncio aparece; nunca trava o jogo).
#pragma once

#include <cstddef>

// Forward (o header não inclui il2cpp_min.h pra continuar host-testável).
struct Il2Cpp;

struct UNoAdsFireCtx {
    const struct Il2Cpp *il;
    void *klass;      // classe onde o Show foi achado
    void *self;       // this do Show (null se estático)
    void *args[4];    // a, b, c, d do fake (arg0..arg2 + MethodInfo perdidos)
    int show_argc;    // nº de args do Show hookado (sem this/MethodInfo)
};

// Dispara o fechamento; true = fluxo do jogo continua (suprime o anúncio).
typedef bool (*UNoAdsCloser)(UNoAdsFireCtx &);

struct UNoAdsShow {
    const char *klass;    // FQN ("A.B.C") ou sem namespace ("MaxSdk")
    const char *method;   // Show/ShowAd/ShowInterstitial...
    int argc;             // nº de args C# (sem this); 0..3 (fake tem 5 slots)
    UNoAdsCloser closer;  // nullptr = Show retorna bool (Meta)
};

enum class UNoAdsSdk {
    GoogleMobileAds,
    UnityAds,
    LevelPlay,
    AppLovinMax,
    MetaAudience,
    Metica,
};

struct UNoAdsTarget {
    UNoAdsSdk sdk;
    const char *label;
    UNoAdsShow shows[4];
    int show_count;
};

bool uno_close_google(UNoAdsFireCtx &);
bool uno_close_unity(UNoAdsFireCtx &);
bool uno_close_levelplay(UNoAdsFireCtx &);
bool uno_close_max_interstitial(UNoAdsFireCtx &);
bool uno_close_max_appopen(UNoAdsFireCtx &);
bool uno_close_metica(UNoAdsFireCtx &);

inline const UNoAdsShow GOOGLE_SHOWS[] = {
    {"GoogleMobileAds.Api.InterstitialAd", "Show", 0, uno_close_google},
    {"GoogleMobileAds.Api.AppOpenAd", "Show", 0, uno_close_google},
};

inline const UNoAdsShow UNITY_SHOWS[] = {
    // Show(adUnitId, listener) e Show(adUnitId, options, listener):
    // listener é sempre o ÚLTIMO arg (ver ADAPTERS.md).
    {"UnityEngine.Advertisements.Advertisement", "Show", 2, uno_close_unity},
    {"UnityEngine.Advertisements.Advertisement", "Show", 3, uno_close_unity},
};

inline const UNoAdsShow LEVELPLAY_SHOWS[] = {
    {"LevelPlayInterstitialAd", "ShowAd", 0, uno_close_levelplay},
    {"LevelPlayInterstitialAd", "ShowAd", 1, uno_close_levelplay},
};

inline const UNoAdsShow MAX_SHOWS[] = {
    {"MaxSdk", "ShowInterstitial", 3, uno_close_max_interstitial},
    {"MaxSdk", "ShowAppOpenAd", 3, uno_close_max_appopen},
};

inline const UNoAdsShow META_SHOWS[] = {
    // Show retorna bool: suprime com false (closer null).
    {"AudienceNetwork.InterstitialAd", "Show", 0, nullptr},
};

inline const UNoAdsShow METICA_SHOWS[] = {
    {"Metica.Ads.MeticaAds", "ShowInterstitial", 3, uno_close_metica},
    {"Metica.Ads.MeticaAdsImpl", "ShowInterstitial", 3, uno_close_metica},
};

inline const UNoAdsTarget U_NOADS_TARGETS[] = {
    {UNoAdsSdk::GoogleMobileAds, "GoogleMobileAds", {GOOGLE_SHOWS[0], GOOGLE_SHOWS[1]}, 2},
    {UNoAdsSdk::UnityAds, "UnityAds", {UNITY_SHOWS[0], UNITY_SHOWS[1]}, 2},
    {UNoAdsSdk::LevelPlay, "LevelPlay", {LEVELPLAY_SHOWS[0], LEVELPLAY_SHOWS[1]}, 2},
    {UNoAdsSdk::AppLovinMax, "AppLovinMAX", {MAX_SHOWS[0], MAX_SHOWS[1]}, 2},
    {UNoAdsSdk::MetaAudience, "MetaAudience", {META_SHOWS[0]}, 1},
    {UNoAdsSdk::Metica, "Metica", {METICA_SHOWS[0], METICA_SHOWS[1]}, 2},
};

inline constexpr std::size_t U_NOADS_TARGET_COUNT =
    sizeof(U_NOADS_TARGETS) / sizeof(U_NOADS_TARGETS[0]);

// Pool de trampolins (vive no mod.cpp). Capacidade garantida por dois
// caminhos: guarda em runtime na instalação + assert no teste host
// (endereço de função em array constexpr não conta como odr-use pro
// -Wunused-function do clang, então a tabela é `const`, não `constexpr`).
#define U_NOADS_TRAMP_N 16

// Total de hooks = soma dos shows.
inline std::size_t uno_hook_total() {
    std::size_t n = 0;
    for (std::size_t i = 0; i < U_NOADS_TARGET_COUNT; i++)
        n += (std::size_t)U_NOADS_TARGETS[i].show_count;
    return n;
}
