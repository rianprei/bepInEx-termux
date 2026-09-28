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
| sinais Termux→BC por seq | merged, validado; bug de baseline em correção | 2026-09-26 v0.4.1: `reload_config` via Termux → prop seq 3 → jogo reagiu; props `persist.*` antigas disparam na abertura (kilo) |
| F1d crashguard | merged, validado | 2026-09-26: t_crash matou 2x, 3ª abertura sem mods e viva, aviso no log; reativar = mods voltam; contador zera após 20s vivo |
| F2 SDK | merged, sem device | `new_mod`/`pack_bmod`/template só testados no host |
| F3 u_dump | merged, validado | SA2: 162.804 linhas, `unity=6000.3.13f1`, Permissive e Enforcing |
| F4 u_patch | merged; teste de device falhou | `6589f2f` implementa return/mul/static/field; `bf9eee3` unifica fixture C4 do Manager e do mod; harness e QEMU passam no host; no primeiro teste em aparelho (2026-09-27, 22:44), `field` causou SIGSEGV em il2cpp_type_get_name e o SA2 caiu; o kit detectou o crash e restaurou o aparelho. Evidência: ~/Documentos/mods/_backup_device_2026-09-27/evidence/tombstone_07_f4field.txt. `return`/`mul`/`static` ainda não foram testados no aparelho; correção em andamento (Kilo, uni/ufield-crash) |
| F5/F6 Manager | merged, sem device | `59fe913`, `5ed8019`, `368406d`: APK e testes JVM passam no gate; inclui detector de engines, filtro de jogos, texto de suporte, controle de mods e Mod Maker; app nunca instalado num celular |
| F7 zip + G7 release | merged, host verificado, sem device/publicação | `f36c650`: build local reproduzível, hashes e dependências pinadas; `192e967`: assinatura com chave fixa do usuário; zip e APK ainda não instalados em aparelho nem publicados |
| F8 docs | merged, sem device | 8 achados de revisão corrigidos (`15b9a3f`) |
| F9b u_noads | merged, sem device | `f08164b`: fecha o anúncio pelo callback de cada SDK (6 SDKs, 15 hooks); falta rodar no SA2 com `sa2content` desligado |
| F11 u_frida | merged, sem device | script `.js` por `tools/deploy_frida.sh` (PC+adb) nunca rodou num celular |
| T2 verify_all | merged, host verificado, sem device | `ad46276` cria o gate; ele compila e testa no host, inclusive simuladores; isso continua sendo SIMULAÇÃO — `tools/device_test.sh` no celular em Permissive e Enforcing segue aberto |
| T1 kit de device | merged, sem device | `1224346` cria o kit com backup verificado; `c3da4cc` adiciona relógio falso; `2b3d8c6` corrige stdin herdado; `05b7138` valida SHA-256 de cada arquivo restaurado; tudo isso foi testado em simulação de host |
| docs de referência no gate | merged, sem device | `73e45b5` estende a checagem para validar linha/faixa e texto literal citado; o gate atual verifica 26 referências; `docs/DEVICE-ROUND.md` é o roteiro da rodada |
| guarda do gadget no loader | merged, sem device | `e92b13f` rejeita `.so` com `DT_SONAME` de gadget (Caso 63) — é barreira de host, ainda não testada com o gadget real num celular |
| README honesto | merged, sem device | `37a4120`: matriz de suporte honesta, limite medido do tradutor, Frida pinado; sem device |
| roteiro device-round-2 | merged, sem device | `6381ead`: roteiro em `docs/DEVICE-ROUND-2.md` + automação com testes host; a rodada no aparelho é pré-requisito de publicação |
| espera IL2CPP unificada | merged, sem device | `8eba793`: 240s com poll de 200ms, matcher por sufixo, log da rota de abertura; host; sem aparelho |
| IDs de caso únicos no harness | merged, sem device | `94dd077`: casos renumerados + gate detecta duplicados; host; sem aparelho |
| tradutor `.dll`→`.bpatch` (JVM→C++) | merged, sem device | `5581e2e`: round-trip real tradutor→parser C++ (Caso 80); testes JVM e harness C++; sem aparelho |
| ABI dupla arm32 + recusa de hook 32-bit | merged, sem device | `f8a12fc`: loader e mods em arm64-v8a e armeabi-v7a, Dobby arm32 pinado com smoke qemu, hooks recusam 32-bit com log, Manager escolhe o `.so` pela ABI; host e qemu, sem aparelho |
| limite medido do tradutor | merged, sem device | `139aceb`: 0 de 378 patches em 30 mods convertem (375 sem o tipo-alvo na DLL, 3 por classe aninhada); `docs/DLL-COVERAGE.md` + checagem no gate; sem aparelho |
| árvore root-only + entrega por FD (mods-reloc) | em revisão, fora da base | `uni/mods-reloc`: mods em diretório root-only com entrega por descritor, kit separa trânsito e árvore; sem hash de merge |
| companion serve só quem conectou (peercred) | em revisão, fora da base | `uni/peercred`: companion monta a partir de pacote e nome, sem aceitar caminho; sem hash de merge |
| símbolos e symbolize (symbols) | em revisão, fora da base | `uni/symbols`: build-id reproduzível, símbolos guardados, tombstone vira função e linha; sem hash de merge |
| fuzz do emissor arm64 (fuzz-upatch) | em revisão, fora da base | `uni/fuzz-upatch`: alvo de fuzz do emissor de thunk com ASan e UBSan; sem hash de merge |

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
                                          <id>.bpatch     regras declarativas            F4
                                          u_dump.so      scanner (pedido pelo Manager)  F3
                                          <id>.conf      opções key=value
                                          <id>.json      manifest (pro Manager listar)
                                        u_noads.so     fecha anúncio pelo callback    F9b
                                           frida-gadget.bin + .config  script .js         F11
                                         bepInEx Manager (APK, root)                     F5-F6
                                        zip Magisk que instala o Manager                F7

