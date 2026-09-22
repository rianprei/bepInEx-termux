---
name: battlecats-mechabun-atk-dead-struct
description: Mecha-Bun (unit 426) — struct CSV-parsed que o mod escreve pra ATK/RANGE/RECHARGE/BEHEMOTH_SLAYER/SAGE_SLAYER não tem NENHUM leitor real em batalha; achado via busca exaustiva de xrefs no binário, não hipótese
metadata:
  type: project
  status: documented
---

# Mecha-Bun: struct de stats sem leitor pra ATK/RANGE/RECHARGE

**Fonte:** `mods/mechabun/jni/mechabun_mod.cpp` (comentário "ACHADO CRITICO
2026-09-20"), investigação real via `r2` direto no binário do device +
Ghidra 12.1 (OpenCode), `libnative-lib.so` JP 15.6.0, build-id
`b94cc0dafd8521f1f7cfcf3841a29f13d7cd1ef3`.

## O achado

O mod escreve stats de unit no struct CSV-parsed carregado pelo loader
(`unit427.csv`, unitId zero-based 426). Pra HP isso funciona — o valor
escrito de fato afeta o jogo. Pra ATK/RANGE/RECHARGE e alguns traits, não:

| Campo | Offset (dentro do struct) | Leitores encontrados |
|---|---|---|
| HP | `+0x9e318` | 5 leitores reais (`0x8712d8`, `0x871420`, `0x872290`, `0x8725c0`, `0x87271d`) |
| ATK | `+0x9e324` | **0** |
| RANGE | `+0x9e32c` | **0** |
| RECHARGE | `+0x9e334` | **0** |
| STRONG_AGAINST | `+0x9e374` | **0** |
| BEHEMOTH_SLAYER | `+0x9e4bc` | **0** |
| SAGE_SLAYER | `+0x9e4d4` | **0** |
| WAVE_IS_MINI | `+0x9e490` | **0** |
| TOXIC_IMMUNITY | `+0x9e480` | **0** |

Método: busca exaustiva por padrão `mov`+`movk` de imediato completo — o
único jeito que C++ compilado acessa um campo de struct em offset fixo.
Não é blind-spot da técnica: HP, no MESMO struct, tem 5 leitores reais
achados com a mesma busca (fazem multiplicação/divisão de ponto fixo). A
ausência nos outros campos é conclusão sólida, não falha de busca.

## Onde o dano real é lido de verdade (rastreado, endereço específico ainda não confirmado por hook em device)

A factory de entidade de combate (`fcn.008717f8`, aloca objeto de 488
bytes) nunca referencia esse struct CSV — recebe tudo por argumento de
uma camada de "prep" (8+ call sites em `0x7c3a38`-`0x7c3e4c`) que lê de
uma tabela **completamente diferente**: singleton (`bl 0x601f5c`) +
`idx*0xc738 + idx2*0x3e8` + offset fixo por campo (`+0x83924`, `+0x83978`,
`+0x8397c`, `+0x83a54`, `+0x83a58`, `+0x83a7c`, via dezenas de thunks
getter/setter auto-gerados em `0x870910+`). Provavelmente é a struct de
"stat efetivo de batalha" (pós-talento/tesouro/catseye).

Localizado sem precisar de device, numa sessão Ghidra posterior: os campos
de dano dessa tabela alternativa são `+0x838c8`/`+0x838f8` (setters em
fileoffset `0x86ac88`/`0x86bfd0`, únicos writers, alimentados pelos
getters `0x8782c4`/`0x878cec`). Esses 2 getters foram hookados primeiro,
mas depois **provados condicionais** via `r2` — só disparam em hits com
status de warp/curse (gated por `tbz` de flag no lado consumidor,
`FUN_00592dd0` em `0x493a80`/`0x493e80`), confirmado também em device
(6/6 disparos com `orig=0` num hit normal sem status). Não são o caminho
do dano normal.

O getter que de fato alimenta dano **normal incondicional** é outro:
`0x8789e8` → `+0x83774` (coluna CSV 26) — é esse que a seção `// D2-fix`
em `mechabun_mod.cpp` hookeia hoje (`kDmgBasePattern`). **Status atual
(known issue, ver CHANGELOG):** esse hook instala e dispara normalmente
(confirmado no log, ~a cada 45-50s, sempre `form=1`), mas o valor lido no
momento é sempre `orig=0` nos testes ao vivo — a fonte de dado real por
trás desse getter ainda não foi confirmada como o número de dano final em
combate. Três hipóteses de getter já testadas e falsificadas ao vivo até
agora (struct CSV col3 sem leitor; getters warp/curse condicionais;
getter dmg-base incondicional com `orig=0`) — nenhuma nova hipótese
gerada ainda.

## Impacto prático no mod

Os campos abaixo **são escritos corretamente** no struct CSV (offset e
valor confirmados via log ao vivo), **mas não têm efeito em batalha
real** — root cause confirmado, não hipótese:

- **ATK** (D2, parte do buff): +80% no struct, zero efeito no dano real.
- **RANGE** (D3): 190→250 no struct, zero efeito no alcance real.
- **RECHARGE** (D5): -400f no struct, zero efeito no cooldown real.
- **BEHEMOTH_SLAYER** (D9): flag setada no struct, zero efeito em combate.
- **SAGE_SLAYER** (D11): flag setada no struct, zero efeito em combate.

Campos que **funcionam** (leitor real confirmado, ver tabela "Índices
reais" no README do mod): HP, Attack Interval, Wave Immunity, Knockback
Immunity, Surge Immunity, Wave prob/level/mini, Strengthen, Dodge,
Explosion Immunity, Warp Immunity, Toxic Immunity.

## Próximo passo (não implementado)

Confirmar em device (hookar os `STR` nos offsets da tabela alternativa
durante boot de batalha real e comparar contra ATK conhecido=400 por
eliminação) qual campo específico da tabela `idx*0xc738` é o ATK
consumido de verdade — ou provar que o getter `0x8789e8`→`+0x83774`
já hookado (`kDmgBasePattern`, seção `// D2-fix`) é de fato o caminho
certo e o `orig=0` observado tem outra causa (timing do hook, condição
de side não bater, etc — não investigado ainda). Os getters
`+0x838c8`/`+0x838f8` (`0x8782c4`/`0x878cec`) já foram descartados:
provados condicionais a status de warp/curse, não disparam em hit
normal (ver seção acima).
