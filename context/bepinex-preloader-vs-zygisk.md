# BepInEx Preloader vs Zygisk preAppSpecialize — injecção pré-execução

Pesquisa real (fontes primárias do repo oficial `BepInEx/BepInEx`, master):
`BepInEx.Preloader.Core/Patching/AssemblyPatcher.cs` e
`BepInEx.Core/Bootstrap/BaseChainloader.cs` (lidos nesta sessão).

## BepInEx.Preloader — como injeta antes do jogo

**Mecanismo real (fontes):**
- `AssemblyPatcher` (Preloader) é o worker que carrega assemblies alvo via
  **Mono.Cecil** (`AssemblyDefinition.ReadAssembly`), roda patchers, e só
  então carrega o assembly já modificado em memória
  (`assemblyLoader(assemblyStream.ToArray(), path)` → `Assembly.LoadFrom`).
- Patchers são descobertos por atributos (`PatcherPluginMetadata`,
  `TargetAssemblyAttribute`/`TargetTypeAttribute`) e aplicados ANTES do
  assembly do jogo ser carregado no AppDomain. `BaseChainloader` roda
  DEPOIS — resolve plugins, dependências e faz load dos plugins BepInEx
  sobre o assembly já patchado.
- `PlatformUtils.SetPlatform()` no Preloader detecta Android
  (`Directory.Exists("/data") && File.Exists("/system/build.prop")`) —
  prova que BepInEx lida com runtime Android (IL2CPP/Unity/Mono).

**O que garante:**
- O patch acontece **antes do código do jogo rodar** (o assembly nunca é
  carregado sem o patch — é lido, patchado em memória, e só então
  `Assembly.Load`).
- Pode alterar IL de QUALQUER método do assembly, inclusive estáticos e
  ctor — porque é reescrita de bytecode, não hook de runtime.
- Precisa de acesso a escrita na pasta do jogo/APK (patchers vivem em
  `BepInEx/patchers/`) e de um entry point externo pra invocar o Preloader
  antes do main do jogo (winhttp/doorstop em Windows; em Unity Linux o
  `doorstop`/`preload` via LD_PRELOAD, ou o launcher do BepInEx).

## Zygisk preAppSpecialize (nosso approach)

**Mecanismo real (nosso módulo):**
- O zygote faz fork do processo do app; Zygisk nos chama em
  `preAppSpecialize` **antes** da especialização terminar (antes do
  sandbox se aplicar, do uid trocar, do classloader rodar).
- `postAppSpecialize` roda com sandbox já aplicado. Nosso módulo espia
  `libnative-lib.so` carregar (via `dl_iterate_phdr` polling) e instala
  hook de runtime via **Dobby** (inline hook em símbolo JNI exportado).

**O que garante:**
- Roda **antes do código do app** (do processo) — ganha a janela de
  privilégio pré-sandbox.
- Interceptação é **runtime hook** (Dobby inline, `dladdr`/RVA-check),
  não reescrita de IL — não precisa tocar em arquivo do jogo.
- Pode carregar libs próprias no processo antes do app decidir qualquer
  coisa (aqui: detectar `libnative-lib.so` e interceptar JNI).

## Equivalência conceitual

| | BepInEx.Preloader | Zygisk preAppSpecialize |
|---|---|---|
| Ponto de injeção | antes do assembly do jogo carregar (no AppDomain) | antes da especialização do processo terminar (no zygote fork) |
| Tipo de patch | reescrita de IL (Mono.Cecil) em memória | inline hook runtime (Dobby) no `.so` nativo |
| Depende de acesso a arquivos do jogo | sim (patchers + pasta BepInEx) | não (in-memory, Zygisk faz load da lib do módulo) |
| Garantia temporal | assembly nunca carrega sem patch | código nativo do jogo ainda não rodou no momento do hook |
| Limite | só .NET/Mono assemblies gerenciados | só código nativo/JNI (JNI exports) |

**Equivalência**: ambos são **injeção pré-execução** — código do jogo ainda
não rodou quando o loader age. Mas a garantia difere no **nível**:
BepInEx reescreve IL antes do CLR tocar o assembly (garantia total de
"patch visível pro jogo"); Zygisk/Dobby hooka no primeiro momento em que o
símbolo existe em memória (garantia de "hook ativo antes do uso", mas
sujeito a janela entre carregamento do `.so` e primeira chamada JNI).

**Conclusão**: conceitualmente equivalentes (pre-execução), tecnicamente
complementares — um é estático/IL, o outro dinâmico/nativo. Para o
BepInEx mobile em Unity Android, o Preloader é quem garantiria o
Chainloader antes do assembly do jogo; Zygisk dá a injeção nativa pré-
sandbox. Não substituem um ao outro em domínio (managed vs native).

Fonte: repo `BepInEx/BepInEx` master — `AssemblyPatcher.cs`,
`BaseChainloader.cs`, `PlatformUtils.cs` (lidos nesta sessão).