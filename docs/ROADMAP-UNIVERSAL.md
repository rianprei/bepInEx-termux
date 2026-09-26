# bepInEx-termux — Roadmap: loader universal e usável por gente normal

Pedido do usuário (2026-09-26): "quero que o nosso bepin-termux seja universal"
+ "tem que ser funcional plenamente pra pessoas normais [...] pode ser igual a
um lucky patcher? tem que ser usável".
Base: `7cfb29c` (v0.3.6-generic2), branch `feat/generic-pkg-mods`.

## Objetivo

Pessoa com celular rooteado (Magisk/KernelSU + Zygisk), sem saber programar:

1. Instala **um zip no Magisk** e reinicia. O app **bepInEx Manager** aparece sozinho.
2. Abre o Manager e vê os jogos instalados, cada um com o engine detectado.
3. **Instala um mod** abrindo um arquivo `.bmod` (baixado, recebido no WhatsApp etc.), liga ou desliga com um toque e ajusta as opções com slider.
4. **Cria um mod sem código**: "Escanear jogo" → busca classe/método → "sempre verdadeiro", "sempre retornar N" ou "multiplicar por N" → salva e compartilha o `.bmod`.
5. Toca em "Reiniciar jogo" e o mod está ativo. Se der erro, o Manager mostra em português o que falhou.

Nada modifica APK, OBB ou arquivos do jogo: tudo acontece em runtime (regra dura do usuário).

**Pronto quando:** num celular limpo, o usuário faz o fluxo 1→5 no **TABS Pocket Edition** (nunca testado) e no **SA2**, sem adb e sem terminal. Battle Cats continua funcionando.

## Status validado no device (POCO C75, HyperOS, Android 16)

| Fase | Estado | Evidência |
|---|---|---|
| F1 zero-config + log C1 | merged, validado | 2026-09-26: SA2 sem allowlist carrega mods; `log.txt` escrito; BC 4/4 |
| F1b `BEPINEX_PKG` | merged, validado | u_patch/u_dump leem o pacote certo |
| F1c SELinux | merged, validado em Enforcing | v0.4.0 `setenforce 1`: SA2 (Dobby, u_dump) + BC 4/4, sem `avc` do nosso caminho |
| sinais Termux→BC por seq | merged, **teste de device pendente** | `feeaab0` |
| F1d crashguard | merged, **teste de device pendente** | `dd6746b`/`faf0afd` |
| F2 SDK | merged | `new_mod`/`pack_bmod` testados no host |
| F3 u_dump | merged, validado | SA2: 162.804 linhas, `unity=6000.3.13f1`, Permissive e Enforcing |
| F4 u_patch | branch, em correção | `return` bool aplicou e foi chamado 12x (Frida); revisão achou encoding errado em mul/float |
| F5/F6 Manager | branch, em correção | APK compila; faltam chcon, field, C7, frida, crashguard |
| F7 zip | merged | v0.4.1 determinístico (`d34b709`); instalação do formato novo pendente |
| F8 docs | branch, em correção | 8 achados de revisão |
| F9b u_noads | branch, em correção | cobertura zero no SA2 → adapters AudienceNetwork/Metica |
| F11 u_frida | branch | teste de device pendente |
| T2 verify_all | branch, em revisão | |

## Diferença honesta pro Lucky Patcher

| | Lucky Patcher | bepInEx-termux |
|---|---|---|
| Root | opcional (sem root ele patcha o APK) | **obrigatório** (Zygisk) |
| Modifica o APK | sim (quebra a assinatura, o login Google e updates) | **nunca** |
| Mod | patch de bytes/APK | hook em runtime + mod nativo `.so` |
| Criar mod | patches prontos | Mod Maker (sem código) + SDK C++ (avançado) |

Sem root não existe caminho: o modelo container (NextBep) é só pra acompanhar, fora de escopo.

