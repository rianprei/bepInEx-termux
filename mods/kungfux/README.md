# kungfux — Kung Fu Cat X (#132) sem os cons

Remove os cons da wiki (https://battle-cats.fandom.com/wiki/Kung_Fu_Cat_X_(Rare_Cat)) e os que a comunidade levanta.
A varredura completa, com fontes, está no vault: `context/kfx-cons-CONSOLIDADO.md`.
O patch não toca nos pros (dano total, imunidade a weaken/warp) e vale para as 3 formas.
Teto de referência: Dancer Cat TF Lv30 (godfat), a unidade que a comunidade diz que "outclassa" o KFX.

| con | coluna | vanilla (f0/f1/f2) | patch | leitor (build 338b0601) |
|---|---|---|---|---|
| movimento lento | 2 speed | 8 | 10 (mediana dos Rare) | 0x872338 |
| 1 knockback / frágil | 1 KB | 1 | 3 (mediana dos Rare) | 0x872290 |
| single attack | 12 área | 0 | 1 | 0x8749ac |
| ataque lento | 4 intervalo | 100/88/73 | 40 (ciclo 90f) | 0x872c7c |
| 3º golpe concentra o dano e erra | 3, 59-62, 64-65 | multi-hit f1/f2 (foreswing 11/15/35) | 1 golpe só: ATK = soma dos 3, no foreswing 11 | tabelas por golpe: ATK (3,59,60) 0x872584/0x87270c, foreswing (13,61,62) 0x872b4c, ability (63,64,65) 0x8734bc |
| HP baixo (16.983 Lv30) | 0 HP | 999 | 1648 (Lv30 ~28,0k; Dancer TF 27,5k) | leitor por golpe/genérico (base `+0x9e318`) |
| range curto | 5 range | 300 | 350 (Dancer TF 330) | 0x872cc0 |
| muito caro | 6 custo | 1560 (Cap.1) | 1200 (Cap.2 1.800; Dancer 2.250) | 0x793f44 multiplica ×100 após o load; escala proporcional |

**Ciclo de ataque:** `col4*2 + último foreswing - 1`. A fórmula bate com a wiki nas 3 formas (210/210/180f).
- Com um golpe só, o último foreswing é o do golpe 1 (11f), então col4 = 40 dá 90f (3s) em todas as formas.
- O DPS é igual ao do patch anterior (TF Lv50 ~16,6k), mas o dano sai no frame 11 em vez do 35.
- O dano também não se perde quando os golpes 1-2 empurram o inimigo.

**Fica como está:**
- **Imunidade a warp:** contestada. 3 fontes dizem que ela impede de reposicionar; outras a chamam de única vantagem.
- **Sem talentos, combos e dificuldade de obter:** não são stat da unidade. Os stats acima compensam a vantagem do Dancer, que vem dos talentos.

**Sem hook:** o `01_mechabun` já engancha o loader de unit CSV. O Dobby recusa hook duplo, e o AOB some depois do primeiro hook.
- Uma thread lê o struct do #132 pelo getter do singleton (`0x601f5c`, `adrp+add+ret`, AOB único).
- Ela só escreve quando HP/ATK/área batem com o vanilla do `unit133.csv`.
- Se o jogo recarregar os dados, ela reaplica.

**Deploy:** `./deploy.sh` com o companion do jogo rodando. Com o jogo fechado, sobrescrever o conteúdo de `/data/local/tmp/bc_mods/02_kungfux.so` (o diretório é do root; o arquivo é shell 666).

**Log esperado:** `[kungfux] #132 patch aplicado (#1): speed 10, KB 3, area, ciclo 90f, 1 golpe (TF atk …, fs 11), HP …, range …, custo …`.

**Validado em batalha em 2026-09-23:** área, KB 3, ciclo rápido. **Validado em 2026-09-24:** custo no deck = 1.800 (Cap.2). A consolidação do multi-hit, o HP e o range ainda não foram testados em batalha.
