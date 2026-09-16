---
name: bepinex-chainloader-vs-bcpoc
description: BepInEx Chainloader (dependency resolution) vs bc-poc PLANS[] fixo — comparação e o que é aplicável
metadata:
  type: context
---

# Chainloader BepInEx vs bc-poc — como cada um carrega e ordena hooks/plugins

Fonte: código-fonte real do BepInEx (`github.com/BepInEx/BepInEx`, ramo `master`)
— `BepInEx.Core/Bootstrap/BaseChainloader.cs` e `Contract/Attributes.cs`.
Comparado com o nosso `~/battlecats-mods/zygisk-bc-poc/jni/main.cpp`.

---

## 1. Como o BepInEx Chainloader descobre/ordena/carrega plugins

### Descoberta (discovery) — `DiscoverPluginsFrom` → `TypeLoader.FindPluginTypes`
- Varre a pasta `BepInEx/plugins/`, **escaneia assemblies .NET** e bate cada tipo
  contra `ToPluginInfo()`.
- `ToPluginInfo` só aceita tipo que (a) não é interface/abstrato, (b) herda de
  `BasePlugin`, (c) tem atributo `[BepInPlugin(GUID, Name, Version)]`.
- Rejeita com `Log(LogLevel.Warning, "Skipping...")` se GUID malformado
  (regex `^[a-zA-Z0-9\._\-]+$`), versão nula, nome nulo, ou se **não** herda do
  plugin base (catch `AssemblyResolutionException` → assume "não é plugin").
- Processo é **scan em disco + metadados**, não array hardcoded.

### Ordenação / load order — `ModifyLoadOrder`
- Agrupa plugins por `Metadata.GUID`; **GUID duplicado → o segundo é pulado**
  (`Log Warning "Skipping [guid] because a plugin with the same GUID..."`).
- Constrói `SortedDictionary<string, IEnumerable<string>>` (case-insensitive
  por GUID) → dependência = **topológica por GUID**, com ordem determinística
  garantida pelo sort do dicionário.
- `DependencyErrors` lista os que **não** carregaram (dependência não resolvida),
  mas o chainloader **segue** (não derruba tudo).

### Atributos de contrato — `Contract/Attributes.cs`
- `[BepInPlugin(GUID, Name, Version)]` — identidade obrigatória.
- `[BepInDependency(GUID, BepInDependency.DependencyFlags.SoftDependency |
  HardDependency)]` — declaração de dependência **explícita**; HardDependency não
  resolvida bloqueia o load do dependente, SoftDependency apenas avisa.
- `[BepInProcess("game.exe")]` — filtro de processo alvo: o plugin **só** carrega
  se o processo atual bate com o nome declarado.
- `[BepInIncompatibility(GUID)]` — incompatibilidade declarada.

### Resolução de versão
- `PluginTargetsWrongBepin` compara a versão do `BepInEx.Core` referenciada pelo
  plugin contra a versão corrente (Major/Minor/Build) — plugin feito pra versão
  muito nova é recusado.

---

## 2. Como o bc-poc carrega hooks

`main.cpp`:
```cpp
static HookPlan *PLANS[] = { &hooks_appinit, &hooks_updatedraw,
                             &hooks_apptouch, &hooks_appkey };
static const int N_PLANS = 4;
```
- **Array fixo em tempo de compilação** — os 4 hooks (appInit/appUpdateDraw/
  appTouch/appKey) existem literalmente no código. Nada é descoberto em runtime
  de disco/metadados.
- **Sem dependência entre hooks** — `install_all()` itera `PLANS[]` na ordem do
  array e chama `try_install()` em cada um, com isolamento total por hook
  (Pattern 11: falha de um não derruba os outros). Independência = ok aqui,
  porque os 4 hooks são funções JNI independentes.
- **Ordem fixa pelo array** — `PLANS[]` é explicitamente ordenado (appinit →
  updatedraw → apptouch → appkey), nada configuravel em runtime.
