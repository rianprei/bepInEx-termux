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

## 3 patches reais do corpus

### 1. dsp-AddFuelStar.dll — AddFuelStar.AddFuelStar.GameTick

**O que faltou:** o tipo alvo `AddFuelStar` não está na DLL (morar no Assembly-CSharp do jogo).

**IL resumido:** `nop ldsfld ldc.i4.0 ceq stloc.0 ldloc.0 brfalse nop ldarg.0 ldfld stloc.1 ...`

### 2. dsp-BiggerSeed.dll — BiggerSeed.BiggerSeed.BigSeed

**O que faltou:** o tipo alvo `BiggerSeed` não está na DLL.

**IL resumido:** `nop ldarg.0 stloc.0 ldc.i4.m1 conv.i8 stloc.1 ldarg.0 call ldstr callvirt callvirt stloc.2 ...`

### 3. dsp-ChangeSun.dll — ChangeSun.ChangeSun.GameTick

**O que faltou:** o tipo alvo `ChangeSun` não está na DLL.

**IL resumido:** `nop ldsfld ldc.i4.0 ceq stloc.0 ldloc.0 brfalse nop ldarg.0 ldfld stloc.1 ldsfld callvirt ldc.i4.0 cgt stloc.2 ...`

## Modo medição (sem conferir no jogo)

Se o tradutor não exigisse que o tipo alvo esteja na DLL (usando apenas o nome
do tipo e do método do atributo), quantos patches seriam traduzidos?

**Resposta:** não medido (requer mudança no tradutor). O risco de traduzir sem
conferir é alto: o tradutor não pode confirmar overload ambíguo, tipo de
retorno, etc. Isso pode gerar patches incorretos que alteram o método errado.

## Top 5 padrões não cobertos

| Padrão | Exemplo | Veredito |
|---|---|---|
| Prefix/Postfix com `__result = CONST` | dsp-AddFuelStar.GameTick | **Limite de projeto:** precisa do assembly do jogo para confirmar o alvo |
| Postfix com `__result *= K` | dsp-BiggerSeed.BigSeed | **Limite de projeto:** precisa do assembly do jogo |
| Atribuição estática | dsp-ChangeSun.GameTick | **Limite de projeto:** precisa do assembly do jogo |
| Classe aninhada | (3 casos) | **Verbo novo:** o u_patch precisa localizar classes aninhadas |
| Tipo string | dsp-BiggerSeed.BigSeed | **Impossível sem runtime .NET:** o u_patch não suporta string |

## Licença

O corpus foi baixado de fontes públicas (GitHub, Thunderstore) com licença que
permite baixar. Os .dll de terceiros NÃO foram commitados no repo. O manifest
(`test/fixtures/dll_corpus/MANIFEST.tsv`) contém nome, URL, licença, sha256,
jogo e backend de cada mod.

## Conclusão

**0/378 patches traduzidos (0%).** O tradutor é estrito demais por projeto: o
alvo mora no `Assembly-CSharp` do jogo, que não vem junto do mod. Para traduzir
mods reais, o tradutor precisaria do assembly do jogo ou de uma forma de
confirmar o alvo sem ele (o que é arriscado).
