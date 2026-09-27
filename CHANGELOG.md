# Changelog — bepInEx-termux

Formato: `Added / Changed / Fixed / Known issues` por release.
Primeira release pública: `v0.3.0` (casa com `BC_LOADER_VERSION` em
`jni/main.cpp` e com o `módulo carregado — v0.3.0` visto ao vivo).

## [Não lançado]

### Para o usuário
- [HOST] O Manager agora lista e identifica mais engines de jogos, explica em português o que cada tipo de jogo permite fazer e evita tratar qualquer app com bibliotecas nativas como jogo. O app compila e a detecção tem testes JVM; instalação e uso no celular ainda não foram verificados. (`59fe913`, `5ed8019`)
- [HOST] O Manager reúne instalação de mods `.bmod` e arquivos compatíveis, controles para ativar/desativar, opções, logs e criação de regras sem código; o fluxo de leitura do jogo gera e limpa o scanner automaticamente, inclusive ao falhar. APK e testes JVM cobrem esses caminhos, mas o fluxo ainda não foi executado no celular. (`59fe913`, `9bc95f8`, `368406d`, `3884e54`, `ca4354b`)
- [HOST] O Manager identifica o tipo de arquivo pelo CONTEÚDO ao instalar (mod nativo ARM64, regras, script Frida, pacote `.bmod`, mod de PC, APK/OBB do jogo, save) e explica em português o que dá para fazer com cada um, com corpus de teste de 26 arquivos reais e proteção contra pacote com caminho `../` dentro. (`02c501a`)
- [EXPERIMENTAL] Se o seletor de arquivos do celular não devolver o arquivo, o Manager oferece escolher direto da pasta Download/Documents. Esse caminho alternativo não foi testado no celular. (`672eed2`)
- O tradutor de mods `.dll` do BepInEx para regras `.bpatch` entra com limite honesto: reconheceu padrão traduzível em 0 de 378 mods reais de 30 jogos (teto de ~3% dos mods; só troca de valor simples de retorno/campo constante). A medição completa está em `docs/DLL-COVERAGE.md`. (`4207b99`)
- [HOST] O guia passo a passo do Mod Maker documenta o fluxo completo de criação de regras, incluindo o que só o autor do `.bpatch` sabia. (`860adcc`)
- Foi adicionado o motor de regras IL2CPP para alterar retornos, multiplicar valores e fixar campos; o formato é compartilhado entre o Manager e o carregador para evitar divergências. Os testes de host incluem harnesses e execução de thunks em QEMU; `return`/`mul`/`static` ainda não foram testados no aparelho; `field` foi corrigido (`e4f2e65`) e o log no celular confirma a leitura, o efeito dentro do jogo ainda não foi verificado. (`6589f2f`, `bf9eee3`)
- [HOST] `u_noads` tenta fechar anúncios forçados usando o callback de fechamento do SDK, cobrindo seis SDKs conhecidos. Os testes verificam alvos e fechadores, mas o uso no SA2 não foi confirmado em aparelho. (`f08164b`)
- [HOST] O carregador aceita scripts `.js` do Frida apenas em modo script; o instalador e a checagem do binário do gadget têm testes de host. A execução no celular continua experimental e não foi validada. (`023e4c9`, `744bb6d`)
- A extensão dos arquivos de regras do motor declarativo passou de `.patch` para `.bpatch`, para não colidir com o arquivo de diff do git e confundir quem olha a pasta de mods. Nada muda para quem usa o Manager: ele reconhece o conteúdo do arquivo, não o nome, então um arquivo de regras com a extensão antiga ou sem extensão continua sendo identificado e instalado como `<id>.bpatch`. O formato ainda não saiu em release, então não há arquivo antigo para migrar. A mudança foi verificada em testes de host e no gate, não em aparelho. (`6589f2f`)