**Escopo ético:** jogos offline/single-player. Não entra preset nem recurso pra burlar compra (IAP), licença ou verificação de pagamento, nem nada pra PvP online com anti-cheat (Clash Royale e similares).

## Mapa atual → alvo

```
ATUAL                                   ALVO
zygote preAppSpecialize(pkg)            zygote preAppSpecialize(pkg)
 ├ BC hardcoded (bc_mods/, companion)    ├ BC hardcoded (intacto até F10)
 ├ allowlist + mods/<pkg>/ → dlopen .so  ├ mods/<pkg>/ existe → dlopen *.so   (F1, sem allowlist)
 ├ allowlist sem pasta → Cocos log hook  ├ allowlist sem pasta → Cocos log hook (legado)
 └ resto → DLCLOSE                       └ resto → DLCLOSE
                                        mods/<pkg>/ contém:
                                          <id>.so        mod nativo (SDK C++)
                                          u_patch.so     motor declarativo (Mod Maker)  F4
                                          <id>.patch     regras declarativas            F4
                                          u_dump.so      scanner (pedido pelo Manager)  F3
                                          <id>.conf      opções key=value
                                          <id>.json      manifest (pro Manager listar)
                                        bepInEx Manager (APK, root)                     F5-F6
                                        zip Magisk que instala o Manager                F7
```

## Contratos (fixos — todos os agentes seguem)

### C1. Diretórios no device
- Mods: `/data/local/tmp/mods/<pkg>/` (dono root, `755`; arquivos `644`). Escrito **só pelo Manager (via su) ou adb**.
- Mod desligado: sufixo `.off` (`foo.so.off`, `foo.patch.off`). O loader só carrega `*.so` (`bc_loader_is_mod_filename`).
- Saída do processo do jogo (o jogo não escreve em `/data/local/tmp`): `/data/data/<pkg>/files/bepinex/`
  - `log.txt`: log de todos os mods (append, 1 linha = `HH:MM:SS [mod] msg`, corta em 256KB).
  - `dump.tsv`: saída do u_dump.
  - Descobrir `<pkg>` dentro do mod: `getenv("BEPINEX_PKG")`, que o loader seta antes do `dlopen`. **Não** use `/proc/self/cmdline` no constructor: no device ele ainda vale `zygote64` nesse momento (achado 2026-09-26, u_patch leu `mods/zygote64`). Fallback só se a env faltar: cmdline, esperando sair de `zygote*`.

### C2. `.bmod` (zip)
```
manifest.json
mod.so        (type=native, arm64-v8a)  |  mod.patch  (type=patch)
```
`manifest.json`:
```json
{
  "format": 1,
  "id": "sa2-infinite-ammo",
  "name": "Munição infinita",
  "version": "1.0",
  "author": "fulano",
  "description": "Nunca acaba a munição.",
  "game": "com.hyperdotstudios.swampattack2",
  "engine": "unity-il2cpp",
  "type": "patch",
  "options": [
    {"key": "mult", "label": "Multiplicador", "type": "float", "default": 2, "min": 1, "max": 10}
  ]
}
```
- `id`: `[a-z0-9-]{3,48}`, vira nome de arquivo. `game`: pacote ou `"*"` (qualquer jogo do mesmo engine).
- `engine`: `unity-il2cpp` | `unity-mono` | `cocos2dx` | `native`.
- Option `type`: `bool` | `int` | `float` | `choice` (com `"choices": [..]`).
- Instalação: `mod.so` → `<id>.so`, `mod.patch` → `<id>.patch`, manifest → `<id>.json`, opções → `<id>.conf`.

### C3. `.conf` (opções)
Uma linha `key=value`, `#` é comentário. Escrito pelo Manager, lido pelo mod no boot.

