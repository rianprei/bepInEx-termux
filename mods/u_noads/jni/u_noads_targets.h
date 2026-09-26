#pragma once

#include <cstddef>

enum class UNoAdsSdk {
    GoogleMobileAds,
    UnityAds,
    LevelPlay,
    AppLovinMax,
};

struct UNoAdsTarget {
    UNoAdsSdk sdk;
    const char *label;
    const char *const *class_names;
    std::size_t class_count;
    const char *const *show_names;
    std::size_t show_count;
    const char *const *close_names;
    std::size_t close_count;
    int show_args;
    int close_args;
    bool close_is_static;
};

inline constexpr const char *GOOGLE_CLASSES[] = {
    "GoogleMobileAds.Api.InterstitialAd",
    "GoogleMobileAds.Api.AppOpenAd",
};
inline constexpr const char *GOOGLE_SHOW[] = {"Show"};
inline constexpr const char *GOOGLE_CLOSE[] = {
    "OnAdDismissedFullScreenContent",
    "OnAdClosed",
};

inline constexpr const char *UNITY_CLASSES[] = {
    "UnityEngine.Advertisements.Advertisement",
};
inline constexpr const char *UNITY_SHOW[] = {"Show"};
inline constexpr const char *UNITY_CLOSE[] = {
    "OnUnityAdsShowComplete",
    "OnUnityAdsShowFailure",
};

inline constexpr const char *LEVELPLAY_CLASSES[] = {
    "LevelPlayInterstitialAd",
    "IronSourceInterstitialEvents",
};
inline constexpr const char *LEVELPLAY_SHOW[] = {"ShowAd", "showAd"};
inline constexpr const char *LEVELPLAY_CLOSE[] = {"OnAdClosed", "onAdClosed"};

inline constexpr const char *MAX_CLASSES[] = {
    "MaxSdk",
    "MaxSdkBase",
};
inline constexpr const char *MAX_SHOW[] = {"ShowInterstitial", "ShowAppOpenAd"};
inline constexpr const char *MAX_CLOSE[] = {"OnAdHidden", "onAdHidden"};

inline constexpr UNoAdsTarget U_NOADS_TARGETS[] = {
    {UNoAdsSdk::GoogleMobileAds, "GoogleMobileAds", GOOGLE_CLASSES, 2, GOOGLE_SHOW, 1,
     GOOGLE_CLOSE, 2, 0, 0, false},
    {UNoAdsSdk::UnityAds, "UnityAds", UNITY_CLASSES, 1, UNITY_SHOW, 1, UNITY_CLOSE, 2, 1, 0,
     true},
    {UNoAdsSdk::LevelPlay, "LevelPlay", LEVELPLAY_CLASSES, 2, LEVELPLAY_SHOW, 2,
     LEVELPLAY_CLOSE, 2, 1, 0, false},
    {UNoAdsSdk::AppLovinMax, "AppLovinMAX", MAX_CLASSES, 2, MAX_SHOW, 2, MAX_CLOSE, 2, 1, 0,
     true},
};

inline constexpr std::size_t U_NOADS_TARGET_COUNT =
    sizeof(U_NOADS_TARGETS) / sizeof(U_NOADS_TARGETS[0]);