### Correções
- [HOST] Os mods passaram a compartilhar o mesmo caminho de log; a espera pelo pacote e as mensagens de timeout foram corrigidas para refletir o comportamento real. Os testes de host cobrem o helper de log, e o gate compila os componentes; não é uma nova validação em aparelho. (`df40b73`, `638b05f`, `09bcd74`)
- [HOST] Foram corrigidos fluxos de root do Manager, incluindo chamadas repetidas, cadeias `chcon`, entrega de comandos por stdin e preservação de `resources.arsc` no APK para instalação em versões atuais do Android. Testes de shell/JVM e checagens do APK cobrem esses casos. (`3f1bdef`, `368406d`, `3884e54`, `ca4354b`)
- [HOST] A instalação do APK passou a ser verificada por alinhamento e estrutura instalável antes de ser aceita pelo gate. (`368406d`, `3884e54`)
- [HOST] O simulador do teste de restauração ganhou relógio falso e não fica preso ao stdin herdado; os limites de timeout separam a simulação dos testes no aparelho. (`c3da4cc`, `2b3d8c6`)

### Segurança/robustez
- [HOST] Antes de carregar mods, o loader confere o ELF e recusa bibliotecas com SONAME do Frida Gadget, mesmo se forem renomeadas; outra checagem confirma o símbolo necessário no loader de Battle Cats. Os casos são cobertos pelo harness, não por um teste em aparelho. (`744bb6d`, `81efee2`)
- [HOST] O kit de teste de aparelho agora exige backup verificável antes de restaurar dados, valida o hash de cada arquivo restaurado e informa divergências. Os testes de restauração são simulações de host, não uma rodada no celular. (`1224346`, `05b7138`)
- [HOST] O gate universal reúne builds, testes do Manager e dos mods, verificações de shell e análise estática pinada; também detecta IDs duplicados no harness e valida referências documentais a arquivos, linhas e trechos citados. (`ad46276`, `aa33ac1`, `66280c0`, `73e45b5`)

### Para desenvolvedores
- [HOST] Há um comando local de release que gera o zip Magisk, o APK do Manager e os módulos distribuídos, com `SHA256SUMS`, metadados de build e dependências externas pinadas. A reprodutibilidade foi verificada em builds de host; publicar ou etiquetar uma release não faz parte desta mudança. (`f36c650`)
- [HOST] O Manager pode ser assinado com uma chave fixa fornecida pelo usuário, sem expor a senha do keystore na lista de processos; o teste usa chaves temporárias e não inclui keystore no repositório. (`192e967`)
- [HOST] Foram adicionados documentação do SDK e do formato `.bmod`, fixture C4 compartilhada, scanner empacotado a partir do fonte e roteiro/gate de referências em documentação. (`0d989c3`, `bf9eee3`, `9bc95f8`, `73e45b5`)
- [HOST] O roadmap recebeu sincronizações com evidência por item e o parecer do spike de runtime .NET; esses registros não significam que a execução ou medição em aparelho foi feita. (`ea957ae`, `7c66679`)
- Relatório da rodada no aparelho (POCO C75, 2026-09-26): o que funcionou, o que quebrou e o que ficou pendente, com evidências. (`74ba919`)

### Problemas conhecidos
- A regra de campo (`field`) foi corrigida (`e4f2e65`); no celular o log confirma a leitura, o efeito dentro do jogo ainda não foi verificado. (`6589f2f`)
- O frida-gadget **17.19.0** (released 2026-09-25) crasha com SIGSEGV (null-pointer deref, fault 0x38) na própria inicialização no POCO C75 + SA2 — antes de qualquer script. O pin foi trocado para **17.18.0** (released 2026-09-09), que carrega e roda scripts sem crash e mantém a API JS do 17.x (Module.getGlobalExportByName etc.). O 16.7.19 também funciona, mas foi descartado como pin porque a API JS do 16.x é incompatível com scripts da comunidade frida-il2cpp-bridge escritos para o 17.x. Se uma versão futura corrigir o bug, atualize o `tools/deps.lock` com a mesma prova de device. (tombstones: `~/Documentos/mods/_backup_device_2026-09-27/evidence/tombstone_f11_frida.txt` e `tombstone_f11_t1_minimal.txt`)

### Histórico preservado do Unreleased anterior

