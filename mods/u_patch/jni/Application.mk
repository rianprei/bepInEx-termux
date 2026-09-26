APP_STL := c++_static
APP_CPPFLAGS := -std=c++17 -fomit-frame-pointer -DANDROID -D_FORTIFY_SOURCE=2 -DNDEBUG -fstack-protector-strong -fvisibility=hidden -Wall -Wextra
# -static-libstdc++/-static-libgcc saem de propósito: com APP_STL := c++_static
# o libstdc++ JÁ entra estático, e manter os dois flags fazia o clang++ emitir
# "argument unused during compilation" — que o gate trata como FAIL (a
# isenção saiu em 81efee2, quando o base limpou o flag de todos os mods).
APP_LDFLAGS := -Wl,-z,relro,-z,now -Wl,--as-needed
APP_PLATFORM := android-23
APP_ABI := arm64-v8a
