# t_crash — mod de teste do crashguard (F1d)

Mata o processo 2s depois de carregar, de propósito. Existe para provar a
garantia G1 ("mod nunca derruba o jogo pra sempre") no device:

1. `cp libs/arm64-v8a/libt_crash.so /data/adb/bepinex/mods/<pkg>/`
2. abra o jogo 3x (reinicie o app entre as tentativas, não o celular)
3. 1ª e 2ª: morre 2s depois de subir. 3ª: sobe limpo, **sem mod**, e o
   logcat/log.txt mostra
   `mods desativados: o jogo fechou 2x logo depois de carregar — reative no Manager`
4. reativar: `rm /data/data/<pkg>/files/bepinex/crashguard` e
   `rm /data/data/<pkg>/files/bepinex/disabled_by_crashguard`
   (o botão "Reativar" do Manager faz o mesmo)

Build: `~/Android/Sdk/ndk/23.2.8568313/ndk-build NDK_PROJECT_PATH=.
APP_BUILD_SCRIPT=jni/Android.mk NDK_APPLICATION_MK=jni/Application.mk -B`

NÃO instale em jogo que você usa: ele mata o processo de propósito.
