#!/bin/bash
# deploy_mod.sh <id> <pkg> — build + instala o mod no device (F2).
# ndk-build → adb push (staging em /data/local/tmp) → su cp/chmod/chcon →
# force-stop (o loader carrega o mod no próximo boot do processo).
#
# Contratos: C1 (/data/local/tmp/mods/<pkg>/, dir 755, arquivo 644, escrito
# via su) e F1c/SELinux — arquivo copiado DEPOIS do boot não ganha o rótulo
# bepinex_mod_file sozinho; sem o chcon, em Enforcing o jogo não lê.
# Só o .so: .conf/.json são com o Manager (F5). Nada toca arquivos do jogo.
set -euo pipefail
cd "$(dirname "$0")/.."

[ $# -eq 2 ] || { echo "uso: $0 <id> <pkg>" >&2; exit 2; }
id=$1
pkg=$2
[[ "$id" =~ ^[a-z0-9-]{3,48}$ ]] || { echo "id inválido: '$id' ([a-z0-9-]{3,48})" >&2; exit 2; }

NDK_BUILD=${NDK_BUILD:-$HOME/Android/Sdk/ndk/23.2.8568313/ndk-build}
"$NDK_BUILD" -C "mods/$id" NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=jni/Android.mk \
    NDK_APPLICATION_MK=jni/Application.mk -B -j4

so="mods/$id/libs/arm64-v8a/lib$id.so"
[ -f "$so" ] || { echo "build não gerou $so" >&2; exit 1; }

adb wait-for-device
adb shell su -c true || { echo "su indisponível no device" >&2; exit 1; }

mods=/data/local/tmp/mods
stage=/data/local/tmp/.deploy.$id.$$
# ENTREGA VIA STDIN (padrão do device_test.sh): `adb shell su -c "A && B"`
# re-divide os args no device — o su roda só "A" como root e o resto roda como
# shell (chmod/rm/chcon com Permission denied; achado real no device nesta
# rodada, POCO C75). Pelo stdin o comando chega inteiro.
printf '%s\n' "mkdir -p $mods/$pkg && chmod 755 $mods $mods/$pkg" | adb shell su
adb push "$so" "$stage"
printf '%s\n' "cp $stage $mods/$pkg/$id.so && rm -f $stage && chmod 644 $mods/$pkg/$id.so && chcon u:object_r:bepinex_mod_file:s0 $mods/$pkg/$id.so" | adb shell su

# Reinicia o processo do jogo pra recarregar os mods.
adb shell am force-stop "$pkg"
echo "instalado: $mods/$pkg/$id.so — abra o jogo de novo"
