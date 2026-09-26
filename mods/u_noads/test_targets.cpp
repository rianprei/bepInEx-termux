#include "jni/u_noads_targets.h"

#include <cassert>
#include <cstring>

int main() {
    assert(U_NOADS_TARGET_COUNT == 4);
    assert(std::strcmp(U_NOADS_TARGETS[0].class_names[0],
                       "GoogleMobileAds.Api.InterstitialAd") == 0);
    assert(std::strcmp(U_NOADS_TARGETS[0].class_names[1],
                       "GoogleMobileAds.Api.AppOpenAd") == 0);
    assert(std::strcmp(U_NOADS_TARGETS[3].show_names[0], "ShowInterstitial") == 0);
    assert(std::strcmp(U_NOADS_TARGETS[3].show_names[1], "ShowAppOpenAd") == 0);
    for (std::size_t i = 0; i < U_NOADS_TARGET_COUNT; ++i) {
        assert(U_NOADS_TARGETS[i].close_count > 0);
        assert(U_NOADS_TARGETS[i].show_count > 0);
    }
}
