Ver também: [ROADMAP-COMPETITORS.md](ROADMAP-COMPETITORS.md) — pesquisa de concorrência (fechada, 4/4).

# bepInEx-termux — Termux-centrico: Roadmap (fechado, 11/11)

**Escopo reduzido (2026-09-16):** watchdog Termux:Boot, atalhos Termux:Widget,
notificação por Termux:API e CLI `bepin` unificado foram descartados a
pedido do usuário — não fazem parte do objetivo real. O objetivo é só:

1. **`push_mod`** — deploy de `.so` via socket, sem `su` (já implementado, `af898ca`).
2. **Console ao vivo** — Termux abre automaticamente com o stream de log
   quando o jogo inicia, replicando a janela de console que o BepInEx abre
   no Windows.

## Fontes pesquisadas (confirmadas, não inventadas — 4 agentes, código-fonte real)

- **Console BepInEx (PC) é ONE-WAY** — confirmado por 2 agentes independentes
  (hermes + OpenCode) lendo `ConsoleManager.cs`/`WindowsConsoleDriver.cs`/
  `IConsoleDriver.cs` no fonte real (github.com/BepInEx/BepInEx): zero
  `Read`/`ReadLine`/`ReadKey`/`Console.In` em toda a classe. Só emite log,
  nunca aceita comando digitado de volta.
- **Ciclo de vida do console BepInEx** (kilo, `ConsoleManager.cs`/
  `ConsoleWindow.cs`, permalinks no fonte): abre no `Preloader.cs:39` via
  `AllocConsole`, acompanha o processo do jogo desde o início; fecha
  implicitamente com o processo (sem `DetachConsole` explícito no shutdown).
  `PreventClose` só remove o botão X da UI, não impede fechamento por
  término de processo — não existe opção de manter aberto depois do jogo.
- **Termux RUN_COMMAND é do termux-app, não termux-api** (correção real,
  freebuff, clone de `termux/termux-app` + wiki oficial
  `RUN_COMMAND-Intent.md`): `RunCommandService.java` em
  `app/src/main/java/com/termux/app/`. `RUN_COMMAND_BACKGROUND=false` →
  `Runner.TERMINAL_SESSION` (linhas 82-85) → `TermuxService` →
  `TermuxSession.execute` → `new TerminalSession(...)` — **sessão real,
  interativa**, não read-only.
- **push_mod** — já implementado (`af898ca`, completo em `9c7353f`):
  protocolo `push_mod <nome> <tamanho>` via socket no companion.cpp, sem
  `su` (UID Termux aceito por `is_authorized_uid`), carrega o `.so` na hora
  via `load_dynamic_mods()`.

## Decisão de design (com base na pesquisa)

Como a sessão RUN_COMMAND é terminal real interativo e o BepInEx no PC não
aceita input nenhum, bepInEx-termux pode ir **além** da paridade: REPL na
mesma janela do stream (usuário digita `toggle_mod`/`set_mod`/etc enquanto
vê o log ao vivo) — vantagem real sobre o PC, não invenção. Sem comando
novo no companion: cada linha digitada abre uma conexão request/response
comum via `termux_client.py` (já testado), o stream roda em paralelo em
background na mesma sessão.

---

## Fase 1 — P0: fundamento
- [x] `push_mod <nome> <tamanho>` no companion (socket binário, sem su).
- [x] push_mod carrega o `.so` na hora, sem reiniciar o jogo (`9c7353f`).

## Fase 2 — console ao vivo + REPL (objetivo atual)
- [x] **2.1** `launch_termux_console()` no companion (`companion_handler`,
      1x por spawn = 1x por sessão do jogo): `am start` abre o Termux, depois
      `am startservice` no `RunCommandService` roda `termux-console/bepin-console`
      em foreground (`RUN_COMMAND_BACKGROUND=false`). Fire-and-forget — se
      Termux/RUN_COMMAND não estiver disponível, loga warning e segue normal.
