APP_STL := c++_static
APP_CPPFLAGS := -std=c++17 -fexceptions -frtti -fomit-frame-pointer -DANDROID -D_FORTIFY_SOURCE=2 -DNDEBUG -fstack-protector-strong -fvisibility=hidden
APP_LDFLAGS := -static-libstdc++ -static-libgcc -llog -Wl,-z,relro,-z,now -Wl,--as-needed
# -landroid removido: achado real (strace + leitura do zygiskd64) — nenhum
# símbolo do .so vem de libandroid.so, mas listar como NEEDED puxava uma
# cadeia transitiva quebrada nesta ROM (libandroid.so -> libharfbuzz_ng.so
# -> libicu.so ausente do namespace default), derrubando o dlopen do
# zygiskd64 especificamente pro companion (o app carrega OK porque o
# zygote tem namespace com acesso à APEX i18n; zygiskd64 não tem).
# --as-needed garante que nenhuma NEEDED morta volte a aparecer no futuro.
APP_PLATFORM := android-23
APP_ABI := arm64-v8a
