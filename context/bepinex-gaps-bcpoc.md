---
name: bepinex-gaps-bcpoc
description: Gap analysis BepInEx PC vs zygisk-bc-poc — 3 gaps pedidos (watch por chave, dependência entre mods, bridge IL2CPP) com implementação real do BepInEx (arquivo:linha) e veredito de port pra Android nativo
metadata:
  type: project
  status: documented
---

# Gap analysis: BepInEx PC vs zygisk-bc-poc

**Data:** 2026-09-15
**Fonte:** fork NextBep local (`~/battlecats-mods/nextbep/BepInEx.Android/`, greps com line numbers desta sessão) + estado do zygisk-bc-poc verificado no disco.
**Pausado:** pesquisa ADB/Termux (retoma depois).

## 0. O que já cobrimos (linha de partida)

- Hook install/unpatch/repatch por alvo (`PLANS[]`, backup/restore)
- `list_patches` = paridade com `Harmony.GetPatchedMethods()` (seq-gated via companion)
- Hot-reload de config com guard no-op (seu fix) + parse tipado (bool/int/enum com clamp)
- Overhead tracking em tempo real (`hook_overhead`, 0.26 µs medido — BepInEx não tem built-in)

---

## 1. Gap 1 — Watch por chave individual (ConfigEntry.SettingChanged)

### Como o BepInEx faz (fonte real)

Duas camadas de evento:
- **Por arquivo**: `ConfigFile.SettingChanged` (evento da classe toda) — `ConfigFile.cs:590`
- **Por chave**: `ConfigEntryBase` assina o evento do arquivo e **filtra por identidade**: `if (args.ChangedSetting == this) SettingChanged?.Invoke(sender, args)` — `ConfigEntryBase.cs:22-24`; o evento individual é declarado em `:55`
- Disparo central: `ConfigFile.OnSettingChanged` (`:592-611`) — **auto-save antes de notificar** (`SaveOnConfigSet`, `:596-597`), e **try/catch por callback** (`:603-610`): um handler ruim não derruba os outros nem o setter. Cada handler ruído vira `Logger.Log(Error)` e o loop continua.

### Estado nosso

O reload aplicando delta já existe (seu guard compara campo a campo), mas **não existe callback por chave**: consumidor (ex.: `throttle_every` → g_throttle_every) aprende o valor novo só na próxima leitura passiva, sem notificação.

### Port pra Android nativo — VEREDITO: PORTAR (barato)

- Caberia no `HookCallbacks` que **já existe** (`main.cpp:361`, `g_hook_callbacks[N_PLANS_MAX]`): tabela estática `on_change[key] → fn(cfg_slot, old, new)` chamada no seu guard, **só nas chaves em delta** — exatamente a semântica `SettingChanged`.
- Copiar o **try/catch por callback** (o nosso é C puro: proteger com flag de erro e seguir pro próximo; um callback ruim não pode derrubar o reload nem os demais).
- Custo: ~50 linhas. Zero risco arquitetural (não toca em hook, socket nem mount).
- O que NÃO copiar: auto-save no setter (`SaveOnConfigSet` no hot path do reload; nosso `save_mods_conf` já é atomic-rename no caminho do companion — escrever de novo no módulo é redundância).

---

## 2. Gap 2 — Dependência e ordem de load entre mods

### Como o BepInEx faz (fonte real)

- Declaração: `[BepInDependency(GUID, DependencyFlags.HardDependency|SoftDependency)]` — `Attributes.cs:83,89-99,109`
- Ordem: `BaseChainloader.ModifyLoadOrder` — `SortedDictionary` pra ordenação estável (`BaseChainloader.cs:222-224`) → coleta `Dependencies.Select(d => d.DependencyGUID)` (`:266`) → **`Utility.TopologicalSort`** (`:304-310`)
- O sort **joga exceção com a lista do ciclo**: `"Cyclic Dependency:\r\n - x\r\n - y"` (`Utility.cs:129-148`) — ciclos = erro fatal de boot, não "ordem aleatória"
- Semântica de falha (`BaseChainloader.cs:356-379+`): dependência **Hard** ausente → plugin **não carrega** (+ `DependencyErrors`); **Soft** → carrega mesmo faltando
- Validação de versão de dependência no mesmo fluxo.

### Estado nosso

"Hooks fixos + conf" não é um sistema de mods — não há nada entre o que dependeria de quê. O primeiro caso real de dependência nasce quando existir **mais de um consumidor por hook** (ex.: dois módulos de gameplay no mesmo `appUpdateDraw`).

### Port — VEREDITO: NÃO PORTAR AGORA (não tem demanda real)

