# kungfux — Kung Fu Cat X (#132) sem os cons da wiki

Remove os cons listados em https://battle-cats.fandom.com/wiki/Kung_Fu_Cat_X_(Rare_Cat)
("Slow attack rate and movement speed", "One knockback", "Single Attack", "Very expensive")
sem tocar nos pros (dano, multi-hit, imunidade a weaken/warp). O patch vale para as 3 formas.

| con | coluna | vanilla (f0/f1/f2) | patch | leitor (build 338b0601) |
|---|---|---|---|---|
| movimento lento | 2 speed | 8 | 10 (mediana dos Rare) | 0x872338 |
| 1 knockback | 1 KB | 1 | 3 (mediana dos Rare) | 0x872290 |
| single attack | 12 área | 0 | 1 | 0x8749ac |
| ataque lento | 4 intervalo | 100/88/73 | 40/28/28 | 0x872c7c |
| muito caro | 6 custo | 1560 | — | sem leitor no struct |

Ciclo de ataque = `col4*2 + último foreswing - 1`. A fórmula bate com a wiki nas 3 formas (210/210/180f).
O alvo é 90f (3s): a forma 0 tem foreswing 11, então col4 = 40; as formas 1/2 têm último foreswing 35, então col4 = 28.
DPS da True Form no Lv50: 8.316 → ~16,6k.

**Sem hook:** o `01_mechabun` já engancha o loader de unit CSV. O Dobby recusa hook duplo, e o AOB some depois do primeiro hook.
Por isso uma thread lê o struct do #132 pelo getter do singleton (`0x601f5c`, `adrp+add+ret`, AOB único) e só escreve quando HP/ATK/área batem com o vanilla do `unit133.csv`.
Se o jogo recarregar os dados, ela reaplica.

Deploy: `./deploy.sh` (companion do jogo rodando) → `02_kungfux.so` em `/data/local/tmp/bc_mods/`.
Log esperado: `[kungfux] #132 patch aplicado (#1): speed 10, KB 3, area, ciclo 90f (col4 40/28/28)`.
Validado em batalha 2026-09-23 (área, KB 3, ciclo rápido). Pendente: hook do custo (deck slot 0x360090, ver context/kungfu-cat-x-pesquisa-OpenCode.md no vault).