ATÉ ONDE O LOADER CHEGOU (feat/generic-pkg-mods, 2026-09-26)
  JÁ NO MAPA: zero-config por pasta (F1), log C1, crashguard de 20s (F1d),
  sepolicy bepinex_mod_file (F1c), scanner u_dump (F3), motor de regras
  u_patch (F4), Manager e Mod Maker (F5/F6), scripts Frida em modo script
  (F11), bloqueio de gadget renomeado por DT_SONAME (e92b13f) e supressão
  de anúncio (F9b). As etapas dependentes de uso real no celular continuam
  pendentes; execução de mods .dll também segue fora do mapa (F12/F13).

## Contratos (fixos — todos os agentes seguem)

### C1. Diretórios no device
- Mods: `/data/adb/bepinex/mods/<pkg>/` (pai root-only `0700`; a pasta `755`, arquivos `644`). Escrito **só pelo Manager (via su) ou adb**.
- Mod desligado: sufixo `.off` (`foo.so.off`, `foo.bpatch.off`). O loader só carrega `*.so` (`bc_loader_is_mod_filename`).
- Saída do processo do jogo (log e snapshot): `/data/data/<pkg>/files/bepinex/`, derivado do `app_data_dir` que o zygote entrega (multiusuário: `/data/user/N/<pkg>`). O jogo também não escreve na árvore de mods.
  - `log.txt`: log de todos os mods (append, 1 linha = `HH:MM:SS [mod] msg`, corta em 256KB).
  - `dump.tsv`: saída do u_dump.
  - Descobrir `<pkg>` dentro do mod: `getenv("BEPINEX_PKG")`, que o loader seta antes do `dlopen`. **Não** use `/proc/self/cmdline` no constructor: no device ele ainda vale `zygote64` nesse momento (achado 2026-09-26, u_patch leu `mods/zygote64`). Fallback só se a env faltar: cmdline, esperando sair de `zygote*`.
  - Identidade do pacote: o loader prefere o último componente de `app_data_dir`; se estiver ausente/inválido, usa `nice_name` sem o sufixo `:processo` (por exemplo, `com.foo:unity` → `com.foo`). O nome precisa caber inteiro; pacote inválido ou longo demais é recusado, nunca truncado.
  - Processo: carrega no principal e em processos normais `:sufixo` do mesmo pacote, pois a engine pode existir só no processo secundário. Recusa child zygote, `app_zygote` e UID isolado. `is_top_app` não é filtro: processos normais em segundo plano também podem carregar a engine. A regra vale igualmente para o matcher especial do Battle Cats.

### C2. `.bmod` (zip)
```
manifest.json
mod.so        (type=native, arm64-v8a)  |  mod.bpatch  (type=patch)
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
- Instalação: `mod.so` → `<id>.so`, `mod.bpatch` → `<id>.bpatch`, manifest → `<id>.json`, opções → `<id>.conf`.

### C3. `.conf` (opções)
Uma linha `key=value`, `#` é comentário. Escrito pelo Manager, lido pelo mod no boot.

### C4. `.bpatch` (motor declarativo u_patch)
Uma regra por linha, campos separados por espaço, `#` é comentário:
```
return  <Classe>  <Método>  <nargs>  <bool|int|float>  <valor>
mul     <Classe>  <Método>  <nargs>  <int|float>       <fator>
static  <Classe>  <campo>   <bool|int|float>           <valor>
field   <Classe>  <campo>   <bool|int|float>           <valor>  [<Método> <nargs>]
```
- `field` = campo de **instância**: a cada chamada de `<Método>` (instância, da mesma `<Classe>`), escreve `this.<campo> = <valor>` antes de rodar o original (hook com thunk, `this` = x0, offset via `field_get_offset`). Sem `<Método>`, o u_patch escolhe sozinho até 8 métodos de instância da classe que passam na guarda de tamanho. Motivo (teste no device 2026-09-26): `return ComplexCreature HasAmmo 0 bool true` aplicou e foi chamado 12x (Frida), mas a munição acabou mesmo assim, porque o jogo decrementa e checa o campo direto; o que dá munição infinita é o campo `WeaponInfo.unlimitedAmmo` (o sa2ammo usa ele). Método patchado não cobre lógica que lê campo direto.
- `<Classe>` = `Namespace.Nome`, ou só `Nome` sem namespace (o último `.` separa). Classe aninhada fica fora do v1.
- O número de campos é **exato**: 6 em `return`/`mul`, 5 em `static`, 5 (auto) ou 7 (com `<Método> <nargs>`) em `field`. Token a mais ou a menos é linha inválida nos dois lados.
- `<nargs>` é inteiro: só dígitos, `>= 0` e `<= 64` (teto do `up_parse_nargs` do u_patch). `field` com 5 tokens não leva `<nargs>`: é o modo "auto", e `nargs = -1` é **só** esse modo, nunca o explícito.
- `<tipo>` sai do conjunto fechado `bool|int|float`; `mul` não aceita `bool` (o fator multiplica valor numérico).
- Um mesmo arquivo de fixtures vale pros dois lados: `test/fixtures/c4_lines.tsv` (`<linha> TAB <accept|reject>`), lido pelo `PatchGenerator.parse` do Manager (teste JVM `C4FixtureTest`) e pelo harness do `u_patch`. Divergência = os dois lados falham, com a linha no erro; corrige-se o lado errado conforme este contrato, nunca a fixture.
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
Inspeciona nomes de bibliotecas e assets do APK base/splits (`ApplicationInfo.sourceDir` + `splitSourceDirs`, via `ZipFile`) ou `nativeLibraryDir`. A implementação e `EngineDetectorTest` cobrem Unity IL2CPP/Mono, Unreal, Godot, Cocos2d-x, Defold, Flutter, React Native, Solar2D, LÖVE, libGDX, Xamarin/.NET e Ren'Py. A ordem prioriza marcadores específicos de Unity; casos sem marcador conhecido viram nativo, Java ou desconhecido. É uma classificação por conteúdo, não confirmação de que o app é um jogo nem garantia de compatibilidade de mods.

