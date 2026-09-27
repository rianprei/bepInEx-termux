APP_STL := c++_static
APP_CPPFLAGS := -std=c++17 -fomit-frame-pointer -DANDROID -D_FORTIFY_SOURCE=2 -DNDEBUG -fstack-protector-strong -fvisibility=hidden -Wall -Wextra -Werror
APP_LDFLAGS := -Wl,-z,relro,-z,now -Wl,--as-needed
APP_PLATFORM := android-23
APP_ABI := arm64-v8a

# Build reproduzivel + simbolos preservados. Ver jni/repro.mk.
BEPINEX_REPRO_ROOT := $(abspath ../..)
include $(BEPINEX_REPRO_ROOT)/jni/repro.mk