### Added
- sa2content (new mod, Swamp Attack 2 1.3.9): content through the game's own balance system, memory only (no `OnReceivedBalanceFromGrid`, which writes a local patch file). 31 embedded patches (`tools/gen_patches.py` from a runtime snapshot): one unused primary weapon unlocked per redneck at level 1 (9), effect fusions (Shotgun frost, DoubleShotgun poison, Kalashnikov electric, TankBusterRifle radioactive), L05/L10/L15 of chapters 2+ end with the previous chapter's boss (18), forced interstitial ads between levels blocked (rewarded ads untouched). Reapplied after `GameBalancer.TryApplyPendingPatches`. Applied on device 2026-09-25 (ld 18 / red 9 / wep 4, Apply ok); in-game look not yet confirmed.
- sa2content: playable Unknown (Slow Joe body + runtime Shotgun clone with every damage effect but mind control), on by default (`-DSA2_ENABLE_UNKNOWN=0` turns it off). Validated on device 2026-09-25 after the `copy_field` fix (the earlier map crash was the mod storing a stack address into reference fields).
- mods/common/il2cpp_min.h: header-only il2cpp API + boot (linker-namespace fix) shared by the generic-path mods; sa2ammo moved onto it.
- loader: per-package mods on the generic path. Every `.so` in `/data/local/tmp/mods/<pkg>/` is `dlopen`ed when an allowlisted package starts, before engine detection (Unity/IL2CPP games expose no `Java_*`, so detection alone would leave them dormant). These mods are self-contained (their constructor does the work), no `bc_mod_register`.
- sa2ammo (new mod, Swamp Attack 2 1.3.9): unlimited ammo on the player's primary weapons by turning on the game's own `WeaponInfo.unlimitedAmmo` flag from `SelectWeapon`/`ReloadWeaponClip` hooks. Classes, methods and field offsets are resolved through the exported il2cpp API. Validated on device 2026-09-25 (user confirmed ammo no longer drops).
- kungfux: remaining community cons of Kung Fu Cat X #132 (vault `context/kfx-cons-CONSOLIDADO.md`): multi-hit merged into one hit at foreswing 11 (the 3rd hit no longer carries the damage and whiffs after KB; cycle kept at 90f, col4 40 on all forms), HP x1648/999 (Lv30 ~28k, Dancer TF 27.5k), range 300 -> 350, cost 1560 -> 1200 (col6 is x100 after load in 0x793f44, scaled proportionally). Per-hit readers proven through the column tables at 0x1f8310/0x1f8328/0x1f8340. Warp immunity kept (contested con).
- mechabun: every remaining sourced proposal at its highest value ("se tem fonte adiciona"): TF HP 520.5k Lv50 / ATK 18.75k Lv30, freeze 38%, wave lv5 normal 100%, strengthen x2, explosion 100%, savage 20%, shield pierce 20%, KB/slow/curse 20%, red/black/alien targets, zombie killer, resistant, massive, curse immunity, 3-hit TF. Missing parameters use vanilla data conventions (explosion start = range, savage +200%, slow 60f, curse 90f).
- kungfux (new mod, `02_kungfux.so`): Kung Fu Cat X #132 without the wiki cons (speed 10, KB 3, area attack, 90f attack cycle); no hook, patches the unit struct from a watcher thread once vanilla values are seen. Cost still unchanged (no struct reader).
- mechabun: sources re-verified in a real browser (context/mecha-bun-fontes-verificadas.md, OpenCode + freebuff English originals): KB count 4, survive 100% and wave immunity (reddit 1ftdnvo t1_lpt0btb), explosion immunity (1o3deph OP; explosion, not surge). Behemoth Slayer source corrected to 1jbfu1p (fandom 734592 was not a real quote).
- mechabun: target traits red/black/metal/traitless/angel, resistant, massive damage, colossus slayer and soul strike (battle readers pending proof).
- mechabun: thread 1qv6rno checked in a real browser: mini-wave 100% lv2, dodge 20%/30f, toxic immunity, weaken 25%/90f/50%.
- mechabun: second source pass (context/mecha-bun-fontes-kimi-2.md): removed survive and wave/explosion/toxic/slow/weaken immunities; weaken 50%/90f; behemoth slayer restored (fandom source).
- mechabun: removed everything without a valid source (targets red/black/metal/traitless, resistant, massive, colossus, soul strike, behemoth, wave attack, dodge, KB count 4); added speed +6 (reddit 1qv6rno).
- mechabun: range 190 -> 250 (user decision; 265 had no traceable source).
- mechabun: wave 20% mini -> 30% full wave, dodge 30% -> 50% (user max-value rule).
- **`mods/mechabun` teto de nível 60+90**: reescreve em memória a linha
  426 do `unitbuy.csv` (parse 0x7936b8, tabela `+0x4ACB8`, XOR key
  `row+0xfc`; leitores col49 0x3e85f8, col50 0x3e7f7c, col51 0x3e80d8).
  Vanilla EN lido no device: col49=30 col50=50 col51=0. Validado: save
  editado pra 60+90 aparece 60+90 no jogo.