### C7. Aceitar mod de qualquer origem (detecção automática no Manager)
O usuário escolhe um arquivo qualquer + o jogo. O Manager identifica o tipo pelo **conteúdo** (magic/cabeçalho), não pela extensão, e diz em português se roda e como.

| Tipo detectado | Como detecta | Roda? | Caminho |
|---|---|---|---|
| `.bmod` | zip com `manifest.json` | sim | C2 |
| `.so` Android arm64 | ELF, `e_machine=183` (AArch64) | sim | copia pra `mods/<pkg>/` |
| `.so` 32-bit/x86 | ELF de outra arquitetura | não | "feito pra outra arquitetura" |
| `.bpatch` | texto nas regras C4 | sim | u_patch |
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
| **G1. Mod nunca derruba o jogo pra sempre** | F1d: proteção contra crash em loop. 2 mortes seguidas < **20s** depois de carregar mod ⇒ o loader não carrega mais mods daquele jogo até o usuário reativar. (A janela era 60s e virou 20s em `faf0afd`: 60s dava falso positivo quando o jogo crasha por motivo alheio ao mod.) |
| **G2. Mod nunca derruba o celular** | Loader só atua em app com pasta de mods; o resto recebe `DLCLOSE`. Magisk safe mode desliga o módulo se o boot falhar. |
| **G3. Jogo atualizado não quebra, só desliga o mod** | Tudo resolvido por nome em runtime; não achou ⇒ log claro e segue. Nada de offset fixo. |
| **G4. Nada no disco do jogo é alterado** | Runtime-only. Nenhum caminho escreve em APK, OBB ou `/data/app`. |
| **G5. Funciona em celular normal** | Todo teste de device roda em **Permissive e Enforcing**. |
| **G6. Nada entra sem prova** | Definição de pronto abaixo; o merge só acontece com todos os itens verdes. |
| **G7. Release imutável** | Tag + zip reproduzível + SHA256 publicado; versão em um lugar só (`VERSION`); dependência externa pinada por hash. |

### Definição de pronto (gate de merge — sem exceção)
1. `tools/verify_all.sh` verde num checkout limpo: todos os mods e o loader com ndk-build sem warning (a isenção do `-static-libstdc++` acabou em `81efee2` — o flag saiu de todo `Application.mk` e hoje QUALQUER warning é FAIL), harness 0 falhas, testes JVM do Manager, `check_sepolicy_rule.sh`, `sh -n`/shellcheck em todo script, encoding arm64 conferido contra o assembler do NDK.
2. Revisão cruzada por **outro** agente, com todos os achados corrigidos (inclusive os cosméticos).
3. `tools/device_test.sh` no device, em Permissive **e** Enforcing: SA2 + Battle Cats + o mod novo, zero crash, zero `avc: denied` do nosso caminho, device restaurado no fim.
4. Soak test: 10 min de jogo com o mod, sem crash, sem ANR e sem crescer memória sem parar.

## Fases (TODO)

### F1d — Proteção contra crash em loop (G1)
- [x] Loader, antes do `dlopen` dos mods: lê `/data/data/<pkg>/files/bepinex/crashguard` (contador + timestamp). Com 2 mortes seguidas < **20s** ⇒ não carrega nenhum mod, loga `mods desativados: o jogo fechou 2x logo depois de carregar — reative no Manager` e cria o `disabled_by_crashguard`. (`faf0afd` mudou a janela de 60s para 20s; device 2026-09-26: `t_crash` matou 2x, 3ª abertura sobe limpa, sem mod, com o aviso no log.)
- [x] Thread do loader zera o contador depois de 20s vivo. (`jni/main.cpp::crashguard_clear_thread`; device 2026-09-26: contador zera após 20s vivo.)
- [~] Manager mostra o aviso e tem "Reativar" (apaga os dois arquivos). (host OK: `CrashGuardState` + banner em `GameDetailActivity` + botão que apaga marcador **e** zera o contador, `CrashGuardStateTest`; device: pendente — o banner nunca apareceu num celular.)
- [x] Lógica de decisão pura + teste no harness. (Caso 60 `bc_crashguard_blocks/next_count`.)
- **Verifica:** mod de teste que dá `abort()` em 2s ⇒ na 3ª abertura o jogo sobe limpo, sem mod, com o aviso no log.