### C4. `.patch` (motor declarativo u_patch)
Uma regra por linha, campos separados por espaço, `#` é comentário:
```
return  <Classe>  <Método>  <nargs>  <bool|int|float>  <valor>
mul     <Classe>  <Método>  <nargs>  <int|float>       <fator>
static  <Classe>  <campo>   <bool|int|float>           <valor>
field   <Classe>  <campo>   <bool|int|float>           <valor>  [<Método> <nargs>]
```
- `field` = campo de **instância**: a cada chamada de `<Método>` (instância, da mesma `<Classe>`), escreve `this.<campo> = <valor>` antes de rodar o original (hook com thunk, `this` = x0, offset via `field_get_offset`). Sem `<Método>`, o u_patch escolhe sozinho até 8 métodos de instância da classe que passam na guarda de tamanho. Motivo (teste no device 2026-09-26): `return ComplexCreature HasAmmo 0 bool true` aplicou e foi chamado 12x (Frida), mas a munição acabou mesmo assim, porque o jogo decrementa e checa o campo direto; o que dá munição infinita é o campo `WeaponInfo.unlimitedAmmo` (o sa2ammo usa ele). Método patchado não cobre lógica que lê campo direto.
- `<Classe>` = `Namespace.Nome`, ou só `Nome` sem namespace (o último `.` separa). Classe aninhada fica fora do v1.
- `<valor>`/`<fator>` pode ser `$key`, e aí vem do `<id>.conf`.
- Busca em todos os assemblies (`domain_get_assemblies` + `class_from_name`).
- Exemplo (SA2): `return ComplexCreature HasAmmo 0 bool true`.
- Regra que não resolve vira um log claro (`classe X não encontrada`) e não derruba nada.

### C5. `dump.tsv` (u_dump)
Cabeçalho `# pkg=<pkg> il2cpp_size=<bytes> unity=<versão se achar>`, depois:
```
C  <assembly>  <Namespace.Classe>
M  <Namespace.Classe>  <método>  <nargs>  <tipo_retorno>  <static 0|1>
F  <Namespace.Classe>  <campo>  <tipo>  <static 0|1>  <offset>
```
Separador TAB. Classe aninhada: `Namespace.Externa/Interna` (sobe `class_get_declaring_type` até a raiz). Gera se não existir. Pra refazer, o Manager apaga o arquivo e reinicia o jogo.

### C6. Detecção de engine (Manager, sem abrir o jogo)
Lista `lib/arm64-v8a/` no APK base + splits (`ApplicationInfo.sourceDir` + `splitSourceDirs`, via `ZipFile`) ou em `nativeLibraryDir`:
- `libil2cpp.so` → `unity-il2cpp`
- `libmonobdwgc-2.0.so` / `libmono*.so` → `unity-mono`
- `libcocos2d*.so` ou `libcocos*` → `cocos2dx`
- `libUE4.so` / `libUnreal.so` → `unreal`
- `libgodot_android.so` → `godot`
- senão → `native`/`java`

### C7. Aceitar mod de qualquer origem (detecção automática no Manager)
O usuário escolhe um arquivo qualquer + o jogo. O Manager identifica o tipo pelo **conteúdo** (magic/cabeçalho), não pela extensão, e diz em português se roda e como.

| Tipo detectado | Como detecta | Roda? | Caminho |
|---|---|---|---|
| `.bmod` | zip com `manifest.json` | sim | C2 |
| `.so` Android arm64 | ELF, `e_machine=183` (AArch64) | sim | copia pra `mods/<pkg>/` |
| `.so` 32-bit/x86 | ELF de outra arquitetura | não | "feito pra outra arquitetura" |
| `.patch` | texto nas regras C4 | sim | u_patch |
| script Frida `.js` | texto JS (`Interceptor`, `Il2Cpp.perform`, `Java.perform`) | sim (F11) | frida-gadget em modo script |
| `.dll` .NET que referencia `Il2CppInterop`/`UnhollowerBaseLib` (BepInEx 6 IL2CPP / MelonLoader IL2CPP) | PE + CLI header + AssemblyRefs | depois (F12) | runtime .NET no processo |
| `.dll` .NET Mono (BepInEx 5 / MelonLoader Mono, ex: mods do TABS PC) em jogo **Unity Mono** | AssemblyRefs sem Il2Cpp* + engine `unity-mono` | depois (F13) | mono_* + Harmony |
| `.dll` .NET Mono em jogo **IL2CPP** | idem + engine `unity-il2cpp` | **não automático** | explica e sugere Mod Maker/port |
| `.dll` Windows nativo, `.dylib` iOS, `.CT` Cheat Engine, `.exe` | PE sem CLI / Mach-O / XML CE | não | explica o porquê (outra plataforma/binário) |
| `.lua` GameGuardian | texto com `gg.` | depois (F10) | — |

