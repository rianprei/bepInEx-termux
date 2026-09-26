APP_STL := c++_static
APP_CPPFLAGS := -std=c++17 -fomit-frame-pointer -DANDROID -D_FORTIFY_SOURCE=2 -DNDEBUG -fstack-protector-strong -fvisibility=hidden -Wall -Wextra
APP_LDFLAGS := -static-libstdc++ -static-libgcc -Wl,-z,relro,-z,now -Wl,--as-needed
APP_PLATFORM := android-23
APP_ABI := arm64-v8a
