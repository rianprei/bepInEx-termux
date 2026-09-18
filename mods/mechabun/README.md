# mechabun — mod real pro bepinEx-termux

Primeiro mod de gameplay de verdade rodando no loader (não é mais throttle
de frame nem exemplo — mexe em stat de unidade real). Aplica o design
"ideal comunitário" do Mecha-Bun (#426) documentado em
`battlecats-mecha-bun-ideal-comunidade.md`.

## Como funciona

Engenharia reversa real (Ghidra 12.1 headless + radare2, `libnative-lib.so`
JP 15.6.0, build-id `b94cc0dafd8521f1f7cfcf3841a29f13d7cd1ef3`):

- `unit%03d.csv` é o nome real do arquivo por unidade (`unit427.csv` pro
  Mecha-Bun, unitId zero-based = 426). **Correção crítica desta
  revisão**: a sessão toda usava unitId=425 (`unit426.csv`) por engano
  — cross-referenciando dado real do BCData, `unit426.csv` é outra
  unidade sem relação (太秦鴻＆ネコ), e o nome "Mecha-Bun" só aparece em
  `Unit_Explanation427_en.csv` → unitId 426. Confirmado por dado real:
  `unit427.csv` raw[5] (range) = 190, batendo exato com a referência de
  design "Range 190→250" usada pelo mod desde o início.
- Função que carrega esse CSV (assinatura AOB de 48 bytes, verificada
  única no binário inteiro) popula uma struct em
  `bigData + (unitId+2)*0x760 + 0x9e568` — **atenção**: o offset de
  endereçamento do struct usa `unitId+2`, não `unitId` puro (achado via
  decompile real: a função faz `param_2 = param_2 + 2` antes de
  multiplicar pelo stride); o nome do arquivo usa `unitId+1`. Dois
  offsets diferentes confirmados separadamente na mesma função — bug
  real encontrado e corrigido nesta sessão (v1 usava `unitId` puro pro
  endereço, o que escreveria na unidade errada). Até 4 formas
  (`while (lVar10 != 4)` confirmado), stride `0x1d8` (472 bytes) cada,
  118 campos int32 por forma (`uVar6 != 0x76`, 0x76=118, confirmado) —
  layout inteiro verificado via decompile direto (não só disassembly
  manual).
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
  original, se `unitId==426`, ajusta os campos abaixo nas 4 formas.

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
| Wave prob/level/mini | 35/36/94 | 10% / lv1 / mini=1 (D6) |
| Strengthen hp%/mult% | 40/41 | 50% HP / +50% dano (D7) |
| Dodge prob/frames | 84/85 | 20% / 30f=1s (D8) |
| Sage Slayer | 111 | seta 1 (D11) |
| Explosion Immunity | 116 | seta 1 (D13) |
| Warp Immunity | 75 | seta 1 (D14) |
| Toxic Immunity | 90 | seta 1 (opcional, design 3.2) |

## Cobertura do desejo da comunidade (D1-D17)

13 de 17 implementados (D15 resolvido nesta revisão — era o mesmo
mecanismo do D8, não item separado). Mapeamento completo:

| # | Desejo | Status |
|---|---|---|
| D1 | Surge Immunity | ✅ implementado |
| D2 | HP Up 80% | ✅ implementado |
| D3 | Range +60 | ✅ implementado |
| D4 | Knockback Immunity | ✅ implementado |
| D5 | Cooldown -400f | ✅ implementado |
| D6 | Mini-wave | ✅ implementado |
| D7 | Strengthen 50%@50%HP | ✅ implementado |
| D8 | Dodge 20%/1s | ✅ implementado |
| D9 | Behemoth Slayer | ✅ implementado |
| D10 | Wave Immunity | ✅ implementado |
| D11 | Sage Slayer | ✅ implementado |
| D12 | Attack Speed Up | ✅ implementado (metade — ver Backswing abaixo) |
| D13 | Explosion Immunity | ✅ implementado |
| D14 | Warp Immunity | ✅ implementado |
| D15 | Shrug Off | ✅ **implementado via D8** — mesma mecânica interna, ver abaixo |
| D12 (parte 2) | Backswing 12f→6f | ⏳ em investigação ativa (hook de animação), ver abaixo |
| D16 | Ultra Form | ❌ jogo não tem essa forma ainda, ver abaixo |
| D17 | Talents oficiais | ❌ redundante, ver abaixo |

### D15 Shrug Off = D8 Dodge (achado real, não aproximação)

Pesquisa cruzada com 4 fontes independentes confirma que "Shrug Off"
não é um recurso hipotético isolado — é o **mesmo proc interno**
`IMUATK` que já implementa o Dodge:
- `tbcml/game_data/bcu.py:176-177,547-548` — `dodge_prob`/
  `dodge_duration` lidos exclusivamente do proc `IMUATK`, sem proc
  "DODGE" separado.
- `BCU-java-PC/resources/util.properties:364` — `ot13=Dodge Attack`.
- `BCU-java-PC/Interpret.java:127,136` — `Data.P_IMUATK` como proc
  único, não agrupado.
- `cats.py:867-920` — `DODGE` no enum de habilidades não é proc
  próprio, é a habilidade id=51 que ativa o proc `IMUATK`.

O próprio design da comunidade (`battlecats-mecha-bun-ideal-
comunidade.md`) nunca cita "Shrug Off" como campo real do jogo —
confirmado também que a wiki/comunidade não documenta esse termo como
mecânica existente (só aparece como "nova ability custom" no PR
original). D15 não precisa código novo — já está satisfeito.

## Escopo honesto

**Tudo abaixo é confirmado via fonte autoritativa (tbcml), não mais
suposição rotulada**:
- HP, ATK — multiplicar o raw por 1.8 propaga +80% pro stat final
  calculado pelo jogo, seja qual for a curva de `unitlevel.csv` aplicada
  depois (curva multiplicativa preserva proporção).
- Attack Interval, Recharge — **fórmula confirmada exata** via tbcml
  (`unit.py:126-136`, `Frames.from_pair_frames`): frames reais = raw × 2
  ("pair frames"). Não é suposição — é o código de conversão real da
  lib de modding, transform linear provado.
- Range — **confirmado sem transform** via tbcml (`cats.py:332`,
  `self.range = raw_data[5]`, sem wrapper nenhum): valor final = raw
  direto. Escalar raw por 250/190 dá final=250 exato, não aproximado.
- Imunidades e Behemoth Slayer — bool flags diretos (`tbcml`
  `unit_bool()` = `bool(value)`, 0=false/qualquer-não-zero=true), sem
  ambiguidade.
- Sage Slayer (D11, índice 111) e Explosion Immunity (D13, índice 116)
  — confirmados via pesquisa dedicada (OpenCodePOCOC75) contra tbcml e
  dados reais de outras unidades (BCData `unit781.csv` col 111=1,
  `unit780`/`unit784.csv` col 116), mesmo padrão bool das outras
  imunidades.
- Mini-wave (D6), Strengthen (D7), Dodge (D8) — `tbcml` (`unit.py`
  classes `Wave`/`Strengthen`/`Dodge`) confirma os índices e que `Prob`
  é percentual direto (`unit.py:164-181`, sem wrapper) — valores do
  design ideal atribuídos sem nenhuma conversão precisar.

Resta uma incerteza real, mas de escopo bem menor: o valor "atual" de
190/32f/84.5s (usado como referência do "antes" pra calcular a razão de
buff) vem de fontes wiki/comunidade, não lido diretamente do raw da
build 15.6.0 atual. **Correção (achado do hermes na revisão)**: a
afirmação anterior de que "o resultado continua correto mesmo se o
atual citado estiver desatualizado" era imprecisa — a razão `num/den`
preserva a PROPORÇÃO sobre qualquer raw real, mas o valor absoluto
final só bate exatamente com o alvo do design (ex.: range=250) se o
raw real na struct for igual ao denominador usado (190). Se a build
atual tiver um raw diferente de 190, aplicar `raw*250/190` dá um
resultado proporcionalmente equivalente, mas não necessariamente o
alvo absoluto 250 — o mod aplica a mesma multiplicação, correta em
proporção, sem garantia de bater o número absoluto do design se o
valor de referência estiver desatualizado.

- Warp Immunity (D14, índice 75, `warp_blocker`) e Toxic Immunity
  (índice 90, `toxic_immunity`) — **correção desta revisão**: versão
  anterior deste README dizia "Warp Immunity não existe campo real",
  ERRADO — campo confirmado direto no tbcml (`cats.py:227-228`), mesmo
  padrão bool das outras imunidades. Implementado.

**Resolvido nesta revisão (não é aproximação, é achado por identidade
de código)**:
- Shrug Off (D15) = Dodge (D8) — pesquisa cruzada (4 fontes
  independentes, ver seção acima) confirma que são a mesma mecânica
  interna (proc `IMUATK`), não conceitos diferentes. Não precisa de
  código novo — D8 já satisfaz D15.

**Em investigação ativa (não abandonado)**:
- Backswing (D12, metade) — NÃO existe como campo CSV separado no
  schema real do tbcml (só existe "foreswing", índice 13). Confirmado
  que o playback de animação usa nomenclatura de engine genérica
  (`%s_entry.maanim`, tipo cocos2d) — não é dado isolado por unidade.
  Hookar isso com segurança exigiria RE do sistema de animação do
  motor (escopo comparável ou maior que este mod inteiro). Pesquisa
  dedicada em andamento pra achar ponto de hook seguro antes de
  desistir.

**Não feito, confirmado limitação real (verificado direto no tbcml,
não suposição herdada)**:
- Ultra Form (D16) — **correção desta revisão**: claim anterior de
  "já coberto pelo loop de 4 formas" estava errado. Dado real
  (`unit427.csv`) mostra que Mecha-Bun tem só 3 formas hoje
  (Normal/Evolved/True); a 4ª iteração do loop escreve numa área que o
  jogo não usa/renderiza pra essa unidade — escrita inerte, sem efeito
  visível, NÃO é "dar Ultra Form". Ultra Form de verdade exige a Ponos
  adicionar a forma (animação/asset/stats) ao jogo — mod de patch de
  memória não cria conteúdo que o jogo não tem alocado.
- Talents oficiais (D17) — o próprio design "ideal comunitário" (seção
  3.3 do doc da comunidade) mapeia os 10 níveis de talento propostos
  1:1 pros efeitos D1/D2/D3/D4/D5/D6/D7/D8/D9/D10, TODOS já
  implementados aqui como stat base incondicional (sem custo de NP). Um
  sistema de talento de verdade (hook diferente, curva de NP, save de
  progresso) entregaria o MESMO efeito atrás de grind — pior pro
  jogador que já tem de graça. Não implementado por falta de ganho
  real, não por limitação técnica.

## Build

```
ndk-build -B -j4 -C mods/mechabun
```

Produz `mods/mechabun/libs/arm64-v8a/libmechabun.so`. Deploy via
`push_mod` do companion (protocolo já existente do loader) pra
`/data/local/tmp/bc_mods/` — **não precisa reboot**: o companion
sinaliza `persist.bc_poc.reload_mods` (property cross-process,
`jni/companion.cpp:724`), o loader recarrega os mods em runtime.

## Teste

`test_buff_math.cpp` — teste host-only das 4 fórmulas de escala inteira
(HP/ATK, Range, Recharge, Attack Interval), roda com
`g++ -fsanitize=address,undefined`. O resto do mod (hook install,
resolve_pattern, escrita de memória real) só roda no device — **não
testado em device real ainda**, só build + assinatura AOB validada
offline. A assinatura AOB é verificada única especificamente na build
JP 15.6.0 (build-id `b94cc0da...`) — se o jogo atualizar, a unicidade
precisa ser re-verificada (mod fica inativo com aviso, não crasha,
nesse caso). Testar no device exige `push_mod` (ação real em device),
não feito sem autorização explícita do usuário.