- **`mods/mechabun`**: KB 4, Freeze 20%/90f, Crit 25%, Weaken 100%/120f/50%,
  Survive 100%, imunidades Freeze/Slow/Weaken — cada coluna com leitor em
  batalha provado no build `338b0601`. Mini-wave 20%, Dodge 30%/90f,
  range 265 (ainda sem leitor).

### Changed
- **`mods/mechabun`**: HP True Form ×25/9 = 300k Lv50. O ×325/54 antigo
  usava a base Lv50 da forma normal (86,4k) e dava ~650k real (base TF =
  108k).

### Removed
- **`mods/mechabun`**: hook "D2-fix" em 0x8789e8 — é o getter de freeze
  time (col26), não de dano; escalaria o freeze 9/5. O dano normal vem de
  `calc_atk` 0x872440, que lê col3 do struct já patchado.

### Verified
- **`mods/mechabun` ATK em batalha**: hook log-only temporário em
  `calc_atk` (não commitado) mostrou `args=0,426,2,20,...` →
  `ret=10800` no builder de batalha (site 0x7c250c) = 900×12 (Lv20 com
  tesouros); vanilla seria 6.000. +80% chega ao dano real.

### Fixed
- loader: a package with its own mods dir (`/data/local/tmp/mods/<pkg>`) no longer connects to the companion (the companion opened the Termux console over the game, so Swamp Attack 2 kept bouncing to Termux) and skips the generic Cocos2d-x engine detection/log hooks.
- mods/common/il2cpp_min.h: `il2cpp_boot` waits for `il2cpp_get_corlib()` before `il2cpp_domain_get()`, which allocates the domain through the GC when it does not exist yet and crashed the game when a mod loaded from Zygisk ran before `il2cpp_init` (Frida injection came late and hid it).
- loader: per-package generic mods now load in alphabetical order (`scandir` + `alphasort`); plain `readdir` order was arbitrary. README states the real `bc_mods/` order (`strcmp` on the name, zero-padded prefixes; manifest `requires` still reorders), not "numeric prefix".
- sa2ammo / sa2content: each Dobby hook is installed and logged on its own; one failing hook no longer aborts the others (review by kilo, `raw/review-sa2-kilo.md`).
- sa2content `gen_patches.py`: snapshot read once with closed handles; a missing category or fusion damage type stops with a clear message instead of `AttributeError`/`StopIteration`.
- **`mods/mechabun` `verify_unit_base`**: device real (build `338b0601`)
  leu range=760 (190×4) com ATK 400/500 exatos — offset certo, struct
  guarda range em unidade interna ×4. Check rejeitava e o patch inteiro
  ficava vanilla. Agora aceita 190 ou 190×4; par de ATK segue provando o
  offset. Validado em batalha: `DEBUG form=0 ATK raw=400 pos-scale=720`,
  `design ideal comunitario aplicado`, zero crash.

## v0.3.6 — 2026-09-22

Segunda rodada de revisão: OpenCode + hermes em paralelo (achados
verificados um pelo outro via freebuff antes de aplicar).

### Fixed
- **`BC_LOADER_VERSION` desincronizada da tag** (`jni/main.cpp`): v0.3.5
  foi taggeada com a string ainda em `v0.3.4` (mesmo erro do v0.3.0→v0.3.4
  se repetindo — a string nunca mais bateu com a tag do dia em que subiu).
  Agora em `v0.3.6`, casando com esta tag.