Nenhum caminho modifica arquivo do jogo: tudo é carregado no processo em runtime.

## Garantias (pedido do usuário: "perfeito, imutável, inquebrável, indestrutível")

"Inquebrável" de verdade não existe: o jogo atualiza, o Android muda, o anti-cheat reage. O que dá pra garantir, e **é obrigatório**:

| Garantia | Como é garantida |
|---|---|
| **G1. Mod nunca derruba o jogo pra sempre** | F1d: proteção contra crash em loop. 2 mortes seguidas < 60s depois de carregar mod ⇒ o loader não carrega mais mods daquele jogo até o usuário reativar. |
| **G2. Mod nunca derruba o celular** | Loader só atua em app com pasta de mods; o resto recebe `DLCLOSE`. Magisk safe mode desliga o módulo se o boot falhar. |
| **G3. Jogo atualizado não quebra, só desliga o mod** | Tudo resolvido por nome em runtime; não achou ⇒ log claro e segue. Nada de offset fixo. |
| **G4. Nada no disco do jogo é alterado** | Runtime-only. Nenhum caminho escreve em APK, OBB ou `/data/app`. |
| **G5. Funciona em celular normal** | Todo teste de device roda em **Permissive e Enforcing**. |
| **G6. Nada entra sem prova** | Definição de pronto abaixo; o merge só acontece com todos os itens verdes. |
| **G7. Release imutável** | Tag + zip reproduzível + SHA256 publicado; versão em um lugar só (`VERSION`); dependência externa pinada por hash. |

### Definição de pronto (gate de merge — sem exceção)
1. `tools/verify_all.sh` verde num checkout limpo: todos os mods e o loader com ndk-build sem warning (fora o `-static-libstdc++`), harness 0 falhas, testes JVM do Manager, `check_sepolicy_rule.sh`, `sh -n`/shellcheck em todo script, encoding arm64 conferido contra o assembler do NDK.
2. Revisão cruzada por **outro** agente, com todos os achados corrigidos (inclusive os cosméticos).
3. `tools/device_test.sh` no device, em Permissive **e** Enforcing: SA2 + Battle Cats + o mod novo, zero crash, zero `avc: denied` do nosso caminho, device restaurado no fim.
4. Soak test: 10 min de jogo com o mod, sem crash, sem ANR e sem crescer memória sem parar.

## Fases (TODO)

### F1d — Proteção contra crash em loop (G1)
- [ ] Loader, antes do `dlopen` dos mods: lê `/data/data/<pkg>/files/bepinex/crashguard` (contador + timestamp). Com 2 mortes seguidas < 60s ⇒ não carrega nenhum mod, loga `mods desativados: o jogo fechou 2x logo depois de carregar — reative no Manager` e cria o `disabled_by_crashguard`.
- [ ] Thread do loader zera o contador depois de 60s vivo.
- [ ] Manager mostra o aviso e tem "Reativar" (apaga os dois arquivos).
- [ ] Lógica de decisão pura + teste no harness.
- **Verifica:** mod de teste que dá `abort()` em 2s ⇒ na 3ª abertura o jogo sobe limpo, sem mod, com o aviso no log.


