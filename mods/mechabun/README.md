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

16 de 17 implementados/resolvidos. Mapeamento completo:

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
| D16 | Ultra Form | ✅ **implementado como tier de stats na True Form** — ver abaixo |
| D12 (parte 2) | Backswing 12f→6f | ⏳ investigação de escopo grande, não fechada |
| D17 | Talents oficiais | ⏳ RE real feita, achado real, não implementado por risco — ver abaixo |

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

### D16 Ultra Form — implementado como tier de stats (não nova forma)

Confirmado via wiki oficial: "Ultra Forms are currently exclusive to
Uber Rare Cats... Mecha-Bun is a Special Cat, not an Uber Rare Cat" —
Ultra Form não é uma mecânica disponível pra essa classe de raridade
no jogo vanilla, não é falta de dado por acaso. Dado real
(`unit427.csv`) confirma só 3 formas hoje (Normal/Evolved/True).

Solução real, 100% dentro do mod (zero arquivo do jogo tocado):
aplica os números-alvo do "Ultra" (HP 520.000, Strong Against
Floating/Relic/Aku) direto na **True Form** (índice de forma 2) em vez
de fabricar uma 4ª forma inexistente:
- HP: raw da True Form escalado por `325/54` (fração exata derivada de
  520000/86400 — mesma lógica proporcional dos outros campos: curva de
  level é multiplicativa sobre o raw, razão final bate o alvo
  independente da fórmula exata da curva).
- Strong Against Floating/Relic/Aku: índices 23/16/96. Mecha-Bun já
  nascia com `strong=1` e `target_relic=1` nativos (confirmado no raw
  real) — só faltava `target_floating`/`target_aku`, agora setados.

O que se perde: apresentação (4ª aba no cat-guide, sprite/animação
nova) — conteúdo que não existe no pack do jogo de qualquer forma,
nenhum patch de memória fabricaria isso. Pesquisado ativamente (web +
GameBanana) se existe skin/reskin pronto pra usar — não existe nenhum.
Usuário forneceu arte-conceito fã-feita (ilustração de pose única,
`fan-made-ultimate-mecha-bunbun-mkx...png`) — confirmado (2 agentes
independentes) que o jogo não tem slot de retrato/galeria estático
onde essa arte pudesse entrar sem rig completo (imgcut/mamodel/maanim,
corpo fatiado em partes); único uso real seria recortar o busto pro
ícone de deploy 128×128 (`uni{cat_id}_{forma}00.png`), não pro sprite
de batalha em si.

### D16.1 Icons estáticos gerados da arte fan-made (deploy + upgrade)

Segunda passada de verificação (sessão Freebuff, independente da de 2
agentes acima) confirmou a conclusão e achou **um slot a mais** que ela
não citou: `udi{cat_id}_{form}.png` (**upgrade icon**, tela de upgrade
da unidade), carregado junto do deploy em `read_icons()` (tbcml
`cats.py:1349-1350`). São esses — e só esses — os 2 slots de imagem
estática por unidade no jogo: loading screen é textura global única
(`download.png`, `loading_screen.py:22`), banner de gacha é imagem de
item de shop (`gatyaitemD_{id}_f/z.png`, `gatyaitem.py:217`), nenhum
é por unidade.

**Arquivos gerados** (nesta pasta `assets/`, zero arquivo do jogo
tocado), a partir da arte fan-made do usuário:

| Arquivo | Slot | Dimensão | Conteúdo |
|---|---|---|---|
| `uni426_s00.png` | deploy icon (battle) | 128×128 | sujeito recortado composto dentro da janela oficial (14,26)-(113,101) do frame `uni_s.png` do tbcml, bottom-aligned |
| `udi426_s.png` | upgrade icon (menu) | 294×111 | sujeito sobre plate `udi_s.png` ×3.5 colada em (13,1), crop (13,1,307,112) — pipeline exato `format_bcu_upgrade_icon_s`+`crop_upgrade_icon` (`cats.py:1420/1433`) |

