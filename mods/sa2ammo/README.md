# sa2ammo — Swamp Attack 2 com munição ilimitada

Alvo: `com.hyperdotstudios.swampattack2` 1.3.9 (Unity 6000.3.13f1, IL2CPP arm64). O jogo é offline, sem PvP.

O con mais citado nas reviews da Play Store e da App Store: o rifle fica sem munição no meio da fase, e o jogo cobra gema pra comprar mais.

## Como funciona

O próprio jogo tem a flag `WeaponInfo.unlimitedAmmo` (bool, `+0x70`), e o ISIL do Cpp2IL mostra onde ela é usada:
- `ComplexCreature.Shoot()`: `if (!weapon.unlimitedAmmo) weapon.currentAmmo.Amount -= 1`.
- `ComplexCreature.ReloadWeaponClip(bool)`: com a flag ligada, a reserva vale 1.000.000.
- `ComplexCreature.HasAmmo()`: só olha `currentAmmo.Amount > 0`.

O mod liga a flag nas armas primárias equipáveis (`canBeEquipped`, `type == 0`), ou seja, nas armas do jogador. Ele faz isso em dois hooks:
- `SelectWeapon`;
- `ReloadWeaponClip`, porque algumas trocas escrevem `selectedWeapon` direto, sem passar por `SelectWeapon`.

Se o inventário da arma já estiver zerado, ele sobe pra 1, senão `HasAmmo()` trava a arma.

As armas secundárias (granada, dinamite...) continuam com o limite normal.

Classe, método e offset de campo saem da API il2cpp exportada (`il2cpp_class_from_name`, `il2cpp_class_get_method_from_name`, `il2cpp_field_get_offset`). Não há offset fixo nem AOB.

## Build

```
~/Android/Sdk/ndk/23.2.8568313/ndk-build NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=jni/Android.mk NDK_APPLICATION_MK=jni/Application.mk -B
```

## Teste rápido, sem reboot (Frida)

Com o jogo aberto:

```
adb push libs/arm64-v8a/libsa2ammo.so /data/local/tmp/libsa2ammo.so
adb shell chmod 644 /data/local/tmp/libsa2ammo.so
echo "Module.load('/data/local/tmp/libsa2ammo.so');" > /tmp/sa2load.js && adb push /tmp/sa2load.js /data/local/tmp/
adb shell su -c "/data/local/tmp/frida-inject -p \$(pidof com.hyperdotstudios.swampattack2) -s /data/local/tmp/sa2load.js -e"
adb logcat -s sa2ammo
```

Log esperado: `ativo: SelectWeapon @... + ReloadWeaponClip @...`. Ao pegar ou recarregar uma arma, aparece `arma primária com munição ilimitada (#N)`.

## Deploy persistente (loader genérico)

Depende do `load_generic_pkg_mods` do `jni/main.cpp`, que precisa do módulo Zygisk atualizado e de reboot:

1. `echo com.hyperdotstudios.swampattack2 >> /data/local/tmp/bc_generic_allowlist.conf`
2. `mkdir -p /data/local/tmp/mods/com.hyperdotstudios.swampattack2`
3. Copiar `libsa2ammo.so` pra dentro dessa pasta.

## Status

Build ok (NDK 23.2). Testado no device via Frida em 2026-09-25: o mod resolve tudo (offsets batem com o dump) e liga a flag nas armas primárias (#1..#5 no log), sem crash. Usuário confirmou em jogo que a munição não cai mais.

A API il2cpp e o boot (fix do namespace do linker) ficam em `mods/common/il2cpp_min.h`, compartilhado com o `sa2content`.