### F1 — Ativação zero-config (loader)
- [x] `preAppSpecialize`: `mods/<pkg>/` existe ⇒ caminho genérico, sem precisar da allowlist (1 `stat`, sem scan). (device 2026-09-26: SA2 fora da allowlist carrega mods; BC 4/4.)
- [x] A allowlist continua só pro experimento Cocos sem pasta (compatível com o que existe). (Caso 62 `bc_generic_allowlist_contains_buf`; device: Battle Cats intacto.)
- [x] Função pura `decide_path(pkg, dir_exists, in_allowlist)` + teste no harness. (Caso 56 `bc_decide_path`.)
- [x] Loader escreve em `/data/data/<pkg>/files/bepinex/log.txt` quais mods carregaram ou falharam (contrato C1). (`jni/main.cpp`; `mod_common.h::mod_log`; device: `log.txt` escrito no SA2.)
- **Verifica:** SA2 fora da allowlist carrega os mods; app sem pasta não gera log novo; BC intacto; harness 0 falhas; build 0 warnings.

### F1c — SELinux Enforcing (obrigatório pra gente normal)
Achado 2026-09-26: o device de teste está em **Permissive**, e é só por isso que o zygote lê `/data/local/tmp` e o jogo faz `dlopen` de lá (o logcat mostra `avc: denied ... permissive=1`). Num celular comum (Enforcing) nada disso carrega.
- [x] `module/sepolicy.rule` (Magisk/KernelSU aplicam no boot): tipo próprio `bepinex_mod_file` + allow mínimo (zygote: `getattr`/`search` na pasta; app: `read`/`open`/`getattr`/`map`/`execute` nos arquivos). Nada de liberar `shell_data_file` inteiro pra todo app. (`b510b1e` + etapa `sepolicy grammar` do gate; device: v0.4.0 com `setenforce 1`, SA2 + BC 4/4, zero `avc` do nosso caminho.)
- [x] `module/post-fs-data.sh`: cria `/data/adb/bepinex/mods` e aplica `chcon` com o tipo novo. O Manager aplica o mesmo `chcon` depois de instalar cada arquivo. (`e5d16a4`; `SuHelper.installFile` faz cp+chmod 644+chcon, coberto por `SuHelperTest`.)
- [x] Allowlist legada: ler só se o arquivo existir, e sem erro barulhento. (Caso 62.)
- **Verifica:** `setenforce 1` no device de teste → SA2 carrega os mods e o u_patch aplica; `dmesg`/logcat sem `avc: denied` do nosso caminho → `setenforce 0` de volta.

### F2 — SDK de mod + kit
- [~] `mods/common/mod_common.h`: `mod_pkg()`, `mod_dir()`, `mod_log(tag, fmt, ...)` (logcat + log.txt C1), `mod_conf_get(id, key, default)` (C3). (host OK: `6fd1415` + etapa `host test/mod_common_test.cpp`; device: pendente.)
- [~] `mods/_template/` (Android.mk, Application.mk, `mod.cpp` com boot il2cpp + log). (host OK: `d0e35f7` + etapa `ndk-build mods/_template` PASS; device: pendente — `new_mod.sh hello` no SA2 nunca rodou.)
- [~] `tools/new_mod.sh <id>`, `tools/deploy_mod.sh <id> <pkg>` (ndk-build + ABI do `primaryCpuAbi` + push + force-stop), `tools/pack_bmod.sh <id> <abi>` (gera `.bmod`, C2). (ABI routing: host; device: pendente.)
- [~] Migrar sa2ammo/sa2content pra `mod_common.h`, **só se** não mudar comportamento. (host OK: `5166d37` (sa2ammo, sa2content) + `3b85de2` (u_dump e t_crash, que ainda tinham C1 duplicado por baixo do `mod_common`) + `09483b8`; a linha de log dos dois passou a ir para `/data/data/<pkg>/files/bepinex/log.txt` e o `up_log` próprio do u_patch saiu, então a rotação do log C1 tem UM dono; device: pendente — o SA2 tem que continuar com munição e conteúdo, e o t_crash tem que continuar matando 2x.)
- **Verifica:** `new_mod.sh hello` → deploy no SA2 → `hello: il2cpp ok` no log.txt.

### F3 — u_dump (scanner universal Unity IL2CPP)
- [x] `mods/u_dump`: C5 via API runtime (imune a metadata v39 e criptografia de metadata). (`752cfa7`; device: SA2 com 162.804 linhas e `unity=6000.3.13f1`, em Permissive e Enforcing.)
- [x] Roda numa thread própria, depois do boot il2cpp, e não trava o jogo (yield entre assemblies). (`u_dump_mod.cpp`; o dump completo de 162k linhas no device mostra que a thread não travou o jogo; Casos 57-58 cobrem `dump_core` e `dump_join_class_name`.)
- **Verifica:** dump.tsv no SA2 contém `ComplexCreature`/`HasAmmo`; no TABS, `UnitBlueprint`.

