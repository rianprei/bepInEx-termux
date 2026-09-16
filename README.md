# zygisk-bc-poc — POC Battle Cats (Zygisk + Dobby + Companion Termux)

Módulo Zygisk (Magisk) que hooka os exports JNI do Battle Cats
(`jp.co.ponos.battlecatsen`, Cocos2d-x) via **Dobby**, log-only — não altera o
jogo. Prova a cadeia completa de injeção/instrumentação nativa em runtime.

**Estado real (2026-09-13): ✅ FUNCIONANDO NO DEVICE FÍSICO** — 4/4 hooks ativos,
zero crash, e a ponte Termux (`ping`/`pong`) verificada ponta a ponta.

---

## Componentes

### 1. `jni/main.cpp` — módulo Zygisk (API v5)

Roda dentro do processo do app. Detecta o pacote, sobe uma thread que faz
polling em `dl_iterate_phdr` até `libnative-lib.so` aparecer, então instala hooks
inline via Dobby nos exports JNI da `MyActivity`.

**Símbolos interceptados** (via `readelf --dyn-symbols` do `.so` v15.5.0 en):
`appInit`, `appUpdateDraw`, `appTouch`, `appKey`.

**Resolução de alvo (cascata, sobrevive a update do APK):**
1. RVA fixo do offset DB (`offsetsdb.h`) — exige build-id confirmado.
2. Símbolo JNI export (`DobbySymbolResolver`).
3. Scan por assinatura de prólogo (match única obrigatória).

**Fail-safe:** qualquer falha → hook vai DORMANT (log-only), nunca crasha o
processo do jogo. Ver `context/zygisk-fallback-design.md` e
`context/bc-poc-hardening-summary.md` pra lista completa dos patterns.

### 2. `jni/companion.cpp` — companion process (root)

Daemon root separado (`REGISTER_ZYGISK_COMPANION`), spawnado pelo `zygiskd64`.
Expõe socket Unix abstract `@bc_companion` pro Termux com autenticação
`SO_PEERCRED` (fail-closed). Ver `COMPANION_TERMUX_ARCHITECTURE.md` e
`zygisk-companion-research.md`.

### 3. `termux_client.py` — cliente de teste

Conecta ao `@bc_companion` e envia `ping`/`status`. Confirmado ponta a ponta
(`ping` → `pong`).

### 4. `termux-console/bepin-console` — console ao vivo no Termux

Equivalente ao console que o BepInEx abre no Windows — ao invés de o
usuário abrir manualmente, o companion dispara isso sozinho via
`launch_termux_console()` (companion.cpp), 1x por sessão do jogo, usando
`RUN_COMMAND` do Termux (`RunCommandService`, pacote `termux-app`).

**Requisito obrigatório**: `allow-external-apps=true` em
`~/.termux/termux.properties` (criar se não existir, `termux-reload-settings`
depois) — sem isso o `RunCommandService` recusa silenciosamente e o console
não abre (o companion segue funcionando normal, só não dispara o Termux).

`bepin-console` roda o stream de log em background + lê comando digitado no
foreground (REPL) — o BepInEx no PC não tem input nenhum no console
(confirmado no código-fonte: `ConsoleManager.cs`/`WindowsConsoleDriver.cs`
não têm nenhum `Read`/`ReadLine`), então isso é além da paridade, não invenção.

Log persistido em disco em `/data/local/tmp/bc_poc_LogOutput.log`
(equivalente ao `LogOutput.log` do BepInEx — trunca a cada boot por
padrão, mesmo comportamento confirmado em `DiskLogListener.cs`). Log
nativo do próprio jogo (não só do módulo) é unificado no mesmo arquivo via
bridge de `logcat` — ver `ROADMAP.md` Fase 3.

---

## Build

```bash
export ANDROID_NDK_HOME=~/Android/Sdk/ndk/magisk
cd ~/battlecats-mods/zygisk-bc-poc
~/Android/Sdk/ndk/magisk/ndk-build NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=jni/Android.mk -B
```

NDK Magisk em `~/Android/Sdk/ndk/magisk/` (arm64-v8a). Saída:
`libs/arm64-v8a/libbc-poc.so`.

### Nota crítica de linkagem (não reverter)

`Application.mk` **não** usa `-landroid`. Adicioná-lo faz o `.so` ganhar uma
NEEDED transitiva quebrada nesta ROM (`libandroid.so` → `libharfbuzz_ng.so` →
`libicu.so`, ausente do namespace default do `zygiskd64`), derrubando o
`dlopen()` do módulo especificamente dentro do daemon companion (o app carrega
OK porque o zygote tem acesso à APEX i18n). `-Wl,--as-needed` garante que
nenhuma NEEDED morta volte. NEEDED final: `liblog`, `libc`, `libdl`.

---

## Empacotamento (módulo Magisk)

```bash
# estrutura correta do zip:
#   module.prop
#   zygisk/arm64-v8a.so   (não "libzygisk-bc-poc.so" — nome é fixo por ABI)
zip -r bc-poc.zip module.prop zygisk/arm64-v8a.so
```

Instalar via Magisk (Modules → Install from storage) e reboot.

---

## Estado real do device (POCO C75, WG7TPZOBHQGIMZZ5)

- **Hooks**: 4/4 ativos em gameplay real — `appInit` (1x), `appUpdateDraw` (~17k
  hits), `appTouch`, `appKey` — zero crash/ANR. Logs na tag `BCPOC`.
- **Ponte Termux**: `python3 termux_client.py ping` → `pong`, UID real do Termux
  aceito via `SO_PEERCRED`, socket `@bc_companion` correto.
- **Companion**: daemon sobrevivendo via double-fork.

---

## Ver também

- `HANDOVER.md` / `~/HANDOVER-bc-poc.md` — log completo da investigação (rounds 1-8).
- `RESILIENCE_ANALYSIS.md` — análise de resiliência a updates de APK.
- `INTEGRATION_CHECK.md` — verificação de build/integração main+companion.
- `COMPANION_TERMUX_ARCHITECTURE.md` — arquitetura da ponte companion↔Termux.
- `termux_client.py` — cliente de teste.