- **Gate por hook** típico do bc-poc: `sdk_min` (requer Android 5.0+) e
  `g_mod_enabled`/`hook_enabled()` (config `bc_mods.conf`, toggled no Termux).
- **Self-test por hook** (`selftest_symbol`): cascata RVA → símbolo → assinatura
  de prólogo; falha → hook DORMANT (mas não bloqueia os demais).

---

## 3. Diferenças reais

| Aspecto | BepInEx Chainloader | bc-poc PLANS[] |
|---|---|---|
| Descoberta | Scan em disco de assemblies + metadados | Hardcoded, 4 entries no array |
| Identidade | `[BepInPlugin]` GUID único obrigatório | `shortname` string fixa por plan |
| Dependência entre plugins/hooks | `[BepInDependency]` explícita, topológica | **Não existe** — hooks são independentes |
| Ordem de carga | `SortedDictionary` (determinístico, configurável) | Fixa pela declaração do array |
| Filtro de processo alvo | `[BepInProcess]` | Equivalente via `is_bc()` + `DLCLOSE_MODULE_LIBRARY` |
| Resolução de versão | GUID/versão BepInEx.Core check | Build-id gate (`BC_BUILD_ID`) |
| Falha de um item | Plugin pula, guuía melhorado (DependencyErrors) | Hook DORMANT, outros seguem (Pattern 11) |
| Config runtime | `BepInEx.cfg` | `bc_mods.conf` (Termux) |

---

## 4. O que do Chainloader real é APLICÁVEL ao bc-poc

Relevante porque hoje o bc-poc é um único módulo; se um dia virar "loader de
vários hooks/plugins independentes", estes conceitos transplantam direto:

1. **Dependência entre hooks** — nada hoje, e os 4 não precisam. MAS se surge um
   hook que depende de outro ser instalado antes (ex.: hook que altera estado
   usado por um segundo hook), a topologia do BepInEx (dependência → ordenar
   dependente depois do dependido) é o padrão a copiar. Hoje seria um vetor de
   "ordem" por dependência em vez da ordem fixa do array.

2. **Impacto de GUID/metadata** — o bc-poc não tem GUID/identidade por hook.
   Se o intuito é serializar/repudiar via `bc_mods.conf` por nome, `shortname` já
   serve; mas registrar uma identidade/hook-id estável ajudaria a manter estado
   entre rebuilds e no log (fonte única).

3. **Filtro de processo** — o bc-poc já cobre via `is_bc()`+`DLCLOSE`; o
   `[BepInProcess]` do BepInEx é o mesmo conceito, já resolvido de forma melhor
   (mais cedo, no specialize).

4. **Ordem de carga configurável** — BepInEx tem o `ModifyLoadOrder` virtual que
   permite a um host reordenar. No bc-poc seria um possível `PLANS[]` reordenável
   em runtime via config, em vez de array estático. Baixo valor hoje (4 hooks
   independentes), mas é o único "gap" arquitetural real entre os dois.

5. **NÃO transplantável**: scan em disco de assemblies .NET / Mono.Cecil — o
   bc-poc hooka um binário nativo já carregado (Zygisk + Dobby), não carrega
   plugins .NET de uma pasta. A "descoberta" dele é `dl_iterate_phdr` +
   `selftest_symbol`, não reflexão de metadata.

---

## 5. Resumo (1 parágrafo)

O BepInEx Chainloader resolve dependências por GUID via topologia determinística
(scan em disco + atributos `[BepInDependency]`/`[BepInProcess]`), enquanto o
bc-poc instala hooks de um **array fixo sem dependência** (`PLANS[]`), com
isolamento total por hook e self-test em cascata. Os 4 hooks do bc-poc são
independentes, então a ausência de dependency-resolution é **correta hoje**; o
único conceito realmente transplantável do BepInEx é **ordem de carga
configurável** (dependência/prioridade explícita) se algum dia um hook passar a
depender de outro. O scan de assemblies e o sistema de GUID não se aplicam ao
modelo native-hook do Zygisk/Dobby.