### F1 — Ativação zero-config (loader)
- [ ] `preAppSpecialize`: `mods/<pkg>/` existe ⇒ caminho genérico, sem precisar da allowlist (1 `stat`, sem scan).
- [ ] A allowlist continua só pro experimento Cocos sem pasta (compatível com o que existe).
- [ ] Função pura `decide_path(pkg, dir_exists, in_allowlist)` + teste no harness.
- [ ] Loader escreve em `/data/data/<pkg>/files/bepinex/log.txt` quais mods carregaram ou falharam (contrato C1).
- **Verifica:** SA2 fora da allowlist carrega os mods; app sem pasta não gera log novo; BC intacto; harness 0 falhas; build 0 warnings.

### F1c — SELinux Enforcing (obrigatório pra gente normal)
Achado 2026-09-26: o device de teste está em **Permissive**, e é só por isso que o zygote lê `/data/local/tmp` e o jogo faz `dlopen` de lá (o logcat mostra `avc: denied ... permissive=1`). Num celular comum (Enforcing) nada disso carrega.
- [ ] `module/sepolicy.rule` (Magisk/KernelSU aplicam no boot): tipo próprio `bepinex_mod_file` + allow mínimo (zygote: `getattr`/`search` na pasta; app: `read`/`open`/`getattr`/`map`/`execute` nos arquivos). Nada de liberar `shell_data_file` inteiro pra todo app.
- [ ] `module/post-fs-data.sh`: cria `/data/local/tmp/mods` e aplica `chcon -R` com o tipo novo. O Manager aplica o mesmo `chcon` depois de instalar cada arquivo.
- [ ] Allowlist legada: ler só se o arquivo existir, e sem erro barulhento.
- **Verifica:** `setenforce 1` no device de teste → SA2 carrega os mods e o u_patch aplica; `dmesg`/logcat sem `avc: denied` do nosso caminho → `setenforce 0` de volta.

### F2 — SDK de mod + kit
- [ ] `mods/common/mod_common.h`: `mod_pkg()`, `mod_dir()`, `mod_log(tag, fmt, ...)` (logcat + log.txt C1), `mod_conf_get(id, key, default)` (C3).
- [ ] `mods/_template/` (Android.mk, Application.mk, `mod.cpp` com boot il2cpp + log).
- [ ] `tools/new_mod.sh <id>`, `tools/deploy_mod.sh <id> <pkg>` (ndk-build + push + force-stop), `tools/pack_bmod.sh <id>` (gera `.bmod`, C2).
- [ ] Migrar sa2ammo/sa2content pra `mod_common.h`, **só se** não mudar comportamento.
- **Verifica:** `new_mod.sh hello` → deploy no SA2 → `hello: il2cpp ok` no log.txt.

### F3 — u_dump (scanner universal Unity IL2CPP)
- [ ] `mods/u_dump`: C5 via API runtime (imune a metadata v39 e criptografia de metadata).
- [ ] Roda numa thread própria, depois do boot il2cpp, e não trava o jogo (yield entre assemblies).
- **Verifica:** dump.tsv no SA2 contém `ComplexCreature`/`HasAmmo`; no TABS, `UnitBlueprint`.

### F4 — u_patch (motor declarativo = base do Mod Maker)
- [ ] `mods/u_patch`: lê todo `*.patch` + `.conf` do `mod_dir()`, aplica C4.
- [ ] `return`: patch de instrução arm64 (`mov w0/x0, #imm` ou `fmov s0`; `ret`) com mprotect + flush de cache. Não precisa de trampolim.
- [ ] `mul`: DobbyHook com pool fixo de thunks (ex.: 64 slots) indexando uma tabela de regras. `// ponytail:` com o teto.
- [ ] `static`: set do campo estático depois do boot e reaplica a cada 2s (o jogo pode resetar).
- [ ] Parser puro e testável (harness host).
- **Verifica:** SA2 com `return ComplexCreature HasAmmo 0 bool true` → munição não trava; regra inválida → log e jogo segue.