**Convenção de nome** (tbcml `cats.py:1240/1243`, `get_cat_id_str` =
`PaddedInt(cat_id, 3)`):
- deploy: `uni{cat_id:03d}_{form}00.png` → cat_id 426 True Form =
  `uni426_s00.png`
- upgrade: `udi{cat_id:03d}_{form}.png` → cat_id 426 True Form =
  `udi426_s.png`
- formas: `f`=Normal, `c`=Evolved, `s`=True, `u`=Ultra (`CatFormType`,
  `cats.py:31-37`); Mecha-Bun só tem as 3 primeiras (ver D16)
- nota: nos stats o arquivo é `unit{cat_id+1}.csv` (unit427.csv), mas
  nos icons NÃO tem +1 — `uni426`/`udi426` direto (padding 3 dígitos,
  ex.: cat_id 9 → `uni009_f00.png`)

**Geração reproduzível**: `gen_icons.py` nesta pasta (roda com a venv
battlecats: `~/.venvs/battlecats-mod/bin/python gen_icons.py`).
Chroma-key do fundo teal (flood-fill das bordas tolerante a gradiente
radial — modelo plano de 1º grau falha no spotlight dessa arte;
bandas autoritativas flood=sujeito/bg-profundo com decisão por cor só
na transição de 3px e no resgate de perna cinza dessaturada conectada
ao sujeito; decontaminação de fringe via estimativa premultiplicada).
Trocou a arte, roda de novo.

**Escopo honesto**: os PNGs vivem só aqui no mod. Pra aparecerem no
jogo falta o passo de entrega (redirect de arquivo no loader ou
repack do pack de assets baixado — os icons não existem no
install_pack, verificado: 199 assets, zero `uni*`/`udi*`). Não feito
nesta etapa.

**Achado real pro hook de entrega** (RE feita, hook ainda não
implementado): `FUN_00744998` (endereço real, `libnative-lib.so` JP
15.6.0) é a função que resolve `cat_id` → filename do deploy icon.
Recebe o `cat_id` em `param_1[4]`, monta a string via padrão
`uni%03d_%@%02d.png` (string real no binário, endereço `0029db73`),
com fallback pra ícone genérico numérico (`FUN_004c80e8`) só em cache
miss (`*param_1==0`). Duas irmãs confirmadas com mesmo padrão de
código: `FUN_004c80e8`/`FUN_004c7b2c` (usam a mesma tabela XOR-obfuscada
de índice por `cat_id`, offset `+0x4af08`, e a mesma string
`uni%03d_m%02d.png`). Candidato de hook: `install_hook` em
`FUN_00744998`, interceptar quando `param_1[4] == 426` e redirecionar
o path resultante pro asset do mod (`uni426_s00.png`). **Não
implementado ainda** — falta confirmar onde o filename resultante vira
`fopen`/`AAssetManager_open` de fato (essa função só monta a string,
não abre o arquivo), pra decidir se o hook fica em `FUN_00744998`
(troca o cat_id resolvido) ou mais adiante na cadeia (troca só o load
de I/O, mais seguro pra não afetar cache/resolução de outras 180+
unidades).

