// test_targets.cpp — teste HOST do u_noads (g++ -std=c++17, sem NDK):
//   g++ -std=c++17 -Wall -Wextra -Ijni -o /tmp/t jni/../test_targets.cpp && /tmp/t
// Cobre a tabela (consistência que o device não checa barato) e a lógica
// pura (split/guarda). Os closers exigem il2cpp vivo: só no device (T1).
#include "jni/u_noads_targets.h"
#include "jni/u_noads_pure.h"

#include <cassert>
#include <cstring>
#include <cstdio>

// Stubs: os closers reais vivem no mod (precisam de il2cpp vivo). Aqui só
// importa que a tabela aponte pra função distinta por SDK.
bool uno_close_google(UNoAdsFireCtx &) { return false; }
bool uno_close_unity(UNoAdsFireCtx &) { return false; }
bool uno_close_levelplay(UNoAdsFireCtx &) { return false; }
bool uno_close_max_interstitial(UNoAdsFireCtx &) { return false; }
bool uno_close_max_appopen(UNoAdsFireCtx &) { return false; }
bool uno_close_metica(UNoAdsFireCtx &) { return false; }

int main() {
    // Tabela: 6 alvos (Google, Unity, LevelPlay, MAX, Meta, Metica).
    assert(U_NOADS_TARGET_COUNT == 6);
    assert(uno_hook_total() == 15);
    // Pool cobre a tabela (capacidade também guardada em runtime no mod).
    assert(uno_hook_total() <= (std::size_t)U_NOADS_TRAMP_N);
    for (std::size_t i = 0; i < U_NOADS_TARGET_COUNT; ++i) {
        const UNoAdsTarget &t = U_NOADS_TARGETS[i];
        assert(t.show_count > 0 && t.show_count <= 6);
        assert(t.label && t.label[0]);
        for (int j = 0; j < t.show_count; j++) {
            assert(t.shows[j].klass && t.shows[j].klass[0]);
            assert(t.shows[j].method && t.shows[j].method[0]);
            assert(t.shows[j].argc >= 0 && t.shows[j].argc <= 3);  // fake tem 5 slots
        }
    }
    // Só Meta suprime sem closer (Show retorna bool).
    for (std::size_t i = 0; i < U_NOADS_TARGET_COUNT; ++i) {
        const UNoAdsTarget &t = U_NOADS_TARGETS[i];
        for (int j = 0; j < t.show_count; j++) {
            bool is_meta = t.sdk == UNoAdsSdk::MetaAudience;
            assert((t.shows[j].closer == nullptr) == is_meta);
        }
    }
    // Unity: listener é o último arg (2 e 3 args).
    assert(U_NOADS_TARGETS[1].shows[0].argc == 2);
    assert(U_NOADS_TARGETS[1].shows[1].argc == 3);
    // MAX sem namespace resolve (split com ns vazia).
    char ns[128], nm[128];
    uno_split("MaxSdk", ns, sizeof(ns), nm, sizeof(nm));
    assert(ns[0] == '\0' && std::strcmp(nm, "MaxSdk") == 0);
    uno_split("GoogleMobileAds.Api.InterstitialAd", ns, sizeof(ns), nm, sizeof(nm));
    assert(std::strcmp(ns, "GoogleMobileAds.Api") == 0 && std::strcmp(nm, "InterstitialAd") == 0);
    uno_split("Metica.Ads.MeticaAdsCallbacks", ns, sizeof(ns), nm, sizeof(nm));
    assert(std::strcmp(ns, "Metica.Ads") == 0 && std::strcmp(nm, "MeticaAdsCallbacks") == 0);
    uno_split(nullptr, ns, sizeof(ns), nm, sizeof(nm));
    assert(ns[0] == '\0' && nm[0] == '\0');
    // Guarda de método curto: Dobby precisa de 4 palavras livres.
    const uint32_t ok[] = {0x910003E0u, 0xB9400800u, 0x0B000020u, 0xD65F03C0u};
    assert(uno_method_fits(ok));  // ret só na última = cabe
    const uint32_t tiny[] = {0xB9400800u, 0xD65F03C0u, 0xD503201Fu, 0xD503201Fu};
    assert(!uno_method_fits(tiny));  // ret na 2ª = curto
    const uint32_t branch[] = {0x910003E0u, 0x14000005u, 0xD503201Fu, 0xD503201Fu};
    assert(!uno_method_fits(branch));  // B incondicional no meio
    const uint32_t br[] = {0xD61F03C0u, 0xD503201Fu, 0xD503201Fu, 0xD503201Fu};
    assert(!uno_method_fits(br));  // br na 1ª
    assert(!uno_method_fits(nullptr));
    assert(uno_is_terminator(0xD65F03C0u) && uno_is_terminator(0xD61F03C0u) &&
           uno_is_terminator(0x14000005u));
    assert(!uno_is_terminator(0x34000020u) && !uno_is_terminator(0x94000005u));  // cbz/bl seguem
    std::printf("u_noads host: tabela + split + guarda OK (15 hooks, pool cobre)\n");
    return 0;
}
