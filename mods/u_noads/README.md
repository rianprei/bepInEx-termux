# u_noads — supressão forçada de interstitial/app-open (F11b)

Lê `*.bpatch`/`.conf`? Não: este é um mod autônomo. Ele hooka os `Show` de
interstitial/app-open de 6 SDKs e **dispara o mesmo fechamento que o SDK
dispararia** ao fechar o anúncio. Rewarded fica de fora por escopo.

- Fuentes por SDK, assinatura e o que cada `Invoke` espera: `ADAPTERS.md`.
- Regra que não se negocia: **nunca suprimir sem fechar**. Sem callback do jogo
  (ou com invoke falhando) o anúncio volta a aparecer, e o motivo vai pro log.
- Log: logcat `u_noads` + `/data/data/<pkg>/files/bepinex/log.txt` (contrato C1).
- Sem `BEPINEX_PKG` (o loader não setou) o mod fica inerte: sem pasta de mods
  não há o que hookar e o log C1 não existe. Mesmo comportamento do
  `u_patch`/`u_frida`.
- Em ARM32, registra “não suportado em 32-bit” e sai: a guarda de prólogo foi
  escrita para instruções AArch64 e ainda não há adaptação validada.

## Testes host (sem device)

```
g++ -std=c++17 -Wall -Wextra -I jni -o /tmp/uno_tt  test_targets.cpp && /tmp/uno_tt
g++ -std=c++17 -Wall -Wextra -I jni -o /tmp/uno_cl  test_closers.cpp && /tmp/uno_cl
```

- `test_targets.cpp`: contagem da tabela (15 hooks), split de nome, guarda de
  método curto, pool de trampolins.
- `test_closers.cpp`: **exercita os 6 closers** contra um `Il2Cpp` falso que
  registra `object_new`/`field_set_value`/`runtime_invoke`. Cobre a caixa real do
  enum do UnityAds (o bug que fazia o jogo tomar SIGSEGV), a aridade de cada
  `Invoke` e a regra "sem callback não suprime".

## Build

```
~/Android/Sdk/ndk/23.2.8568313/ndk-build NDK_PROJECT_PATH=. \
  APP_BUILD_SCRIPT=jni/Android.mk NDK_APPLICATION_MK=jni/Application.mk -B
```

O `Application.mk` tem `-Werror`: o gate do repo é build sem warning.