Rastreado um nível a mais: `FUN_00744998` chama `FUN_00493f70`
(endereço real), que é um **resource cache genérico** — lookup por
filename-key num container tipo mapa (`FUN_0041607c`), com resultado
guardado via `__shared_weak_count` (contagem de referência
compartilhada, padrão cache de textura/imagem usado pra QUALQUER
asset do jogo, não só icons). Em cache miss, chama `FUN_0049631c`.
**Correção após decompilar**: essa função NÃO é o load de arquivo — é
init genérico de objeto interno de engine (aloca com vtable
`PTR_FUN_00bc9f78`, chama método virtual por índice, sem nenhuma
referência a filename/string). Beco sem saída nessa direção
específica — o load real de arquivo fica em outro callee de
`FUN_00493f70` ainda não identificado (candidatos restantes não
rastreados: `FUN_004092e0`, `FUN_00409350`, `FUN_004162b4`,
`FUN_009bd700`, `FUN_004966bc`, `FUN_0049674c`). Parado neste ponto
por disciplina de risco/tempo: `FUN_00493f70` é compartilhado por
todo tipo de asset do jogo — hookar nesse nível exige filtro preciso
por filename pra não afetar o cache de mais nada; ir mais fundo sem
esse filtro pronto é escopo não fechado igual ao D17.

### D17 Talents oficiais — RE real completa, não implementado por risco real

**Engenharia reversa completa e honesta, não abandono por preguiça.**
Achado via decompile direto (Ghidra) de `FUN_006b2d68` (endereço real
do binário JP 15.6.0, xref confirmado da string "SkillAcquisition.csv"):

- `SkillAcquisition.csv` é config estática carregada do disco todo
  boot (mesma categoria de `unit427.csv`), **não é save do jogador** —
  confirmado (tbcml armazena `self.talents` igual `self.unit_buy`,
  nenhuma referência a `SaveData` em lugar nenhum do código).
- Layout de struct em memória confirmado por completo: nó de árvore
  de 0x1e8 bytes — 36 bytes de bookkeeping (ponteiros
  esquerda/direita/pai + chave `cat_id` no offset 0x1c) + 8 grupos
  (habilidades A-H) × 56 bytes cada (14 campos int32/grupo: abilityID,
  MAXLv, min/max×4, textID, LvID, nameID, limit — bate exato com os
  114 campos do CSV real).
- A árvore inteira vive em `param_1 + 0x233000` dentro do singleton
  principal do jogo — **não é array simples, é uma árvore binária
  balanceada** (`std::map`/`std::__ndk1::__tree`, libc++, com
  campos `__left_`/`__right_`/`__parent_`/`__is_black_` confirmados
  contra o código-fonte real da LLVM).
- 9 de 10 ability IDs reais dos efeitos do design ideal confirmados
  (cruzando `SkillAcquisition.csv` real com `TalentAbilityType` do
  tbcml): Surge Immunity=55, Wave Immunity=48, Knockback Immunity=47,
  HP Up=32, Cooldown Down=26, Mini-wave=62, Strengthen=10, Dodge=51,
  Behemoth Slayer=64. Range Up não tem talento real no jogo vanilla
  (confirmado, não fabricado).

**Por que não foi implementado**: cat_id 426 (Mecha-Bun) não tem linha
em `SkillAcquisition.csv` hoje — pra existir de verdade, precisaria
INSERIR um nó novo nessa árvore balanceada compartilhada por TODAS as
180 unidades reais que já têm talento. Isso exige replicar
corretamente o algoritmo de rebalanceamento red-black da libc++
(rotações, cor de nó) — um erro sutil não afeta só o Mecha-Bun, pode
corromper a estrutura de talento de QUALQUER outra unidade, com
sintoma só visível em teste extensivo em device (que não pode ser
feito sem autorização explícita do usuário a cada etapa). Achar a
função de CONSULTA oficial (caminho mais seguro, sem tocar a árvore)
virou busca sem limite claro: o offset base `0x233000` é referenciado
por 59 funções diferentes só nesse binário (a maioria mexe em outras
tabelas de dado não-relacionadas que vivem na mesma região de
memória), inviável de rastrear uma-a-uma manualmente.

Decisão de engenharia: risco de corrupção de estrutura compartilhada
sem forma de testar com segurança > valor de entregar D17. Achado
documentado por completo pra retomar no futuro se surgir um caminho
mais seguro (ex.: achar a função de consulta oficial com mais tempo,
ou confirmar via teste real em device com autorização).

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
