# bepin-termux

Injeção/instrumentação nativa em runtime pra apps Android via **Zygisk**
(Magisk) + **Dobby** (inline hook ARM64) + ponte de controle pelo **Termux**
— o mesmo papel que o BepInEx cumpre no ecossistema Unity/Mono, mas pra
código nativo C++ (motores como Cocos2d-x, que não expõem um chainloader
gerenciado).

[![licença-MIT](https://img.shields.io/badge/licen%C3%A7a-MIT-green)](LICENSE)

Prova de conceito validada ao vivo contra o Battle Cats
(`jp.co.ponos.battlecatsen`), log-only — não altera o jogo. A arquitetura é
genérica: qualquer app com libs nativas dá pra hookar trocando os símbolos
alvo.

## Arquitetura

```
Zygote (fork) ──▶ processo do app ──▶ jni/main.cpp (módulo Zygisk)
                                          │
                                          │ dl_iterate_phdr até a lib nativa
                                          │ carregar, então hook via Dobby
                                          ▼
                                    símbolos JNI interceptados
                                          │
                     companion (root) ────┘
                     jni/companion.cpp
                          │
                          │ socket Unix abstract @bc_companion
                          │ (SO_PEERCRED, fail-closed)
                          ▼
                    Termux / adb forward
                    (termux_client.py / bc_log_viewer.py)
```

### 1. `jni/main.cpp` — módulo Zygisk (API v5)

Roda dentro do processo do app alvo. Espera a lib nativa carregar
(`dl_iterate_phdr` em polling), resolve os símbolos e instala hooks inline
via Dobby.

**Resolução de alvo em cascata** (sobrevive a updates do APK):
1. RVA fixo de uma offset DB pré-computada (mais rápido, exige build-id
   confirmado).
2. Símbolo JNI export via `DobbySymbolResolver`.
3. Scan por assinatura de prólogo (só aceita se a assinatura casar uma
   única vez — nunca um match ambíguo).

**Fail-safe**: qualquer etapa que falhar deixa aquele hook específico
DORMANT (log-only, sem crashar o processo do jogo). Ver
`context/bc-poc-hardening-summary.md`.

### 2. `jni/companion.cpp` — processo companion (root)

Daemon root separado (`REGISTER_ZYGISK_COMPANION`), spawnado pelo
`zygiskd64` — sobrevive via double-fork. Expõe um socket Unix abstract
(`@bc_companion`) autenticado por **`SO_PEERCRED`** (kernel, não-forjável):
aceita root (UID 0), shell/adb (UID 2000) e o UID real do Termux, rejeita
qualquer outro processo — fail-closed por padrão. Ver
`COMPANION_TERMUX_ARCHITECTURE.md`.

### 3. Clientes de controle

- **`termux_client.py`** — cliente mínimo, roda dentro do Termux, conecta
  direto no socket abstract.
- **`bc_log_viewer.py`** — cliente para uso **fora** do device, via
  `adb forward tcp:PORT localabstract:bc_companion` (nenhuma porta TCP fica
  aberta no device). Trata timeout, forward morto e rejeição de UID com
  mensagens claras em vez de travar ou devolver traceback.

## Build

Requer o NDK (usei o bundle do próprio Magisk, mas qualquer NDK recente
com `clang`/`libc++` serve):

```bash
export ANDROID_NDK_HOME=/caminho/pro/ndk
"$ANDROID_NDK_HOME/ndk-build" NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=jni/Android.mk -B
```

Saída: `libs/arm64-v8a/libbc-poc.so`.

### Nota crítica de linkagem (não reverter)

`jni/Application.mk` **não** linka `-landroid`. Em builds recentes de
HyperOS/MIUI, adicionar essa lib puxa uma cadeia NEEDED transitiva quebrada
(`libandroid.so → libharfbuzz_ng.so → libicu.so`, `libicu.so` só existe na
APEX i18n, fora do namespace padrão do `zygiskd64`) — o `.so` do app carrega
normal (zygote tem acesso à APEX), mas o `dlopen()` **dentro do companion**
falha silenciosamente. `-Wl,--as-needed` garante que nenhuma NEEDED morta
volte a entrar. NEEDED final esperado: `liblog`, `libc`, `libdl`.

## Empacotamento (módulo Magisk)

```bash
# estrutura exigida pelo Zygisk:
#   module.prop
#   zygisk/arm64-v8a.so   ← nome fixo por ABI, não é o nome do seu .so
zip -r seu-modulo.zip module.prop zygisk/arm64-v8a.so
```

Instalar via Magisk (Módulos → Instalar do armazenamento) e **reiniciar** —
módulos Zygisk só materializam/carregam depois de um reboot real, mesmo
scripts simples de `post-fs-data.sh` não rodam sem reiniciar primeiro.

## Uso

```bash
# dentro do Termux, direto no socket abstract:
python3 termux_client.py ping

# fora do device (PC), via adb forward:
adb forward tcp:17654 localabstract:bc_companion
python3 bc_log_viewer.py --host 127.0.0.1 --port 17654 ping
python3 bc_log_viewer.py --host 127.0.0.1 --port 17654 status
python3 bc_log_viewer.py --host 127.0.0.1 --port 17654 list_patches
```

## Testado ao vivo

Device físico rooted (Magisk), Android 16/HyperOS. 4/4 hooks ativos em
gameplay real, zero crash/ANR. Bateria de 40 testes unitários do hook
lifecycle (`test/selftest_harness.cpp`, 0 falhas na última execução)
cobrindo patch/unpatch/repatch, idempotência, race entre clientes
concorrentes, e stress test de 50 ciclos unpatch/repatch no mesmo hook.

Ponte de controle confirmada de ponta a ponta via `adb forward`:

```
ping         → pong
status       → companion_active
list_patches → appInit|on|active|1
               appUpdateDraw|on|active|1464
               appTouch|on|active|0
               appKey|on|active|0
```

2 bugs reais encontrados e corrigidos durante o desenvolvimento (não
hipotéticos — reproduzidos ao vivo antes do fix):

- **Semântica invertida de close no accept loop**: o handler de
  `list_patches` virou thread-per-client pra não bloquear o loop de accept;
  o retorno de "adotado" estava invertido, fechando o fd do cliente antes
  da thread terminar de escrever a resposta.
- **Race em property compartilhada entre clientes concorrentes**: 2
  chamadas simultâneas de `list_patches` competiam pela mesma Android
  property (só suporta 1 pedido em voo); resolvido com mutex serializando
  só o ciclo sinaliza→espera→lê, sem travar o resto do accept loop.

## Ver também

- `RESILIENCE_ANALYSIS.md` — análise de resiliência a updates do APK alvo.
- `INTEGRATION_CHECK.md` — verificação de integração build main+companion.
- `COMPANION_TERMUX_ARCHITECTURE.md` — arquitetura completa da ponte
  companion↔Termux, incluindo o modelo de autenticação por `SO_PEERCRED`.
- `context/` — notas técnicas de hardening, comportamento de config, e
  comparação de design contra o BepInEx (o que ele resolve que este projeto
  ainda não precisa, e vice-versa).

## Créditos

- [Dobby](https://github.com/jmpews/Dobby) (jmpews) — inline hook ARM64.
  Ver [NOTICE.md](NOTICE.md).
- [Zygisk](https://github.com/topjohnwu/Magisk) (topjohnwu/Magisk) —
  mecanismo de injeção em todo processo Zygote-forked.

## Licença

MIT — ver [LICENSE](LICENSE). O binário redistribuído (`libdobby.a`) mantém
a licença MIT original do projeto Dobby — ver [NOTICE.md](NOTICE.md).