- [x] **2.2** `bepin-console`: stream de log em background (`&`) + loop
      lendo stdin em foreground, cada linha digitada vira comando pro
      companion via `termux_client.py` — REPL na mesma janela do stream.
- [x] **2.3** Requisito documentado no README: `allow-external-apps=true`
      em `~/.termux/termux.properties`, senão o RunCommandService recusa
      silenciosamente.

## Fase 3 — paridade total com o log/console do BepInEx (`literalmente tudo`)
- [x] **3.1** Log em disco (`ab72ce2`) — equivalente ao `LogOutput.log`.
      Confirmado no fonte real (`DiskLogListener.cs`): `appendLog=false` é
      DEFAULT (trunca a cada boot, não Append), fallback de até 5 arquivos
      (`LogOutput.N.log`) se travado. Implementado igual: `fopen("w")` na
      1ª escrita do processo, fallback com sufixo numérico.
- [x] **3.2** Filtro de nível Debug (`1bb3866`) — confirmado no fonte real
      (freebuff, BepInEx v5.4.23.5): default de `[Logging.Console]` e
      `[Logging.Disk]` `LogLevels` é `Fatal|Error|Message|Info|Warning`,
      SEM Debug. `stream_send_prefixed` descarta nível Debug por padrão.
- [x] **3.3** Bridge de logcat (`1bb3866`) — unifica o log NATIVO do
      próprio jogo (não só do módulo) no mesmo canal stream+disco.
      Achado real (OpenCode): BepInEx tem `UnityLogSource`, gancho em
      `Application.logMessageReceived` (evento Unity) pra capturar
      `Debug.Log` do PRÓPRIO jogo. Battle Cats não é Unity (engine própria
      PONOS/Cocos2d-x-like) — não existe esse evento gerenciado. Mas o
      jogo usa `__android_log_print` nativo como qualquer app, já vai pro
      logcat — só não tava unificado. `logcat -v brief --pid=<próprio>`
      resolve, filtrando a própria tag pra não duplicar.
- [x] **3.4** Confirmado NÃO aplicável (kilo): formatação especial de
      stack trace de exceção (`ex.ToString()`, `LogLevel.Fatal` forçando
      console a abrir mesmo desabilitado) — não mapeia pra código nativo
      C/C++ sem exceções gerenciadas. Sem ação.
- [x] **3.5** Requisito documentado: `allow-external-apps=true` em
      `~/.termux/termux.properties`, senão o RunCommandService recusa
      silenciosamente.
- [x] **3.6** enum `LogLevel` confirmado (hermes, `LogLevel.cs:5`,
      `[Flags]`): `None=0, Fatal=1, Error=2, Warning=4, Message=8, Info=16,
      Debug=32, All=63`. Cores (`GetConsoleColor`, mesmo arquivo):
      Fatal=Red, Error=DarkRed, Warning=Yellow, Message=White,
      Info/Debug=DarkGray — bate com o mapeamento ANSI já usado em
      `termux_client.py` (extraído de `TtyHandler.cs` em sessão anterior).
      Fatal/Debug não usados no nosso lado, sem lacuna: o filtro padrão do
      próprio BepInEx já exclui Debug do console, e Fatal só serve pra
      erro irreversível — não é comportamento que falte replicar.

## Status: Fase 3 fechada, 5/5 agentes despachados
Todos os 5 agentes maestri (hermes, freebuff, OpenCode, kilo, devin)
foram despachados pra fechar a paridade console/log com o BepInEx. Devin
bloqueado por cota semanal (`Quota exhausted`, confirmado novamente nesta
rodada) — item 3.7 (overlay in-game) fica como NÃO COMPROVADO, não como
lacuna assumida.

## O que NÃO é lacuna real (verificado, não suposição)
- **Captura de logs internos do Unity** — não aplicável, Battle Cats não é
  engine Unity (achado OpenCode). `WriteUnityLog` do BepInEx é `false` por
  padrão mesmo no PC — logs do jogo já não vêm por padrão nem lá.