### F4 — u_patch (motor declarativo = base do Mod Maker)
- [~] `mods/u_patch`: lê todo `*.bpatch` + `.conf` do `mod_dir()`, aplica C4. (host OK: `up_scan_apply` + `up_foreach_line` puro (Casos 69-77) e a etapa "u_patch encoding harness" do gate passando; device: pendente — a rodada do F4 no SA2 ainda não rodou.)
- [~] `return`: patch de instrução arm64 (`mov w0/x0, #imm` ou `fmov s0`; `ret`) com mprotect + flush de cache. Não precisa de trampolim. (host OK: Caso 70 confere cada palavra contra o llvm-objdump do NDK e o guard `up_method_fits` (Caso 71) recusa método curto; device: pendente.)
- [~] `mul`: DobbyHook com pool fixo de thunks (ex.: 64 slots) indexando uma tabela de regras. `// ponytail:` com o teto. (host OK: Caso 70 + `test/device/thunk_exec` executando o thunk de verdade no qemu (8×100k threads, recursão, float) e `buga`/`bugb2` falhando de propósito; device: pendente.)
- [~] `static`: set do campo estático depois do boot e reaplica a cada 2s (o jogo pode resetar). (host OK: revalidação do FieldInfo a cada 10 passadas, com o valor reescrito a cada 2s; device: pendente.)
- [~] `field`: campo de instância reescrito a cada chamada de `<Método>` (a 4ª linha do verbo C4). (host OK: Casos 72/77 conferem o layout palavra a palavra e o `test/device/thunk_exec` executa o thunk com `this=NULL` (não escreve e chama o original) e com `this` válido (escreve e o original lê de volta, com canário); device FAIL no primeiro teste (2026-09-27, 22:44): SIGSEGV em `il2cpp_type_get_name`, SA2 caiu e o kit restaurou o aparelho. Evidência: ~/Documentos/mods/_backup_device_2026-09-27/evidence/tombstone_07_f4field.txt. Correção em andamento (Kilo, uni/ufield-crash); não usar `field` até a correção ser integrada.)
- [x] Parser puro e testável (harness host). (Entrega de host, não precisa de device: `u_patch_parse.h` puro, Casos 69/73/74/76 e a fixture `test/fixtures/c4_lines.tsv` — a mesma que o `PatchGenerator` do Manager lê, então os dois lados do C4 não podem divergir sem os dois testes caírem.)
- [x] Guarda de `this` nulo no thunk `field` (achado CRÍTICO do cross-review: o jogo chama método com `this == nullptr` e o store em `[0+off]` derrubava o jogo). (host OK: `up_enc_cbz_x0` + layout com o caminho direto (Caso 77) e o teste de execução com `this=NULL`; a sabotagem sem o `cbz` faz o caso falhar.)
- [x] Tipos e recusas antes de hookar: tamanho do campo pelo tipo real do il2cpp, classe de valor (struct) recusada, e `return`/`mul` float em método que devolve `System.Double` recusado — cada um com caso próprio. (host OK: `up_value_type_check` no Caso 73.)
- **Verifica:** SA2 com `return ComplexCreature HasAmmo 0 bool true` → munição não trava; regra inválida → log e jogo segue. O roteiro pronto da rodada está em `docs/DEVICE-ROUND.md:102`.

### F5 — bepInEx Manager: núcleo (APK)
- [~] Java puro, sem AndroidX/Gradle: build com SDK (`aapt2` + `javac --release 17` + `d8` + `apksigner`) por `manager/build.sh`, usando `~/Android/Sdk` (build-tools 37, android-36). minSdk 26. (host OK: `manager/build.sh` gera APK assinado e o `badging` confere com o `VERSION` da raiz (`dacad11`); device: pendente — o APK nunca foi instalado num celular.)
- [~] Tela Jogos: apps instalados (jogos identificados pelo sistema, engines de jogos conhecidos ou apps com mods; Flutter, React Native, Xamarin, nativo e Java não bastam por si só), ícone, badge do engine, nº de mods. (host OK: `MainActivity` + `EngineDetectorTest` e teste da regra `isGameEngine`; device: pendente.)
- [~] Tela Jogo: mods instalados (C1/C2) com switch (renomeia `.off`), opções geradas do manifest (C3), "Reiniciar jogo" (`am force-stop` + launch), "Ver log" (log.txt C1). (host OK: `GameDetailActivity` + `SuHelper.toggleMod`, `ConfTest` (C3), `CrashGuardStateTest`; device: pendente.)
- [~] Instalar `.bmod`: intent filter pra abrir `.bmod` + botão "+" (SAF). Mostra o manifest, avisa se `game` ≠ pacote ou engine incompatível, e copia os arquivos via `su`. (host OK: `AndroidManifest.xml` tem o filter (`pathPattern .*\.bmod`), `BmodInstaller` valida compatibilidade e `LooseModInstaller` cobre qualquer arquivo (C7), com tetos anti-zip-bomb (`BmodInstallerTest`); device: pendente.)
- [~] Tela Status: root ok? módulo ativo (`/data/adb/modules/<id>`)? Zygisk ligado? Versão. (host OK: `StatusChecker` + card de status com versão vinda do `VERSION` (`BuildVersionTest`); device: pendente.)
- [~] Todo `su` num helper só; falha de root = mensagem clara, sem crash. (host OK: `SuHelper` com validação central de todo dado interpolado no shell root (`SuHelperTest`: pkg/nome/chmod/caminho hostis recusados antes de montar o comando); device: pendente — `su` de verdade ainda não foi exercitado por teste automatizado.)
- [~] Assinar com a chave do usuário sem expor a senha no `ps`. (host OK: `manager/build.sh` usa `--ks-pass env:MANAGER_KS_PASS` (nunca `pass:<literal>` fora da chave debug pública) e, sem a variável, deixa o apksigner perguntar no terminal; `tools/build_release.sh` acha `~/.config/bepinex-termux/manager-release.jks` por padrão e grava no `BUILD-INFO` o fingerprint SHA-256 do CERTIFICADO, extraído do APK assinado; `test/release_key_test.sh` assina com keystore temporário nos dois caminhos e apaga tudo no trap; chave e binário assinado fora do git em todo o repo. device: pendente — assinar com a chave de verdade e conferir o fingerprint é coisa de humano.)
- **Verifica:** instalar `sa2-infinite-ammo.bmod` pelo app, ligar, reiniciar SA2 → ativo; desligar → volta ao normal.