- **Regressão real introduzida pelo próprio fix do v0.3.5**
  (`jni/main.cpp`, `load_dynamic_mods`): o `memset(g_hook_callbacks, ...)`
  que resolveu o double-registro em reload rodava ANTES do `opendir()` —
  se o diretório de mods falhasse por motivo transiente (não só "mods
  removidos": `EMFILE`, permissão passageira, I/O), a função zerava os
  callbacks de mods já ativos e retornava sem re-registrar nada. Reset
  movido pra depois dos dois early-returns — só dispara quando a função
  vai de fato reconstruir a partir do que achou no disco.
- **`README.md`**: contagem de assertions do harness (232 → **234**,
  desatualizada desde os 2 casos novos do v0.3.5).
- **`docs/ROADMAP.md`**: título "Roadmap + TODOs" não batia mais com o
  conteúdo (11/11 itens fechados, 0 pendente) — retitulado.
- **`mods/mechabun/deploy.sh`**: `PACK_SRC_ORIGINAL` não tem mais default
  de path pessoal hardcoded (repo é público) — vazio por padrão, só
  funciona com a env setada explicitamente; mesmo comportamento prático
  de "arquivo ausente" pra quem não é o autor original.

### Verificado sem ação (achados já corretos, confirmado por 2 revisores)
- Offset `0x9e568`/`0x9e318` no README do mod: já correto (build-id JP
  explícito + correção em negrito na mesma frase + cross-ref).
- `bc_repatch_hook`: lógica de referência deliberada, testada pelo
  harness; `main.cpp` usa implementação própria documentada.
- Menção a "5 TODOs" do `symbol_scan.cpp` no CHANGELOG v0.3.5: descrição
  histórica dentro da própria entrada que anuncia a deleção do arquivo,
  não referência órfã.

Build (`ndk-build -B -j4`, via agente com NDK real): 0 warnings além do
benigno conhecido. Host harness: 234/234 assertions.

## v0.3.5 — 2026-09-22

Revisão forense de 5 agentes (OpenCode, hermes, freebuff, devin, kilo) +
verificação direta de cada achado contra o código real antes de aplicar
(vários achados já estavam resolvidos por releases anteriores — só os
confirmados por leitura direta viraram fix).

### Fixed
- **`BC_LOADER_VERSION` desatualizada** (`jni/main.cpp`): dizia `v0.3.0`
  desde a primeira release, nunca acompanhou v0.3.1-v0.3.4 — log de boot
  e o companion reportavam versão errada pro Termux.
- **`bc_repatch_hook` com endereço placeholder perigoso**
  (`jni/bc_hook_logic.h`): instalava hook sempre em `(void*)1` — função
  sem call site em produção hoje, mas um SIGSEGV garantido esperando um
  caller futuro real (Dobby). Assinatura agora exige `target` explícito
  do caller (endereço já resolvido); `test/selftest_harness.cpp`
  atualizado (Casos 39/40 + 1 caso novo pra `target=nullptr`).
- **Double-registro de hook em reload de mod dinâmico** (`jni/main.cpp`,
  `load_dynamic_mods`): sinal `reload_mods` reexecuta a função inteira
  sem resetar `g_hook_callbacks` — um mod já ativo tinha seus callbacks
  prefix/postfix reempilhados a cada reload (dispatch duplicado; após
  `HOOK_MAX_CALLBACKS` reloads, falha silenciosa por slot cheio). Fix:
  `memset(g_hook_callbacks, ...)` no topo da função (rebuild completo a
  cada chamada, mesma semântica já documentada pelo caller).
- **`BC_SCHEMA` duplicado** (`jni/main.cpp` + `jni/companion.cpp`): cada
  arquivo mantinha sua própria cópia manual do array (comentário já
  admitia "tem que bater... se adicionar chave, adicionar nos DOIS").
  Definição única movida pra `jni/bc_mods_conf.h` (SSOT) — os dois `.cpp`
  ganham cada um sua cópia `static const` via include, sem risco de
  drift na manutenção.
- **`jni/symbol_scan.cpp` removido**: arquivo inteiro era esboço não-
  funcional (autodocumentado "ESBOÇO — não 100% funcional", signature
  bytes placeholder, 5 TODOs em aberto), nunca incluído no `Android.mk`
  (só `main.cpp` + `companion.cpp` compilam). Comentário stale em
  `main.cpp` que referenciava o arquivo também removido.
- Nota de escopo adicionada ao changelog do v0.3.3 (ver abaixo) —
  timeout de 60s é exclusivo do `event_thread` (Battle Cats), não do
  caminho genérico multi-jogo.

### Verificado sem ação (achados já resolvidos ou falso-positivo)
- `mod_api_resolve_pattern`/`mod_api_install_hook` "sem call site": usados
  como ponteiros de função no struct `bc_mod_api` exposto a mods `.so`.
- `write_patches_snapshot`: já valida `w == hw` antes do rename atômico
  (fix real do v0.3.2, achado batia com estado anterior ao fix).
- Offset `0x9e568` no README do mod: já documentado lado a lado com o
  valor real `0x9e318`, com explicação do drift entre builds.
- `deploy.sh` `PACK_SRC_ORIGINAL`: já overridable via env, mesmo padrão
  aceito de `BCDATA_DIR` (documentado desde v0.3.0).
- `docs/ROADMAP.md` item 3.7: nunca foi marcado `[x]`, já consta como
  "NÃO COMPROVADO" no texto.
- `test/selftest_harness.cpp`: usa helper `check()` próprio, não
  `assert()` (imune a `-DNDEBUG`); harness host-only é design
  documentado no topo do arquivo, não lacuna de build.

Build (`ndk-build -B -j4`, verificado por agente com toolchain NDK): 0
warnings além do `-static-libstdc++` benigno conhecido. Host harness
(`g++ -std=c++17`): 234/234 assertions (232→234, 2 casos novos).

## v0.3.4 — 2026-09-22

Revisão pós-v0.3.3 (freebuff). Dois fixes:

### Fixed
- **Regressão do reset de `consecutive_errors`** (`jni/companion.cpp`,
  accept loop): o reset no caminho de sucesso existia só no comentário —
  a linha tinha se perdido, e o daemon voltava a morrer após 21 falhas
  *totais* na vida do processo em vez de consecutivas. Reset reposto
  fora de qualquer branch (accept com sucesso quebra a sequência mesmo
  que o cliente caia depois no gate de UID).
- **Comentário stale no caminho genérico** (`jni/main.cpp`,
  `generic_event_thread`): dizia "janela maior que os 5000ms do Battle
  Cats" — com o v0.3.3 o BC espera 60s (`TARGET_LIB_WAIT_MS`), 13x maior
  que os 8s genéricos, não menor. Texto corrigido.

## v0.3.3 — 2026-09-22

### Fixed
- **`libnative-lib` não carrega a tempo em alguns boots**: `wait_lib_loaded`
  tinha timeout fixo de 5s antes de desistir permanentemente pro resto da
  vida daquele processo (mod fica dormant, sem segunda chance). Achado real
  em device: num boot a lib apareceu só 1.5s depois do timeout (quase
  pegou); em outro boot, no mesmo device, não carregou nem depois de várias
  dezenas de segundos (app fica em splash/menu antes de inicializar o
  engine nativo, dependendo do caminho de boot). Timeout subiu pra 60s —
  poll (`dl_iterate_phdr` a cada 8ms) é barato numa thread dedicada, não
  bloqueia nada mais.

### Escopo (achado de review, kilo)
- Mudança confinada ao `event_thread` — caminho ESPECÍFICO do Battle Cats
  (`TARGET_LIB="libnative-lib.so"`). O caminho genérico multi-jogo
  (`generic_event_thread`, detecção de engine via `bc_wait_engine_detect`
  pra qualquer app Cocos2d-x/C++ na allowlist) usa timeout próprio de 8s,
  não tocado por este fix — threads e timeouts são independentes por
  design (Battle Cats sempre teve lib nomeada conhecida; genérico não).

## v0.3.2 — 2026-09-22

Revisão forense de 5 agentes em paralelo (OpenCode, hermes, freebuff,
devin, kilo), cada um com ângulo diferente. Ver mensagem do commit
`1955810` pra lista completa dos 18 achados reais corrigidos (bugs de
código, docs/consistência, scripts shell/python) — resumo abaixo.

### Fixed
- `qsort_strcmp` (loader de mods dinâmicos) deferenciava `char[256]` como
  `char**` — crash real com 2+ mods dinâmicos instalados.
- `write()` de snapshot aceitava short-write parcial como sucesso.
- Registro de callback de hook (`hook_register_prefix/postfix_by_slot`)
  aceitava função nula sem checar — null-call garantido no dispatcher.
- `entry_called` do loader dinâmico virava `true` mesmo quando a entry do
  mod nunca foi chamada de fato (`run_entry == nullptr`).
- `consecutive_errors` do accept loop do companion nunca resetava — matava
  o daemon após 21 falhas *totais* na vida do processo, não consecutivas.
- 2 handlers do companion (`toggle_mod`/`set_mod`) tinham um caminho de
  erro que não respondia nada ao cliente — travava até timeout de 3s.
- Redirect de ícone (D16.1) do Mecha-Bun não tinha o mesmo fallback pro
  original que o redirect do pack (D12) já tinha — ícone ausente no
  device quebrava o `fopen`, retornando `NULL` pro jogo.
- 2 links markdown quebrados pro `NOTICE.md`; `.gitignore` com `obj/`
  duplicado; 4 scripts Python com shebang de execução direta sem `+x`.
- `bepin-alert.sh`: substring bash-only rodando sob shebang `sh` (dash) —
  `Bad substitution` real no Termux; `bepin-watchdog.sh`: sem trap pro
  wake-lock, sem checar `logger`/`python3` ausentes antes de reportar
  status errado do companion.
- `deploy.sh` do mechabun: `TERMUX_PY` hardcoded, `stat -c%s` sem fallback
  BSD, resposta do companion não validada antes de declarar sucesso,
  arquivos residuais deixados no device após deploy.
- `d12_transform.py`: 2 validações críticas via `assert` (removido com
  `python -O`) viraram `raise` explícito.

### Changed
- Grafia do nome do projeto normalizada pra `bepInEx-termux` em prosa/
  comentários (mantido o slug real do repo GitHub, `bepinEx-termux`, nos 2
  scripts Termux que resolvem o path de clone real no disco).
- Offset de struct no README do mod citado sem contexto de build,
  corrigido com nota do valor real pro device de teste.
- Contagem de assertions do README raiz corrigida (232, confirmado
  rodando o test harness de verdade).

## v0.3.1 — 2026-09-21

### Changed
- `mods/mechabun/assets/` (ícones + script gerador) movido pra
  `mods/mechabun/tools/` — repo mais limpo, MD5 dos PNGs idêntico,
  `deploy.sh` atualizado pro novo caminho, recurso visual (redirect D16.1)
  funciona igual num clone novo.
- Mod Mecha-Bun reposicionado na documentação como mod de teste/prova de
  conceito da infra do bepInEx-termux, não mod de buff pra jogar — zero
  conteúdo técnico alterado (known issues, offsets, tabela D1-D17).

### Fixed
- Exemplo de comando `adb push` no README do mod ainda citava o path
  antigo `assets/`.

## v0.3.0 — 2026-09-21

### Added
- **Log fiel ao BepInEx real**: cada mod dinâmico loga com seu próprio
  nome como fonte (equivale ao `ManualLogSource` por plugin), extraído da
  convenção `"[nome] mensagem"`; banner inicial com source `"Preloader"`
  (ground-truth checado no fonte do BepInEx — `"Chainloader"` nunca existiu
  como Source); layout `[Nível,-7:Fonte,10]` idêntico ao
  `LogEventArgs.ToString()`; filtro default sem `Debug` (mesmos defaults de
  `[Logging.Console]`/`[Logging.Disk]`). Desvio deliberado documentado:
  timestamp `HH:MM:SS` na frente (BepInEx não mostra hora em nenhum sink).
- **Mutex no log em disco** (`pthread_mutex_t`, sem `<mutex>` pra não inchar
  o `.so`): par `fwrite`+`fflush` atômico entre threads do jogo.
- **Buffers de log aumentados** (`line` 224→448, `body` 160→384): mensagens
  reais de erro (ex. mismatch de `STAT_BLOCK_OFF`) passavam de 300 chars e
  eram truncadas — confirmado no capture ao vivo.
- **Mod Mecha-Bun (#426, prova de conceito da infra — não mod pra jogar)**:
  veículo de teste de ponta a ponta (hooks, log, deploy, hot-reload) com
  conteúdo realista: hook de stats via loader CSV (HP/ATK/range/
  recarga/imunidades/wave/strengthen/dodge/slayers/traits, até 3 formas),
  hook de dano D2-fix (getter `0x8789e8` → campo `+0x83774`, mesma escala
  9/5 do ATK), redirect de ícones D16.1 e de pack de animação D12 via hook
  de `fopen` (com fallback pro original + checagem de tamanho do pack),
  retry de hook no hot-reload, e `verify_unit_base` (assinatura sanity
  contra `unit427.csv` real antes de escrever — mismatch vira stats
  vanilla, nunca corrupção de memória).
- **Infra de deploy do mod**: `mods/mechabun/deploy.sh` robusto
  (`mktemp`+`adb push`+`su`, sem `run-as` frágil; path do pack original
  overridable via env `PACK_SRC_ORIGINAL`),
  `mods/mechabun/tools/d12_transform.py` (gera o pack D12 sob demanda a
  partir do pack original — requer `pycryptodome` no host; dir base
  overridable via env `BCDATA_DIR`), `mods/mechabun/PLAYTEST_LEARNINGS.md`
  (log de sessão real em device físico).
- **Termux**: `termux-boot/bepin-watchdog.sh` (monitora socket do companion)
  e `termux-shortcuts/tasks/bepin-alert.sh` (notificação só em
  Warning/Error/Fatal). Paths do repo overridables via `BEPIN_TERMUX_DIR`.

### Changed
- `STAT_BLOCK_OFF` 0x9e568 → **0x9e318** (RE direta no `.so` do device;
  update do jogo moveu o offset — ver comentário no código).
- Strengthen mult 150 → **50** (campo é bônus-percentual, cf. tbcml
  `Strengthen.multiplier_percent` + design "+50%"; 150 over-buffava 2.5x).
- Trails de traits D16 aplicados nas 3 formas (não só True Form).
- Comentário de convenção de mask AOB em `bc_pattern_scan.h` (0/não-zero,
  não `"xx??xx"`).

### Fixed
- **Race TOCTOU em `bc_mod_register`** (crash real `SEGV_ACCERR` em device):
  load boot × sinal de hot-reload viam `g_orig==nullptr` juntos e chamavam
  `install_hook` concorrente no mesmo alvo (Dobby sem lock + `mprotect`
  RWX). Fix: mutex cobrindo a função inteira + atomics nos contadores.
- Crash com `api->log(level, nullptr)` (guard de nulo).
- `COL_ATTACK_INTERVAL` documentado como no-op pro Mecha-Bun (raw 0) em vez
  de alegar efeito; D12 v1 apontava pra unit errada (425 → 426 correto).

### Known issues (honestos, não features)
- **Dano real do Mecha-Bun em batalha NÃO confirmado**: o hook D2-fix
  instala e dispara normalmente (confirmado no log), mas o valor lido do
  getter de dano-base no momento do hit é sempre `orig=0` nos testes ao
  vivo — não é que o hook falhe em disparar, é que a fonte de dado que ele
  lê retorna 0 nesse ponto, então não há prova de que o dano real em
  combate mude. Campos ATK/RANGE/RECHARGE +
  traits (relic/behemoth/sage/mini-wave/toxic) do struct **não têm leitor
  no jogo** (busca exaustiva de xrefs) — escrever certo ≠ funcionar. Ver
  `context/battlecats-mechabun-atk-dead-struct.md` e tabela "Leitor
  confirmado" no README do mod.
- **`STAT_BLOCK_OFF` deriva por update do jogo**: se mover de novo, o mod
  detecta via `verify_unit_base` e fica vanilla nesse boot (sem crash) —
  mas o buff não aplica até re-REV.
- **D12 opcional**: sem o pack no device, o hook usa o original (sem ciclo
  32f→26f); ícones idem sem os PNGs.
- **Ultra Form não é forma nova de verdade**: tier de stats na True Form
  (jogo só tem 3 formas pra essa unit); backswing/Ultra real e talents
  oficiais fora de escopo (documentado no README do mod).
