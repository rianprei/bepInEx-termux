Ver também: [ROADMAP-COMPETITORS.md](ROADMAP-COMPETITORS.md) — pesquisa de concorrência (fechada, 4/4).

# bepin-termux — Termux-centrico: Roadmap + TODOs

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
aceita input nenhum, bepin-termux pode ir **além** da paridade: REPL na
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
- [ ] **2.3** Requisito documentado no README: `allow-external-apps=true`
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
- [ ] **3.5** Requisito documentado: `allow-external-apps=true` em
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