### F6 — Mod Maker (criar mod sem código)
- [~] "Escanear jogo": copia `u_dump.so` (vem nos assets do APK), reinicia o jogo, espera `dump.tsv`, remove `u_dump.so`. (Correção do sync anterior: `manager/assets/` NÃO é um arquivo versionado — o `manager/build.sh` compila `mods/u_dump` e copia o `.so` para lá a cada build, que é por isso que a pasta não aparece no repo. host OK: `build.sh` etapas de asset + `ModMakerActivity` com guarda `hasAsset` que avisa "Componente Ausente" em vez de scanner quebrado; device: pendente — o botão Escanear nunca foi apertado num celular.)
- [~] Busca com filtro (classe/método/campo), resultados paginados (dump pode ter 100k+ linhas). (host OK: `ModMakerActivity` + `DumpParserTest` (C5) com leitura de arquivo de 100k+ linhas; device: pendente.)
- [~] Ação por item: método bool → "sempre verdadeiro/falso"; método int/float → "sempre N" ou "multiplicar por N"; campo estático → "fixar em N"; campo de instância → verbo `field` do C4. Gera regras C4. (host OK: `PatchGeneratorTest` (C4) cobre `return`/`mul`/`static`/`field` com round-trip; device: pendente.)
- [~] Salvar mod: nome e descrição → cria `.bpatch` + manifest `type=patch`, instala e copia `u_patch.so` (assets) se faltar. "Compartilhar" gera `.bmod` em `Download/`. (host OK: `ModMakerActivity` gera o `.bpatch`, instala e exporta o `.bmod` em `Download/` via `BmodInstaller.createBmod`; F4 já está merged; device: pendente.)
- **Verifica:** no SA2, recriar o `HasAmmo=true` só pela UI, sem adb.

### F7 — Empacotamento Magisk (1 zip)
- [~] `tools/build_module.sh`: `module.prop`, `customize.sh` (instala o Manager APK com `pm install`, cria `/data/local/tmp/mods`), `zygisk/arm64-v8a.so`, `uninstall.sh`. `tools/build_release.sh` gera também APK, módulos de exemplo, `SHA256SUMS` e `BUILD-INFO.txt`, conferindo dependências pinadas. (host OK: `f36c650` e builds reproduzíveis; device: pendente — zip e APK não instalados em celular.)
- [~] Compatível com Magisk (Zygisk nativo) e KernelSU + ZygiskNext (documentar). (host OK: README e `module/*.sh` citam os três; device: pendente — só Magisk foi exercitado, e nem isso desde o formato novo.)
- **Verifica:** zip instalado pelo app Magisk → reboot → Manager no launcher → SA2 com mod funciona.

### F8 — Docs pra gente normal
- [x] README PT-BR no topo: "Instalar em 3 passos", "Instalar um mod", "Criar mod sem código", FAQ (sem root? Play Protect? jogo online?). (`15b9a3f`, 8 achados do hermes corrigidos; seções existem no README. O texto do doc describe um fluxo que ainda não foi seguido de ponta a ponta por uma pessoa normal no device — isso é o item 3 da "Definição de pronto", não do doc.)
- [x] `docs/BMOD-FORMAT.md` (C2–C5) pra quem faz mod. (`15b9a3f`.)
- [x] `docs/SDK.md`: mod nativo C++ com o template (F2). (`15b9a3f`.)

