APP_ABI := arm64-v8a
APP_PLATFORM := android-23
# Shim C++ precisa de headers padrão (cstdint) — libc++ estática.
APP_STL := c++_static
APP_CPPFLAGS := -std=c++17 -fno-exceptions -fno-rtti
