# mechabun — mod real pro bepinEx-termux

Primeiro mod de gameplay de verdade rodando no loader (não é mais throttle
de frame nem exemplo — mexe em stat de unidade real). Buffa HP e ATK do
Mecha-Bun (#426) em +80%, primeiro item do design "ideal comunitário"
(`battlecats-mecha-bun-ideal-comunidade.md`, item D2, maior consenso
depois de Surge Immunity).

## Como funciona

Engenharia reversa real (Ghidra 12.1 headless + radare2, `libnative-lib.so`
JP 15.6.0, build-id `b94cc0dafd8521f1f7cfcf3841a29f13d7cd1ef3`):

- `unit%03d.csv` é o nome real do arquivo por unidade (`unit426.csv` pro
  Mecha-Bun, unitId zero-based = 425).
- Função que carrega esse CSV (assinatura AOB de 48 bytes, verificada
  única no binário inteiro) popula uma struct em
  `bigData + unitId*0x760 + 0x9e568`, até 4 formas, stride `0x1d8` (472
  bytes) cada, 118 campos int32 por forma (offset = coluna_csv × 4).
- Colunas confirmadas em `battlecats-mecha-bun-forense.md`: col 6/7/8 =
  HP (Lv1/Lv30/Lv50), col 9/10/11 = ATK (base/Lv30/Lv50), valores RAW
  pré-curva de `unitlevel.csv`.
- Hook via `bc_mod_api.h` v3 (`install_hook`, capacidade NOVA adicionada
  no loader por este mod — DobbyHook cru pra alvo achado por
  `resolve_pattern`, fora dos 4 hooks nomeados fixos): pós-execução do
  original, se `unitId==425`, multiplica os 6 campos por 1.8 (`*9/5`
  inteiro) nas 4 formas.

## Escopo honesto (v1)

Feito: HP +80%, ATK +80% (D2 do design ideal).

NÃO feito (documentado, não fingido):
- Range 190→250 e velocidade — são ÍNDICES de lookup table no CSV, não
  valor direto (raw 9→190 exibido, raw 36→23 exibido — não-linear).
  Escalar o índice não escala o resultado de forma previsível sem mapear
  a tabela de lookup primeiro.
- Recarga -400f, attack frequency -6f, backswing -6f — colunas ainda não
  localizadas.
- Imunidades (Surge/Wave/Knockback) — provavelmente bitflags em outra
  struct (buff/imunidade costuma ser tabela separada, tipo `unitbuff.csv`
  or similar) — não investigado ainda.

## Build

```
ndk-build -B -j4 -C mods/mechabun
```

Produz `mods/mechabun/libs/arm64-v8a/libmechabun.so`. Deploy via
`push_mod` do companion (protocolo já existente do loader) pra
`/data/local/tmp/bc_mods/`.

## Teste

`test_buff_math.cpp` — teste host-only da fórmula de arredondamento
inteiro (`*9/5`), roda com `g++ -fsanitize=address,undefined`. O resto do
mod (hook install, resolve_pattern) só roda no device — não testado em
device real ainda (só build + assinatura AOB validada offline).