### F9 — Pesquisa: bloqueio de anúncio genérico + UX de referência
- [~] Ponto de hook comum de AdMob/AppLovin/ironSource/Unity Ads (Java via JNI vs wrapper C#). Go/no-go de um `u_noads`. (host OK: `f08164b` — `mods/u_noads` hooka os `Show` de 6 SDKs (15 hooks) e dispara o fechamento pelo callback de cada SDK, com "nunca suprimir sem fechar"; Caso 64 + `test_targets.cpp`/`test_closers.cpp`; device: pendente — precisa rodar no SA2 com o `sa2content` desligado, senão os dois mods brigam pelo mesmo banner.)
- [x] Referência de UX: LSPosed Manager, GameGuardian, Lucky Patcher, MT Manager. Padrões verificados, equivalentes no Manager, custos e top 5 em `docs/UX-REFERENCE.md`. Lucky Patcher sem padrões verificáveis: site oficial retornou HTTP 403. (`3307976d94b5347a7a9c8ea1cb497810ee9436f6`.)

### F11 — Scripts Frida como mod (runtime, sem PC)
- [x] Mod `mods/u_frida` (existe, branch): com `*.js` na pasta do jogo, confere `frida-gadget.bin` + `frida-gadget.config` ao lado e dá `dlopen` no gadget. Só no modo script: `uf_config_is_script_mode()` exige JSON válido (≤4KB) com `interaction.type` `script`/`script-directory` — `{}`, `listen`, `connect` ou inválido recusa (o default do gadget é `listen` + `on_load: wait`, que congela o jogo). Selftest Caso 61.
- [x] Instalador `tools/deploy_frida.sh` (PC + adb + su): valida o pacote, copia `.js` + `.bin` e escreve config `script-directory` apontando pra `mods/<pkg>/`, chmod 644 + chcon `bepinex_mod_file`, cada passo conferido. Gadget pinado por sha256 em `tools/fetch_frida_gadget.sh` (17.19.0, licença wxWindows).
- [~] Manager (F5): instalar `.js` + gadget + config pelo celular, sem PC. (host OK: o botão de instalar virou "qualquer arquivo" e o `LooseModInstaller` instala o gadget pelo caminho certo — `frida-gadget.bin` **sem .so** + `frida-gadget.config` em `script-directory` — e recusa o `.so` do gadget; `ModContentDetectorTest`; device: pendente. O `gadget` ainda não vem embutido nos assets do APK: hoje o usuário escolhe o binário.)
- [ ] Rodada de device Enforcing: coletar `avc: denied` do u_frida e adicionar SÓ a permissão negada à regra (`module/sepolicy.rule`). [NAO VERIFICADO EM ENFORCING: `bepinex_mod_file` não tem `execmod`; o gum pode precisar de `mprotect(+PROT_EXEC)` em página do binário. Não adicionar por teoria.]
- [~] Loader recusa o gadget renomeado antes do `dlopen`. (host OK: `e92b13f` + Caso 63 — `.so` com `DT_SONAME` de gadget é barrado no preflight do ELF, sem abrir o arquivo; device: pendente — só o ELF sintético do harness foi usado, o binário real de 25MB nunca passou pelo loader.)
- [ ] [NAO VERIFICADO: tamanho do gadget em RAM + detecção por anti-tamper.] (Falta medir com o binário real de 25MB rodando; o `fetch_frida_gadget.sh` dá o tamanho em disco, não o de RAM.)
- [~] Suporte a `frida-il2cpp-bridge` (scripts da comunidade que usam `Il2Cpp.perform`). (host OK: `tools/bundle_frida_script.sh` gera UM .js autocontido com a ponte embutida — versão 0.14.0 pinada por sha256 em `tools/frida_il2cpp_bridge.lock`, licença MIT guardada; o fixture versionado passa em `test/frida_bridge_bundle_test.sh` e o `Il2Cpp.perform` roda de verdade no node contra um stub; a rede é só na ferramenta, nunca no gate. O caminho existente do Manager serve: o .js entra como `FRIDA_JS` e o gadget do u_frida roda o primeiro `*.js` da pasta. (Falta: **nada rodou num aparelho** — o bundle real de 168 KB foi gerado no host e inspecionado, mas nunca carregado pelo gadget num jogo IL2CPP de verdade; falta um jogo-alvo real com `libil2cpp.so` carregada, confirmar que o `Il2Cpp.perform` do script da comunidade acha o jogo e medir o custo de RAM do bundle. Também falta decidir a política de versão: pinada por lockfile atual, enquanto a bridge é atualizada pela comunidade.)
- **Verifica:** script `.js` simples que hooka um método do SA2 com efeito observável (escreve arquivo em `files/bepinex/` — `console.log` vai pra `/dev/null`), instalado por `tools/deploy_frida.sh`, em Permissive e Enforcing; depois o mesmo instalado pelo Manager.

### F12 — Mods `.dll` IL2CPP (BepInEx 6 IL2CPP / MelonLoader IL2CPP) — spike primeiro
- [~] Spike: carregar o runtime .NET (CoreCLR) **dentro do processo pelo nosso Zygisk** (sem container), reaproveitando o que o NextBep/FusionCore já portou (CoreCLR android-arm64 + Il2CppInterop + HarmonyX). Medir tamanho, RAM, tempo da 1ª execução (geração dos assemblies proxy). (Pesquisa feita 2026-09-26; fonte e veredito em `raw/f12-spike.md`, no vault: **viável com PC**. O runtime reaproveitável é o payload do LemonLoader `v0.7.3-android.3` = **21,75 MB**, com CoreCLR .NET 10.0.13 android-arm64 dentro. O JIT **não** é bloqueio: a fonte externa `private/app.te` (linha 204) dá `allow appdomain self:process execmem` e o nosso `module/sepolicy.rule` já tem a mesma linha. O bloqueio real é a **G4**: LemonLoader e NextBep instalam **patchando o APK** (`libmain.so`/`libfusion.so`), e não existe caminho Zygisk publicado. Device: pendente por definição — o spike é pesquisa, nada rodou.)
- [ ] Go/no-go com números. Se go: fase de implementação separada. (Falta só a medição que o device dá: RAM com o CoreCLR dentro do jogo, tempo da 1ª execução e se o `bepinex_mod_file` vai precisar de `execmod`. Antes disso, escolher o caminho do interop: pré-gerar no PC por jogo, usar o `PregeneratedInteropData` do LemonLoader, ou gerar no device — hoje ninguém gera no device.)
- Limite real: só roda `.dll` feito pra versão **IL2CPP** do jogo. Mod de PC Mono não entra aqui.

### F13 — Mods `.dll` Mono em jogo Unity Mono
- [~] `mono_min.h`: detecção Mono/IL2CPP por mapas e resolução atômica dos 7 símbolos Mono (host verificado). (Falta: integrar o runtime ao loader; abrir assembly real com `mono_domain_assembly_open`; HarmonyX; validação em device com jogo Unity Mono.)
- [ ] Compat BepInEx 5 mínima (`BaseUnityPlugin`, `Logger`, `Config`) pra mod de PC do mesmo jogo carregar sem recompilar, quando o jogo Android também é Mono. (Depende do item acima.)
- Precisa de jogo-alvo Unity Mono real pra validar.

### F10 — Depois (fora do caminho crítico)
- `mono_min.h` (Unity Mono), com jogo-alvo real. (mesma lacuna da F13; o `il2cpp_min.h` do nosso lado é só IL2CPP)
- Helper de thread principal Unity (habilita `u_speed`/`u_fps` universais).
- Stream/console no genérico sem abrir Termux por cima do jogo.
- Unificar o caminho BC no genérico (decisão do usuário).

## Ordem, dependências, dono

| Fase | Depende de | Agente | Branch/worktree | Estado (2026-09-26) |
|---|---|---|---|---|
| F1 loader | — | kilo | `uni/f1-zeroconfig` | mergeado, device validado |
| F2 SDK + kit | F1 (log C1) | OpenCode | `uni/f2-sdk-v2` | mergeado, sem device |
| F3 u_dump | contrato C5 | freebuff | `uni/f3-udump` | mergeado, device validado |
| F4 u_patch | contrato C4 | kimi | `uni/f4-upatch` | mergeado (`6589f2f`), sem device — volta pro device com o roteiro do `DEVICE-ROUND.md` |
| F5+F6 Manager | contratos C1–C6 | Antigravity | `uni/f5-manager` | mergeado, sem device |
| F7 módulo + F8 docs | F5 | OpenCode / hermes | `uni/f7-module`, `uni/f8-docs` | mergeados, sem device |
| F9/F9b pesquisa + u_noads | — | hermes | `uni/f9b-noads` | mergeado (`f08164b`), sem device |
| F11 u_frida | — | kimi | `uni/f11-frida` | mergeado, sem device |
| guarda do gadget | F11 | hermes | `uni/loader-frida-guard` | mergeado (`e92b13f`), sem device |
| T1 device | tudo acima | OpenCode | `uni/t1-devicetest`, `uni/sim-fast`, `uni/sim-safe` | mergeado, sem device (simulador, fake clock, stdin) |
| T2 verify_all | — | — | `uni/t2-verify` | mergeado, roda no host |
| G7 release-key | F5 | Claude Code | `uni/release-key` | mergeado (`192e967`), sem device (falta assinar com a chave real) |
| C4 compartilhado | contrato C4 | Claude Code | `uni/c4-shared` | mergeado: Manager e u_patch leem a mesma fixture |
| G7 release reproduzível | F5/F7 | Claude Code | `uni/g7-release` | mergeado (`f36c650`), builds locais reproduzíveis em host; sem tag, publicação ou validação em aparelho |

F3, F4 e F5 andam em paralelo contra os contratos. F2 dá `mod_common.h`: até ele chegar, F3/F4 usam o próprio log mínimo e trocam depois. Integração: merge em `feat/generic-pkg-mods` (sem push sem autorização do usuário), revisão cruzada por outro agente, validação no device (SA2 + BC) pelo orquestrador.

## Riscos

- `il2cpp_domain_get` antes do `il2cpp_init` crasha o jogo (sa2ammo). Todo mod usa `il2cpp_boot()`: biblioteca e runtime compartilham um deadline de 240s, com polling a cada 200ms e logs periódicos/motivo explícito ao desistir. `u_frida` usa 10s e carrega o gadget mesmo sem runtime; scripts que dependem de `Il2Cpp.*` podem não funcionar nesse caso. A rota usada para abrir a biblioteca (`__loader_dlopen` ou fallback `dlopen`) também fica no log.
- Patch de instrução (F4 `return`): método minúsculo (< 8 bytes) ou inline → não patchar, logar.
- SELinux: o jogo **não** lê a árvore de mods por caminho — recebe o FD (SCM_RIGHTS) do companion, e o `getattr` do `fstat` do linker é o que a regra cobre. **Escrever** `/data/data/<pkg>/files` é do próprio app, ok.
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
