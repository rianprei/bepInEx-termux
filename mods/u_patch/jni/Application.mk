APP_STL := c++_static
APP_CPPFLAGS := -std=c++17 -fomit-frame-pointer -DANDROID -D_FORTIFY_SOURCE=2 -DNDEBUG -fstack-protector-strong -fvisibility=hidden -Wall -Wextra
# -static-libstdc++/-static-libgcc saem de propósito: com APP_STL := c++_static
# o libstdc++ JÁ entra estático, e manter os dois flags fazia o clang++ emitir
# "argument unused during compilation" — que o gate trata como FAIL (a
# isenção saiu em 81efee2, quando o base limpou o flag de todos os mods).
APP_LDFLAGS := -Wl,-z,relro,-z,now -Wl,--as-needed
APP_PLATFORM := android-23
APP_ABI := arm64-v8a

# Build reproduzível + símbolos preservados. Ver jni/repro.mk: sem isto o
# build-id muda com o diretório de build e o .so de release sai sem nome de
# função, então um crash do device não é simbolizável.
# O ndk-build é chamado de mods/<id>/ (verify_all, deploy, documentação), e
# a raiz do repo são dois níveis acima.
BEPINEX_REPRO_ROOT := $(abspath ../..)
include $(BEPINEX_REPRO_ROOT)/jni/repro.mk
