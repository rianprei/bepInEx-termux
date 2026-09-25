# sa2content — Swamp Attack 2 com conteúdo novo

Alvo: `com.hyperdotstudios.swampattack2` 1.3.9 (Unity 6000.3.13f1, IL2CPP arm64). Só em memória: nenhum arquivo do jogo é escrito.

## O que muda

1. **Armas cortadas liberadas.** O jogo tem armas primárias que nenhum personagem usa. Cada personagem ganha uma delas, liberada no nível 1 dele:

   | Personagem | Arma |
   |---|---|
   | Slow Joe | Kalashnikov |
   | Welder | ProtonRay |
   | Uncle Hairy | UziGun |
   | Frosty | RayGun |
   | Pepper | Nailgun |
   | Neighbor Willy | HamsterGun |
   | Wei | M16ElectricGrenadeLauncher |
   | Sonny | OmeletteDayWeapon |
   | Larry | FireExtinguisher |

2. **Fusões.** Cada arma abaixo ganha a entrada de dano de efeito de outra:
   - Shotgun: gelo (IceCubeLauncher);
   - DoubleShotgun: veneno (RottenEgg);
   - Kalashnikov: choque (HamsterGun);
   - TankBusterRifle: radiação (AtomicBazooka).
3. **Fases remixadas.** As fases L05, L10 e L15 dos capítulos 2 a 9 (18 fases) terminam com o chefe do capítulo anterior, com a tela de apresentação de chefe (`present_screen` + `spawn "<Boss>" 50% 3`, antes do último `wait all`).
4. **Sem anúncio forçado entre fases.** `InterstitialAdManager.TryShowInterstitial` vira no-op. Os anúncios opcionais com recompensa continuam iguais.

## Unknown jogável (`SA2_ENABLE_UNKNOWN`, ligado)

O "Unknown" é o card "?" do elenco (`isPlayable 0`, sem prefab, skin, vida ou arma). O mod faz o seguinte:
- copia do Slow Joe prefab, skins, melhorias e ícones (`Il2Cpp::copy_field`);
- aplica o JSON do Unknown pelo `Apply` do jogo: jogável, vida e armas do Slow Joe, sem fase de liberação;
- troca a arma inicial por um clone em runtime da Shotgun (`Object.Internal_CloneSingle` + `Deserialize` da categoria `wep`), com o dano dela e os efeitos 1-7, 9 e 11. O controle mental (8) não entra, porque nenhuma arma do jogo tem.

Tudo isso roda na thread principal, dentro do hook de `TryApplyPendingPatches`.

Lição do crash: `il2cpp_field_set_value` recebe o próprio ponteiro do objeto pra campo de referência. Passar `&ptr` gravava lixo nas skins e no prefab, e o jogo crashava em chamada de interface (no parser e no ícone de recompensa do mapa).

Save: o jogo salva o progresso do Unknown pelo nome. Antes de tirar o mod, troque de personagem.

## Como funciona

O jogo tem um sistema próprio de patch de balanceamento. `GameBalancer.AvailableCategories` lista as categorias (`wep` armas, `red` personagens, `ld` scripts de fase, entre outras). Cada categoria tem:
- `CreateSnapshot()`, que devolve os itens atuais como `key` + `data` (JSON ou script);
- `Apply(GameBalanceCategory, StringBuilder)`, o mesmo parser que o jogo usa pros patches remotos.

O mod tira o snapshot, troca o `data` dos itens que estão em `jni/patches.h` (escrita via `il2cpp_gc_wbarrier_set_field`) e chama `Apply`.

Ele não usa `OnReceivedBalanceFromGrid`, porque esse caminho grava o patch num arquivo local do jogo (`SaveLocalPatch`).

A aplicação roda em dois momentos:
- no boot, assim que as três categorias existem;
- de novo depois de cada `GameBalancer.TryApplyPendingPatches`, pra um patch remoto do jogo não sobrescrever as mesmas chaves. Reaplicar é idempotente: `Apply` retorna 0 quando nada mudou.

Classe, método e offset de campo saem da API il2cpp exportada (`mods/common/il2cpp_min.h`).

## Gerar patches.h

`tools/gen_patches.py` lê um snapshot tirado em runtime (dump read-only das categorias via Frida) e a tabela guid→nome das armas:

```
python3 tools/gen_patches.py balance_snapshot.txt guid_names.json jni/patches.h
```

A lista de armas, as fusões e o mapa de chefes ficam no topo do script.

## Build

```
~/Android/Sdk/ndk/23.2.8568313/ndk-build NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=jni/Android.mk NDK_APPLICATION_MK=jni/Application.mk -B
```

## Teste rápido, sem reboot (Frida)

Com o jogo aberto:

```
adb push libs/arm64-v8a/libsa2content.so /data/local/tmp/libsa2content.so
adb shell chmod 644 /data/local/tmp/libsa2content.so
echo "Module.load('/data/local/tmp/libsa2content.so');" > /tmp/sa2c.js && adb push /tmp/sa2c.js /data/local/tmp/
adb shell su -c "/data/local/tmp/frida-inject -p \$(pidof com.hyperdotstudios.swampattack2) -s /data/local/tmp/sa2c.js -e"
adb logcat -s sa2content
```

Não mate o `frida-inject` no meio do script: isso já derrubou o jogo (SIGSEGV).

Log esperado:
- `ativo: 31 patches embutidos`;
- depois `ld: 18`, `red: 9`, `wep: 4 item(ns) trocados, Apply ok`.

## Deploy persistente

É igual ao `sa2ammo`: coloque o `libsa2content.so` em `/data/local/tmp/mods/com.hyperdotstudios.swampattack2/`, com o pacote na allowlist genérica.

## Status

Build ok (NDK 23.2). Aplicado no device via Frida em 2026-09-25: as 31 trocas foram aceitas pelo `Apply` do jogo, a reaplicação pós-`TryApplyPendingPatches` foi observada e não houve crash. Ainda falta confirmar em jogo o visual das armas novas, as fusões e o chefe no fim das fases.