- Motivo honesto: topological sort sem nós reais é código morto. BepInEx resolve um problema que nós **ainda não temos**.
- Gatilho concreto de reversão: quando existir ≥2 consumidores no mesmo hook, portar o par **TopologicalSort + Hard/Soft** (o algoritmo é ~40 linhas, port direto; `HardDependency` = consumidor não registra se o "patch base" não instalou).
- O que guardar desde já: **declaração estática de dependência no schema** (`bc_mods_conf.h`: campo `requires` opcional por entrada) — se um dia crescer, a declaração já existe e o sort entra em cima dela.
- Riscos que o BepInEx aceita e nós não precisamos: GUID global de plugin, versionamento de contrato, unload por plugin (nenhum disso existe aqui — e não deveria existir sem sistema de mods real).

---

## 3. Gap 3 — API de interop estilo IL2CPP bridge

### Como o BepInEx faz (fonte real)

Pipeline **de 3 programas** (csproj do BepInEx.Unity.IL2CPP, lido):
1. **Cpp2IL** (`Samboy063.Cpp2IL.Core`) — lê o binário do jogo (global-metadata.dat + libil2cpp.so) e regenera "assemblies" C# stub;
2. **Il2CppInterop.Generator** — gera proxies C# das classes do jogo;
3. **Il2CppInterop.Runtime** (com `HarmonySupport`) — em runtime, faz o glue GC interop (objeto C# ↔ objeto il2cpp), e permite Harmony patchear métodos il2cpp via os proxies.

Custo medido no próprio fork: `Il2CppInteropManager.cs:322` e `:396` — **Stopwatch no preload de assemblies**; relato do log real: "Cpp2IL finished in ..." na ordem de **segundos a minutos por versão do jogo** (one-shot no boot). É pesado: consome o global-metadata.dat inteiro.

### Estado nosso

Não temos bridge — e **não precisamos**: Battle Cats é **C++ puro (libcocos2dcpp.so / libnative-lib.so), sem IL2CPP** (verificado forense em `nextbep-bepinex-android-analysis.md`: alvo real são os 58 exports JNI + CSV/packs). O bridge IL2CPP existe pra resolver um problema que não é o nosso.

### Port — VEREDITO: NÃO PORTAR (arquiteturalmente errado pro alvo)

- A função *equivalente* ao bridge no nosso mundo já está de pé: **offset DB por build-id + assinaturas com wildcard** (`selftest_symbol`, fallback de assinatura de bytes). O "metadata" do Cpp2IL é o nosso offset DB; o "proxy" do Il2CppInterop é o nosso `HookPlan`.
- O que **vale extrair do conceito**: o BepInEx transforma "binário desconhecido" em "API tipada" uma vez por versão. Nossa versão disso = **companion empurra o offset DB novo por socket quando o build muda** (offsets OTA — já listado como item #2 no roadmap de capacidades mobile-only). Isso é o análogo honesto, custa ~1 dia, e nenhuma arquitetura Zygisk é tocada.
- Portar o pipeline de verdade (parse de global-metadata, GC interop) seria portar solução pra problema de outro jogo. Se um dia o alvo for um jogo IL2CPP, o caminho correto é **injetar o NextBep real** (que já roda em Android IL2CPP — é o que o fork é), não re-implementar bridge dentro do bc-poc.

---

## 4. Resumo executivo

| Gap | Custo de port | Veredito | Quando |
|---|---|---|---|
| Watch por chave (SettingChanged) | ~50 linhas no guard existente | **PORTAR** | próxima rodada de código |
| Dependência/ordem entre mods | ~40 linhas + schema `requires` | **NÃO AGORA** — sem nós reais p/ sort; gatilho: 2º consumidor por hook | quando houver demanda |
| Bridge IL2CPP | enorme (3 pipelines) | **NÃO** — alvo é C++ puro; análogo nosso = offsets OTA por socket | quando alvo IL2CPP (aí usa NextBep direto) |

Nenhum dos 3 quebra a arquitetura Zygisk atual: o único recomendado agora é aditivo (callbacks no guard que já existe).

## 5. Fontes

- `BepInEx.Core/Configuration/ConfigEntryBase.cs:22-24,55` (fan-out filtrado por identidade da entry)
- `BepInEx.Core/Configuration/ConfigFile.cs:590,592-611` (OnSettingChanged: auto-save + try/catch por callback)
- `BepInEx.Core/Contract/Attributes.cs:83-109,148,165` (BepInDependency, Hard/Soft flags)
- `BepInEx.Core/Bootstrap/BaseChainloader.cs:222-224,266,304-310,356-379` (SortedDictionary + TopologicalSort + semântica Hard/Soft + DependencyErrors)
- `BepInEx.Core/Utility.cs:129-148` (TopologicalSort com exceção "Cyclic Dependency")
- `Runtimes/Unity/BepInEx.Unity.IL2CPP/Il2CppInteropManager.cs:18-19,322,396` (Cpp2IL + Stopwatch no preload)
- `Runtimes/Unity/BepInEx.Unity.IL2CPP/BepInEx.Unity.IL2CPP.csproj:19-26` (Il2CppInterop.Generator/Runtime/HarmonySupport + Samboy063.Cpp2IL.Core)
- Estado bc-poc: `jni/main.cpp:361,382,485-489` (HookCallbacks já existente), schema `main.cpp:83-88`
