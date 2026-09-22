# Changelog — bepInEx-termux

Formato: `Added / Changed / Fixed / Known issues` por release.
Primeira release pública: `v0.3.0` (casa com `BC_LOADER_VERSION` em
`jni/main.cpp` e com o `módulo carregado — v0.3.0` visto ao vivo).

## v0.3.0 — 2026-09-21

### Added
- **Log fiel ao BepInEx real**: cada mod dinâmico loga com seu próprio
  nome como fonte (equivale ao `ManualLogSource` por plugin), extraído da
  convenção `"[nome] mensagem"`; banner inicial com source `"Preloader"`
  (ground-truth checado no fonte do BepInEx — `"Chainloader"` nunca existiu
  como Source); layout `[Nível,-7:Fonte,10]` idêntico ao
  `LogEventArgs.ToString()`; filtro default sem `Debug` (mesmos defaults de
  `[Logging.Console]`/`[Logging.Disk]`). Desvio deliberado documentado:
  timestamp `HH:MM:SS` na frente (BepInEx não mostra hora em nenhum sink).
- **Mutex no log em disco** (`pthread_mutex_t`, sem `<mutex>` pra não inchar
  o `.so`): par `fwrite`+`fflush` atômico entre threads do jogo.
- **Buffers de log aumentados** (`line` 224→448, `body` 160→384): mensagens
  reais de erro (ex. mismatch de `STAT_BLOCK_OFF`) passavam de 300 chars e
  eram truncadas — confirmado no capture ao vivo.
- **Mod Mecha-Bun (#426, prova de conceito da infra — não mod pra jogar)**:
  veículo de teste de ponta a ponta (hooks, log, deploy, hot-reload) com
  conteúdo realista: hook de stats via loader CSV (HP/ATK/range/
  recarga/imunidades/wave/strengthen/dodge/slayers/traits, até 3 formas),
  hook de dano D2-fix (getter `0x8789e8` → campo `+0x83774`, mesma escala
  9/5 do ATK), redirect de ícones D16.1 e de pack de animação D12 via hook
  de `fopen` (com fallback pro original + checagem de tamanho do pack),
  retry de hook no hot-reload, e `verify_unit_base` (assinatura sanity
  contra `unit427.csv` real antes de escrever — mismatch vira stats
  vanilla, nunca corrupção de memória).
- **Infra de deploy do mod**: `mods/mechabun/deploy.sh` robusto
  (`mktemp`+`adb push`+`su`, sem `run-as` frágil; path do pack original
  overridable via env `PACK_SRC_ORIGINAL`),
  `mods/mechabun/tools/d12_transform.py` (gera o pack D12 sob demanda a
  partir do pack original — requer `pycryptodome` no host; dir base
  overridable via env `BCDATA_DIR`), `mods/mechabun/PLAYTEST_LEARNINGS.md`
  (log de sessão real em device físico).
- **Termux**: `termux-boot/bepin-watchdog.sh` (monitora socket do companion)
  e `termux-shortcuts/tasks/bepin-alert.sh` (notificação só em
  Warning/Error/Fatal). Paths do repo overridables via `BEPIN_TERMUX_DIR`.

### Changed
- `STAT_BLOCK_OFF` 0x9e568 → **0x9e318** (RE direta no `.so` do device;
  update do jogo moveu o offset — ver comentário no código).
- Strengthen mult 150 → **50** (campo é bônus-percentual, cf. tbcml
  `Strengthen.multiplier_percent` + design "+50%"; 150 over-buffava 2.5x).
- Trails de traits D16 aplicados nas 3 formas (não só True Form).
- Comentário de convenção de mask AOB em `bc_pattern_scan.h` (0/não-zero,
  não `"xx??xx"`).

### Fixed
- **Race TOCTOU em `bc_mod_register`** (crash real `SEGV_ACCERR` em device):
  load boot × sinal de hot-reload viam `g_orig==nullptr` juntos e chamavam
  `install_hook` concorrente no mesmo alvo (Dobby sem lock + `mprotect`
  RWX). Fix: mutex cobrindo a função inteira + atomics nos contadores.
- Crash com `api->log(level, nullptr)` (guard de nulo).
- `COL_ATTACK_INTERVAL` documentado como no-op pro Mecha-Bun (raw 0) em vez
  de alegar efeito; D12 v1 apontava pra unit errada (425 → 426 correto).

### Known issues (honestos, não features)
- **Dano real do Mecha-Bun em batalha NÃO confirmado**: o hook D2-fix
  instala e dispara normalmente (confirmado no log), mas o valor lido do
  getter de dano-base no momento do hit é sempre `orig=0` nos testes ao
  vivo — não é que o hook falhe em disparar, é que a fonte de dado que ele
  lê retorna 0 nesse ponto, então não há prova de que o dano real em
  combate mude. Campos ATK/RANGE/RECHARGE +
  traits (relic/behemoth/sage/mini-wave/toxic) do struct **não têm leitor
  no jogo** (busca exaustiva de xrefs) — escrever certo ≠ funcionar. Ver
  `context/battlecats-mechabun-atk-dead-struct.md` e tabela "Leitor
  confirmado" no README do mod.
- **`STAT_BLOCK_OFF` deriva por update do jogo**: se mover de novo, o mod
  detecta via `verify_unit_base` e fica vanilla nesse boot (sem crash) —
  mas o buff não aplica até re-REV.
- **D12 opcional**: sem o pack no device, o hook usa o original (sem ciclo
  32f→26f); ícones idem sem os PNGs.
- **Ultra Form não é forma nova de verdade**: tier de stats na True Form
  (jogo só tem 3 formas pra essa unit); backswing/Ultra real e talents
  oficiais fora de escopo (documentado no README do mod).
