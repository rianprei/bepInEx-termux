# mechabun — mod real pro bepinEx-termux

Primeiro mod de gameplay de verdade rodando no loader (não é mais throttle
de frame nem exemplo — mexe em stat de unidade real). Aplica o design
"ideal comunitário" do Mecha-Bun (#426) documentado em
`battlecats-mecha-bun-ideal-comunidade.md`.

## Como funciona

Engenharia reversa real (Ghidra 12.1 headless + radare2, `libnative-lib.so`
JP 15.6.0, build-id `b94cc0dafd8521f1f7cfcf3841a29f13d7cd1ef3`):

- `unit%03d.csv` é o nome real do arquivo por unidade (`unit426.csv` pro
  Mecha-Bun, unitId zero-based = 425).
- Função que carrega esse CSV (assinatura AOB de 48 bytes, verificada
  única no binário inteiro) popula uma struct em
  `bigData + unitId*0x760 + 0x9e568`, até 4 formas, stride `0x1d8` (472
  bytes) cada, 118 campos int32 por forma (offset = coluna_csv × 4) —
  layout verificado via disassembly direta.
- **Identidade das colunas corrigida numa segunda passada**: a v1 deste
  mod usava colunas erradas (6/7/8 pra HP, 9/10/11 pra ATK), herdadas de
  um doc do vault (`battlecats-mecha-bun-forense.md`) que continha erro
  de transcrição próprio (a tabela de colunas do doc não batia com o CSV
  bruto que o mesmo doc citava — achado ao cross-referenciar). Corrigido
  usando fonte autoritativa: **tbcml** (lib de modding Battle Cats ativa,
  mantida, instalada em `~/.venvs/battlecats-mod/`),
  `core/game_data/cat_base/cats.py:329-395` (`Stats.assign`), que é
  código executável testado por terceiros, não transcrição manual.
- Hook via `bc_mod_api.h` v3 (`install_hook`, capacidade nova adicionada
  no loader por este mod — DobbyHook cru pra alvo achado por
  `resolve_pattern`, fora dos 4 hooks nomeados fixos): pós-execução do
  original, se `unitId==425`, ajusta os campos abaixo nas 4 formas.

## Índices reais (fonte: tbcml, não mais o doc com erro)

| Campo | Índice | Ação |
|---|---|---|
| HP | 0 | ×1.8 (+80%) |
| ATK | 3 | ×1.8 (+80%) |
| Speed | 2 | não mexido (design mantém) |
| Attack Interval | 4 | ×26/32 (32f→26f) |
| Range | 5 | ×250/190 (190→250) |
| Recharge | 7 | ×2136/2536 (-400f) |
| Wave Immunity | 46 | seta 1 |
| Knockback Immunity | 48 | seta 1 |
| Surge Immunity | 91 | seta 1 |
| Behemoth Slayer | 105 | seta 1 |

## Escopo honesto

**Alta confiança** (matemática comprovada, independente de fórmula
desconhecida de curva): HP e ATK — multiplicar o raw por 1.8 propaga
+80% pro stat final calculado pelo jogo, seja qual for a curva de
`unitlevel.csv` aplicada depois (curva multiplicativa preserva
proporção). Imunidades e Behemoth Slayer — bool flags diretos (`tbcml`
`unit_bool()` = `bool(value)`, 0=false/qualquer-não-zero=true), sem
ambiguidade.

**Suposição rotulada, não fabricada como certeza**: Range, Recharge,
Attack Interval — a fórmula raw→exibido pra esses campos especificamente
NÃO foi confirmada via rastreamento dinâmico (precisaria Frida no device
rodando o jogo de verdade). Assume-se linear/multiplicativa (razoável,
mas não provado). Se estiver errado, o pior caso é o Mecha-Bun ter um
desses stats "estranho" no jogo — não crasha nem corrompe memória (são
campos int32 isolados, sem ponteiro nem tamanho envolvido).

**Não feito, confirmado limitação real (não preguiça)**:
- Backswing — NÃO existe como campo CSV separado no schema real do
  tbcml (só existe "foreswing", índice 13). O "backswing" que a
  comunidade pede vem de timeline de animação (mamodel/maanim), fora do
  escopo de um patch de memória de stats.
- Warp Immunity (D14 do design ideal) — sem campo correspondente no
  schema de stats do tbcml pra este slot — não fabricado.

## Build

```
ndk-build -B -j4 -C mods/mechabun
```

Produz `mods/mechabun/libs/arm64-v8a/libmechabun.so`. Deploy via
`push_mod` do companion (protocolo já existente do loader) pra
`/data/local/tmp/bc_mods/`.

## Teste

`test_buff_math.cpp` — teste host-only das 3 fórmulas de escala inteira
(HP/ATK, Range, Recharge/Frequency), roda com
`g++ -fsanitize=address,undefined`. O resto do mod (hook install,
resolve_pattern, escrita de memória real) só roda no device — **não
testado em device real ainda**, só build + assinatura AOB validada
offline. Testar no device exige `push_mod` + reboot (ação real em
device), não feito sem autorização explícita do usuário.
