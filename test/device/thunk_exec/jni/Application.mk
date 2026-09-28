APP_ABI := arm64-v8a
APP_PLATFORM := android-23
# Shim C++ precisa de headers padrão (cstdint) — libc++ estática.
APP_STL := c++_static
APP_CPPFLAGS := -std=c++17 -fno-exceptions -fno-rtti

# Build reproduzivel + simbolos preservados. Ver jni/repro.mk.
#
# TRÊS níveis, não dois: o projeto é test/device/thunk_exec, e ../.. a partir
# dele é test/ — onde não existe jni/repro.mk. Com dois níveis o include
# falhava, o ndk-build não produzia libs/arm64-v8a/thunk_exec, e o gate só
# descobria isso na ETAPA DE EXECUÇÃO (o fix_tls_palign.py reclamava do
# arquivo inexistente). Nos mods/<id>/ são dois níveis, que é por isso que o
# erro passou: aqui a profundidade é outra.
BEPINEX_REPRO_ROOT := $(abspath ../../..)
include $(BEPINEX_REPRO_ROOT)/jni/repro.mk