- **Overlay de log dentro do jogo (IMGUI in-game)** — não pesquisado
  (devin bloqueado por quota semanal esgotada, confirmado 2x nesta sessão;
  sem esse dado, não afirmo se existe ou não no BepInEx).

## Dependências manuais não-automatizáveis (documentadas)
- `allow-external-apps=true` precisa estar setado manualmente 1x no Termux.
- `pkg install termux-api` + app Termux:API instalado (mesma fonte/assinatura
  do Termux — F-Droid↔F-Droid ou GitHub↔GitHub).

## Fase 4: generalização pra qualquer jogo C++/JNI nativo (2026-09-16/17)

Pedido do usuário: sair de "só Battle Cats hardcoded" pra "se adaptar a
qualquer jogo Cocos2d-x, e a qualquer jogo C++/JNI nativo, não importa
o motor" — mantendo o caminho Battle-Cats-específico intacto (não
reescrito, só uma segunda porta de entrada ao lado dele).

### O que foi construído
- **`jni/bc_elf_symtab.h`** — enumeração de símbolo `.dynsym` via
  `dl_iterate_phdr` + parsing manual de `DT_GNU_HASH` (mesmo mecanismo do
  Frida, validado contra `gumelfmodule.c` real). Sem `dlopen` — lib já
  está mapeada no processo.
- **`jni/bc_engine_detect.h`** — cascata de 3 sinais Cocos2d-x (lib do
  motor / símbolo `cocos2d::` / JNI stock `Cocos2dxRenderer`), com
  fallback `BC_ENGINE_GENERIC_NATIVE` pra qualquer app que exporte
  `Java_*` mesmo sem motor reconhecido — cobre "qualquer jogo C++", não
  só Cocos2d-x.
- **`jni/bc_generic_allowlist.h`** — allowlist de pacote
  (`/data/local/tmp/bc_generic_allowlist.conf`). Detecção genérica só
  escaneia/atua em pacote explicitamente listado — escanear TODO app do
  device custaria latência de boot em apps que não interessam.
- **`jni/bc_generic_hook.h`** — hook de LOG via `DobbyInstrument` (não
  `DobbyHook`) em até 8 símbolos `Java_*` descobertos. `DobbyInstrument`
  não reconstrói a chamada original — sem risco de ABI por assinatura
  desconhecida, diferente do caminho Battle-Cats-específico que usa
  `DobbyHook` com replacement de assinatura fixa e conhecida.

### Limite honesto (não escondido, documentado no código)
Apps que registram tudo via `RegisterNatives()` em `JNI_OnLoad()` não têm
símbolo `Java_*` exportado — indetectável por qualquer sinal aqui, sem
workaround sem hookar `RegisterNatives`/`JNI_OnLoad` (fora de escopo).
"Sem erros" = nunca crasha (fail-safe DORMANT), não = "sempre acha algo
pra hookar".

### Bugs reais achados por revisão e corrigidos
Duas rodadas de revisão real (não retórica) rodaram contra esse código:

1. **freebuff** (`60df375`) — 2 bugs P0/P1:
   - Bias de ASLR (`dlpi_addr`) nunca somado no endereço do símbolo —
     `DobbyInstrument` armava em offset cru, memória arbitrária/não
     mapeada, não no símbolo real. Testado ao vivo: scan em zygote64
     confirmou 0 símbolos Java_* nesse estágio, provando o segundo bug.
   - Detecção rodando em `preAppSpecialize`, antes do processo ser
     especializado — nenhuma lib do app mapeada ainda, cascata nunca
     detectava nada em nenhum app. Movida pra `postAppSpecialize` com
     poll+timeout (`bc_wait_engine_detect`, 8s/200ms), mesmo padrão do
     `wait_lib_loaded` do caminho Battle Cats, adaptado pra não saber o
     nome da lib de antemão.