### F5 — bepInEx Manager: núcleo (APK)
- [ ] Java puro, sem AndroidX/Gradle: build com SDK (`aapt2` + `javac --release 17` + `d8` + `apksigner`) por `manager/build.sh`, usando `~/Android/Sdk` (build-tools 37, android-36). minSdk 26.
- [ ] Tela Jogos: apps instalados (filtra jogos + qualquer app com engine detectado, C6), ícone, badge do engine, nº de mods.
- [ ] Tela Jogo: mods instalados (C1/C2) com switch (renomeia `.off`), opções geradas do manifest (C3), "Reiniciar jogo" (`am force-stop` + launch), "Ver log" (log.txt C1).
- [ ] Instalar `.bmod`: intent filter pra abrir `.bmod` + botão "+" (SAF). Mostra o manifest, avisa se `game` ≠ pacote ou engine incompatível, e copia os arquivos via `su`.
- [ ] Tela Status: root ok? módulo ativo (`/data/adb/modules/<id>`)? Zygisk ligado? Versão.
- [ ] Todo `su` num helper só; falha de root = mensagem clara, sem crash.
- **Verifica:** instalar `sa2-infinite-ammo.bmod` pelo app, ligar, reiniciar SA2 → ativo; desligar → volta ao normal.

### F6 — Mod Maker (criar mod sem código)
- [ ] "Escanear jogo": copia `u_dump.so` (vem nos assets do APK), reinicia o jogo, espera `dump.tsv`, remove `u_dump.so`.
- [ ] Busca com filtro (classe/método/campo), resultados paginados (dump pode ter 100k+ linhas).
- [ ] Ação por item: método bool → "sempre verdadeiro/falso"; método int/float → "sempre N" ou "multiplicar por N"; campo estático → "fixar em N". Gera regras C4.
- [ ] Salvar mod: nome e descrição → cria `.patch` + manifest `type=patch`, instala e copia `u_patch.so` (assets) se faltar. "Compartilhar" gera `.bmod` em `Download/`.
- **Verifica:** no SA2, recriar o `HasAmmo=true` só pela UI, sem adb.

### F7 — Empacotamento Magisk (1 zip)
- [ ] `tools/build_module.sh`: `module.prop`, `customize.sh` (instala o Manager APK com `pm install`, cria `/data/local/tmp/mods`), `zygisk/arm64-v8a.so`, `uninstall.sh`.
- [ ] Compatível com Magisk (Zygisk nativo) e KernelSU + ZygiskNext (documentar).
- **Verifica:** zip instalado pelo app Magisk → reboot → Manager no launcher → SA2 com mod funciona.

### F8 — Docs pra gente normal
- [ ] README PT-BR no topo: "Instalar em 3 passos", "Instalar um mod", "Criar mod sem código", FAQ (sem root? Play Protect? jogo online?).
- [ ] `docs/BMOD-FORMAT.md` (C2–C5) pra quem faz mod.
- [ ] `docs/SDK.md`: mod nativo C++ com o template (F2).

