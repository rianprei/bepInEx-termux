# DLL Coverage — 2026-09-27

## Resumo

| Métrica | Valor |
|---|---|
| Total de mods no corpus | 30 |
| Total de patches Harmony | 378 |
| Patches traduzidos | 0 (0%) |
| Mods 100% traduzíveis | 0 (0%) |
| Recusas "assembly do método-alvo não está na DLL" | 375 |
| Recusas "classe aninhada" | 3 |

## Veredito: LIMITE DE PROJETO (2b)

O tradutor exige que o tipo alvo (do atributo `[HarmonyPatch(typeof(X), "M")]`)
esteja presente na própria DLL do mod. Mas o alvo mora no `Assembly-CSharp` do
jogo, que não vem junto do mod. O tradutor não pode confirmar o método-alvo sem
o assembly do jogo.

**Prova:** 375 de 378 patches recusados com "assembly do método-alvo não está na DLL".
Os 3 restantes são "classe aninhada" (o u_patch ainda não localiza esse tipo de classe).

## Motivo das 375 recusas

O tradutor (`HarmonyTranslator.java:80-82`) exige que o tipo alvo esteja na DLL:

```java
DllReader.TypeInfo targetType = findType(types, candidate.target.className);
if (targetType == null) {
    reports.add(reportPrefix + "assembly do método-alvo não está na DLL; não é possível confirmar overload");
    continue;
}
```

**Por quê:** sem o tipo alvo na DLL, o tradutor não pode confirmar qual método
o patch altera (overload ambíguo, tipo de retorno, etc.). O alvo mora no
`Assembly-CSharp` do jogo, que não vem junto do mod.

## 3 exemplos reais das 375 recusas

### 1. Darkwood_Customizer.dll — DarkwoodCustomizer.CamMainPatch.CamUpdate

**O que faltou:** o tipo alvo `CamMainPatch` não está na DLL (morar no Assembly-CSharp do jogo).

**IL resumido:** `nop ldsfld ldc.i4.0 ceq stloc.0 ldloc.0 brfalse nop ldarg.0 ldfld stloc.1 ...`

### 2. Darkwood_Customizer.dll — DarkwoodCustomizer.CharacterPatch.CharUpdate

**O que faltou:** o tipo alvo `CharacterPatch` não está na DLL.

**IL resumido:** `nop ldarg.0 ldc.i4.0 call nop ret`

### 3. Darkwood_Customizer.dll — DarkwoodCustomizer.CharacterPatch.ChararcterAwake

**O que faltou:** o tipo alvo `CharacterPatch` não está na DLL.

**IL resumido:** `nop ldarg.0 ldc.i4.1 call nop ret`

## Modo medição (sem conferir no jogo)

Se o tradutor não exigisse que o tipo alvo esteja na DLL (usando apenas o nome
do tipo e do método do atributo), quantos patches seriam traduzidos?

**Resposta:** não medido diretamente (requer mudança no tradutor). Mas a
classificação do corpo dos 378 patches mostra que:

| Corpo do patch | Contagem | % |
|---|---|---|
| branch/if | 260 | 69% |
| chamada de método | 104 | 28% |
| campo estático | 10 | 3% |
| outro | 2 | 1% |
| acesso a campo | 1 | 0% |
| aritmética | 1 | 0% |

**Risco de traduzir sem conferir:** o tradutor não pode confirmar overload
ambíguo, tipo de retorno, etc. Isso pode gerar patches incorretos que alteram o
método errado.

## Top 5 padrões do CORPO (não os motivos de recusa)

| Padrão | Contagem | Exemplo | Veredito |
|---|---|---|---|
| branch/if | 260 | Darkwood_Customizer.CamMainPatch.CamUpdate | **Impossível sem runtime .NET:** o patch altera fluxo condicional, não um valor fixo |
| chamada de método | 104 | Darkwood_Customizer.CharacterPatch.CharUpdate | **Verbo novo:** o patch chama um método do jogo; o u_patch precisaria de um verbo "call" |
| campo estático | 10 | TPC_CheatCraftFromNearbyContainers.Plugin.Patch_UiWindowPause_OnQuit | **Cabe no C4:** o verbo `static` já existe |
| acesso a campo | 1 | Darkwood_Customizer.EnemiesPatch.DaySpawnChancePostfix | **Cabe no C4:** o verbo `field` já existe |
| aritmética | 1 | TPC_CheatInventoryStacking.Plugin.Patch_WorldObjectsHandler_DropOnFloorImplentation | **Verbo novo:** o patch faz aritmética; o u_patch precisaria de um verbo "add/sub" |

## Ideia para o veredito (b): u_dump como solução

O projeto já tem o `u_dump`, que despeja no celular os tipos e métodos do jogo
IL2CPP. O tradutor poderia confirmar o alvo contra esse despejo em vez do
`Assembly-CSharp`. Isso resolveria o limite porque o `u_dump` roda no celular e
tem acesso ao assembly do jogo. O que faltaria: o tradutor precisaria ler o
`dump.tsv` gerado pelo `u_dump` e confirmar o alvo contra ele. Isso é viável
mas requer mudança no tradutor.

## Licença

O corpus foi baixado de fontes públicas (GitHub) com licença que permite
baixar. Os .dll de terceiros NÃO foram commitados no repo. O manifest
(`test/fixtures/dll_corpus/MANIFEST.tsv`) contém nome, URL, licença, sha256,
jogo e backend de cada mod.

**Contagem de licenças no MANIFEST:**
- Apache-2.0: 22 (Planet Crafter + Dyson Sphere Program)
- MIT: 6 (Darkwood_Customizer, FFPR_Fix, Magicite, MiSide_KappiMod, TLD_PrepperCache, TLD_QualityOfLife)
- BSD-2-Clause: 1 (Tunic_Translation)
- desconhecido: 1 (PotionCraft_EnableDev)

## Conclusão

**0/378 patches traduzidos (0%).** O tradutor é estrito demais por projeto: o
alvo mora no `Assembly-CSharp` do jogo, que não vem junto do mod. Para traduzir
mods reais, o tradutor precisaria do assembly do jogo ou de uma forma de
confirmar o alvo sem ele (o que é arriscado). A ideia de usar o `u_dump` é
viável mas requer mudança no tradutor.