2. **hermes** — revisão completa do projeto pós-fix, sem P0 novo, achados
   reais adicionais corrigidos:
   - `BC_MOD_API_VERSION` ainda em `1` apesar do campo `resolve_pattern`
     (adicionado em rodada anterior) exigir `version>=2` pro mod checar
     com segurança — bump pra `2`.
   - `bc_gnu_hash_symcount`: `idx - symoffset` sem checar `idx >=
     symoffset` — lib hostil/malformada (alcançável agora: caminho
     genérico escaneia QUALQUER lib carregada, não só as próprias) causa
     underflow em `uint32_t` e leitura fora dos limites de `chain`. Fix:
     aborta a cadeia se `idx < symoffset`.
   - `connectCompanion()` só era chamado no caminho `be_bc` —
     `publish_log()` no hook genérico nunca tinha `g_stream_fd` setado,
     log genérico nunca chegava no Termux (só disco/logcat). Fix: mesma
     chamada também no branch `be_generic_candidate`.
   - README dizia "61 testes", real são 55 casos (228 assertions) —
     corrigido.

### Validação ao vivo no device (2026-09-17, pós-`f9714a3`)
Não havia jogo Cocos2d-x real instalado pra testar a allowlist — em vez
de deixar isso sem prova, construído do zero um APK de teste mínimo
(`com.bepintest.fakegame`, package próprio, `.so` compilado com o mesmo
NDK do projeto, exporta só `Java_com_bepintest_fakegame_MainActivity_nativeInit`,
sem nada Cocos2d-x) e testado contra o módulo real no device físico.

Log real capturado:
```
com.bepintest.fakegame na allowlist — detecção de engine adiada pra postAppSpecialize
com.bepintest.fakegame: engine nativo detectado (sinal=4) — instalando hook de log
com.bepintest.fakegame: 8 hook(s) de log instalado(s)
```
`sinal=4` = `BC_ENGINE_GENERIC_NATIVE` (fallback, não Cocos2d-x) — confirma
que o caminho genérico funciona pra QUALQUER app C++/JNI, não só pra
Battle Cats/Cocos2d-x. Battle Cats testado no mesmo boot, 4/4 hooks
intactos — generalização não quebrou o caminho específico.

**Achados operacionais reais desse teste:**
- Módulo atualizado (`.so` trocado manualmente em `/data/adb/modules/`) só
  recarrega com **reboot completo** — `killall zygote64` sozinho não
  reinicia `zygiskd`/`magiskd` de verdade (uptime do device não mudou
  depois do kill). Não documentado antes, causava resultado "módulo não
  injeta" enganoso achando que era bug de código.
- **Corrida real**: chamada JNI única logo após `System.loadLibrary()`
  (padrão comum de app real) pode executar ANTES do poll genérico
  instalar o hook — `DobbyInstrument` só intercepta chamada futura a
  partir da instalação, não retroage. Mitigado (não eliminado) reduzindo
  o intervalo de poll de 200ms pra 50ms em `generic_event_thread`
  (`main.cpp`) — reduz a janela, não garante pegar toda chamada única no
  mesmo instante do `loadLibrary`. Fix de verdade exigiria hookar
  `JNI_OnLoad`/`dlopen`, fora de escopo. Chamada repetida (loop de
  render por frame, caso comum em jogo real) não tem esse problema —
  eventualmente cai dentro da janela de poll.

### O que NÃO foi feito (limite real, não escondido)
- Nunca testado contra jogo COMERCIAL Cocos2d-x real (só o APK de teste
  mínimo acima) — prova a mecânica funciona, não prova comportamento com
  motor Cocos2d-x de verdade rodando (mais libs carregadas, mais ruído
  de símbolo, timing de carga diferente).
- `load_dynamic_mods()` (loader de mod `.so` dinâmico via `push_mod`) não
  roda no caminho genérico — mods atuais assumem `TARGET_LIB` fixo
  (Battle Cats), não são engine-agnósticos. Decisão consciente de não
  wirar ainda, não bug.