### F9 — Pesquisa: bloqueio de anúncio genérico + UX de referência
- [ ] Ponto de hook comum de AdMob/AppLovin/ironSource/Unity Ads (Java via JNI vs wrapper C#). Go/no-go de um `u_noads`.
- [ ] Referência de UX: LSPosed Manager, GameGuardian, Lucky Patcher, MT Manager. Lista de padrões pra copiar no Manager.

### F11 — Scripts Frida como mod (runtime, sem PC)
- [ ] Loader/Manager: mod `.js` ⇒ copia `frida-gadget` arm64 (licença wxWindows, vai nos assets do Manager) + config `interaction: script` apontando pro `.js`. [NAO VERIFICADO: confirmar a doc do gadget modo script + tamanho + detecção por anti-tamper]
- [ ] Suporte a `frida-il2cpp-bridge` (scripts da comunidade que usam `Il2Cpp.perform`).
- **Verifica:** script `.js` simples que loga um método do SA2, instalado pelo Manager.

### F12 — Mods `.dll` IL2CPP (BepInEx 6 IL2CPP / MelonLoader IL2CPP) — spike primeiro
- [ ] Spike: carregar o runtime .NET (CoreCLR) **dentro do processo pelo nosso Zygisk** (sem container), reaproveitando o que o NextBep/FusionCore já portou (CoreCLR android-arm64 + Il2CppInterop + HarmonyX). Medir tamanho, RAM, tempo da 1ª execução (geração dos assemblies proxy).
- [ ] Go/no-go com números. Se go: fase de implementação separada.
- Limite real: só roda `.dll` feito pra versão **IL2CPP** do jogo. Mod de PC Mono não entra aqui.

### F13 — Mods `.dll` Mono em jogo Unity Mono
- [ ] `mono_min.h` + carregar assembly (`mono_domain_assembly_open`) + HarmonyX (roda nativo em Mono).
- [ ] Compat BepInEx 5 mínima (`BaseUnityPlugin`, `Logger`, `Config`) pra mod de PC do mesmo jogo carregar sem recompilar, quando o jogo Android também é Mono.
- Precisa de jogo-alvo Unity Mono real pra validar.

### F10 — Depois (fora do caminho crítico)
- `mono_min.h` (Unity Mono), com jogo-alvo real.
- Helper de thread principal Unity (habilita `u_speed`/`u_fps` universais).
- Stream/console no genérico sem abrir Termux por cima do jogo.
- Unificar o caminho BC no genérico (decisão do usuário).

## Ordem, dependências, dono

| Fase | Depende de | Agente | Branch/worktree |
|---|---|---|---|
| F1 loader | — | kilo | `uni/f1-zeroconfig` |
| F2 SDK + kit | F1 (log C1) | OpenCode | `uni/f2-sdk` |
| F3 u_dump | contrato C5 | freebuff | `uni/f3-udump` |
| F4 u_patch | contrato C4 | kimi | `uni/f4-upatch` |
| F5+F6 Manager | contratos C1–C6 | Antigravity | `uni/f5-manager` |
| F7 módulo + F8 docs | F5 | OpenCode (depois de F2) | `uni/f7-module` |
| F9 pesquisa | — | hermes | só relatório |

F3, F4 e F5 andam em paralelo contra os contratos. F2 dá `mod_common.h`: até ele chegar, F3/F4 usam o próprio log mínimo e trocam depois. Integração: merge em `feat/generic-pkg-mods` (sem push sem autorização do usuário), revisão cruzada por outro agente, validação no device (SA2 + BC) pelo orquestrador.

## Riscos

- `il2cpp_domain_get` antes do `il2cpp_init` crasha o jogo (sa2ammo). Todo mod usa `il2cpp_boot()`.
- Patch de instrução (F4 `return`): método minúsculo (< 8 bytes) ou inline → não patchar, logar.
- SELinux: jogo lendo `/data/local/tmp/mods` já funciona (SA2); **escrever** `/data/data/<pkg>/files` é do próprio app, ok.
- Play Protect pode reclamar do APK do Manager sideloaded → documentar no FAQ.
- Anti-tamper por jogo: fora do nosso controle; log claro se o jogo morrer logo depois de carregar mod.
- APK Manager sem Gradle: build manual frágil → `build.sh` idempotente, testado limpo.

## Fora de escopo

- Sem root. Patch de APK.
- Conversão automática de mod `.dll` Mono de PC pra jogo IL2CPP: o mod referencia tipos Mono que não existem no build IL2CPP, e transpiler Harmony não tem IL pra reescrever. Fica como "port assistido" (Mod Maker/SDK), não automático.
- Burlar IAP/licença/pagamento. PvP online com anti-cheat.

## Decisões tomadas (defaults do orquestrador, usuário pode trocar)

1. Allowlist vira opcional: pasta de mods basta (F1).
2. Caminho BC fica separado até F10.
3. Anúncio genérico: só pesquisa agora (F9).
4. Manager em Java puro sem Gradle (zero dependência nova).
