# bepin-termux

Injeção/instrumentação nativa em runtime pra apps Android via **Zygisk**
(Magisk) + **Dobby** (inline hook ARM64) + ponte de controle pelo **Termux**
— o mesmo papel que o BepInEx cumpre no ecossistema Unity/Mono, mas pra
código nativo C++ (motores como Cocos2d-x, que não expõem um chainloader
gerenciado).

[![licença-MIT](https://img.shields.io/badge/licen%C3%A7a-MIT-green)](LICENSE)

Prova de conceito validada ao vivo contra o Battle Cats
(`jp.co.ponos.battlecatsen`), log-only — não altera o jogo. A arquitetura é
genérica: qualquer app com libs nativas dá pra hookar trocando os símbolos
alvo.

## Onde é estruturalmente mais resiliente que BepInEx PC (e onde não é)

Comparação honesta, não "melhor em tudo" — BepInEx tem anos de maturidade,
ecossistema de plugins, suporte IL2CPP, e nenhuma dessas vantagens de
ecossistema é replicável aqui. Duas vantagens estruturais **reais e
verificadas** (não memória, não hipótese):

**1. Sobrevive a update de VERSÃO do jogo (hooking por assinatura, não RVA).**
BepInEx/Harmony hooka por metadata .NET (`Type.GetMethod`), que sobrevive
recompile porque nome/assinatura não mudam. bepin-termux (nativo, sem
runtime gerenciado) usa AOB pattern scan (`jni/bc_pattern_scan.h`) como
equivalente: procura os bytes da função, não o endereço. Prova real (não
simulada): dados já capturados do próprio projeto (`tools/battlecats-offsets.json`)
mostram um update real do Battle Cats (EN 15.5.0→15.6.0, build_id ELF
distinto) onde o RVA dos 4 hooks mudou ~40KB e o prólogo de bytes ficou
idêntico nos 4 — RVA fixo teria quebrado, assinatura sobreviveu sem
reempacotar nada. Limite honesto: sobrevive reposicionamento de código
(o que updates normalmente fazem), não mudança do CORPO da função — nisso
metadata .NET ainda tem vantagem que nenhuma técnica nativa replica sem
runtime gerenciado.

**2. Sobrevive a update do APK sem reinstalar nada (instalação fora do app).**
BepInEx no PC injeta via **Doorstop**: `winhttp.dll` (hijack de DLL) +
`doorstop_config.ini` sentados dentro da própria **pasta de instalação do
jogo** (confirmado em instalação real local: `winhttp.dll` e
`doorstop_config.ini` na raiz do diretório do jogo, apontando pra
`BepInEx\core\BepInEx.Preloader.dll`). Um update/verify-integrity da
distribuidora (Steam etc.) pode sobrescrever ou remover esses arquivos —
prática documentada na comunidade BepInEx: reinstalar/revalidar após
update grande do jogo. bepin-termux é um módulo **Zygisk** (Magisk) que
vive fora do storage do app inteiramente (`/data/adb/modules/`) — hooka o
processo em runtime, nunca escreve dentro do APK/pasta do app. Um update
de APK (Play Store) não apaga nem precisa tocar no módulo; só pode mudar
endereços internos, que é exatamente o problema que o item 1 já resolve.

**3. Controle de acesso real no canal de controle (kernel, não forjável).**
Verificado em instalação real: `BepInEx/plugins/` e `BepInEx/config/` (PC)
são graváveis (`755`) por qualquer processo rodando como o mesmo usuário
do SO — sem isolamento entre apps, sem autenticação, qualquer programa
(inclusive malware) pode substituir uma DLL de plugin ou editar config sem
pedir permissão nenhuma; é o modelo de permissão de desktop, não tem
analogia melhor no PC. O canal de controle do bepin-termux
(`companion.cpp`, socket abstract `@bc_companion`) usa `SO_PEERCRED` —
autenticação a nível de kernel, não forjável por processo userspace —
aceitando só UID 0 (root), UID 2000 (shell/adb) ou o UID real do Termux
(`is_authorized_uid()`). Um app Android arbitrário (sandbox de UID
diferente) não consegue nem abrir o socket, muito menos falsificar
identidade — isolamento que o modelo de permissão do Android garante e o
Windows/Steam não tem equivalente.

## Arquitetura

```
Zygote (fork) ──▶ processo do app ──▶ jni/main.cpp (módulo Zygisk)
                                          │
                                          │ dl_iterate_phdr até a lib nativa
                                          │ carregar, então hook via Dobby
                                          ▼
                                    símbolos JNI interceptados
                                          │
                     companion (root) ────┘
                     jni/companion.cpp
                          │
                          │ socket Unix abstract @bc_companion
                          │ (SO_PEERCRED, fail-closed)
                          ▼
                    Termux / adb forward
                    (termux_client.py / bc_log_viewer.py)
```

### 1. `jni/main.cpp` — módulo Zygisk (API v5)

Roda dentro do processo do app alvo. Espera a lib nativa carregar
(`dl_iterate_phdr` em polling), resolve os símbolos e instala hooks inline
via Dobby.

**Resolução de alvo em cascata** (sobrevive a updates do APK):
1. RVA fixo de uma offset DB pré-computada (mais rápido, exige build-id
   confirmado).
2. Símbolo JNI export via `DobbySymbolResolver`.
3. Scan por assinatura de prólogo (só aceita se a assinatura casar uma
   única vez — nunca um match ambíguo).

**Fail-safe**: qualquer etapa que falhar deixa aquele hook específico
DORMANT (log-only, sem crashar o processo do jogo). Ver

### 1.1. Loader de mods `.so` dinâmico (`jni/bc_loader.h` + `jni/bc_mod_api.h`)

O que fecha o gap de "mod hardcoded" → "mod-loader de verdade": qualquer
`.so` colocado em `/data/local/tmp/bc_mods/` (ordenados por prefixo
numérico no nome) é descoberto, `dlopen()`+`dlsym("bc_mod_register")` no
boot, **depois** dos hooks estáticos (o mod já pode usar `resolve_symbol`
contra a lib carregada). Cada mod exporta:

```c
extern "C" bool bc_mod_register(const bc_mod_api *api);
```

recebe `register_prefix`/`register_postfix` (mesmo dispatcher Prefix/Postfix
estilo Harmony já usado nos hooks estáticos), `resolve_symbol`, e `log`.
Isolamento de falha por arquivo: `dlopen` que falha ou `dlsym` sem o símbolo
esperado só pula aquele `.so` (log, não derruba o processo nem os outros
mods) — mesmo padrão DORMANT dos hooks estáticos, agora por mod.

Grafo de dependência (`jni/bc_mod_graph.h`): cada mod pode declarar
`requires`/`conflicts` por nome; resolve ordem topológica, rejeita ciclo e
conflito sem crashar (`BC_MOD_REJ_CYCLE`/`BC_MOD_REJ_CONFLICT`). O loader de
`.so` dinâmicos aplica esse grafo de verdade na ordem de carga (não só o
nome do arquivo) — um mod com `requires` nunca carrega antes do que ele
depende.

Config tipada (`jni/bc_mods_conf.h`): schema com tipo (`BC_MOD_BOOL/INT/ENUM`)
+ range/domínio, validado **antes** de aplicar (equivalente ao
`AcceptableValueRange`/`AcceptableValueList` do BepInEx `ConfigEntry<T>`),
mais callback por chave individual quando o valor muda (porta do
`ConfigFile.SettingChanged`, `ConfigFile.cs:596-610` do BepInEx).

Ver
`context/bc-poc-hardening-summary.md`.

### 1.2. AOB pattern scan (`jni/bc_pattern_scan.h`) — resiliência a update do jogo

Gap estrutural real entre bepin-termux e BepInEx PC: BepInEx/Harmony hooka
por **metadata .NET** (`Type.GetMethod(nome, assinatura)`), sobrevive a
recompiles porque nome/assinatura não mudam mesmo com o binário realocado.
bepin-termux (Dobby) hooka por **endereço fixo** (RVA em `offsetsdb.h`),
que quebra a cada update do jogo — não existe metadata gerenciada num
binário nativo ARM64.

`bc_pattern_scan_buffer`/`bc_pattern_scan_lib` são o equivalente nativo real
(mesma técnica de Frida/Cheat Engine/IDA FLIRT): varre o segmento executável
procurando os bytes que só uma função tem (com wildcard nos operandos que
mudam entre builds — `adrp`/`bl` relativos), em vez de assumir que ela está
sempre no mesmo offset. Um recompile que só realoca código (sem mudar o
corpo da função) continua achando o pattern; um recompile que muda o corpo
quebra igual RVA quebraria — não é milagre, é estritamente mais resiliente
que offset fixo, nunca menos.

Honesto sobre o limite: ambiguidade (0 ou 2+ matches) **nunca** escolhe "o
primeiro que achar" — retorna erro explícito (`BC_SCAN_NOT_FOUND`/
`BC_SCAN_AMBIGUOUS`), igual a `resolve_symbol` hoje. Testado com bytes reais
extraídos do binário do jogo (prólogo de `appUpdateDraw`, EN, via
`llvm-objdump`/`xxd`), não só dado sintético — ver `test/selftest_harness.cpp`
Caso 50. Exposto a mods dinâmicos via `bc_mod_api.resolve_pattern` (campo
novo no fim do struct — mod compilado contra a API anterior continua
funcionando sem mudança).

**Prova empírica (não simulada) de que resolve o problema de verdade**: o
projeto já tem 4 builds regionais reais do jogo extraídas (arquivos
`.so` distintos, hashes diferentes, não redistribuídos aqui por serem
binário de terceiro). O RVA de `appUpdateDraw` varia **~120KB** entre elas:

| Região | MD5 do `.so`                      | RVA de `appUpdateDraw` |
|--------|------------------------------------|-------------------------|
| EN     | `049a93097de3534ea9279369cd8009a5` | `0x31ec4c`               |
| TW     | `f33fe5be2beb5d883a26e5a8cd5b6637` | `0x30168c`               |
| KR     | `b4d15eb7abc764b3631e333f2fc18ae4` | `0x3013bc`               |
| JP     | `f954d90207ca99f68147b878e8ee4f75` | `0x3087bc`               |

Um hook por **RVA fixo** (`offsetsdb.h`) funcionaria em exatamente 1 dessas
4 builds reais e cairia em `DORMANT` nas outras 3 — é literalmente o
cenário que RVA fixo não sobrevive. Os 24 bytes do prólogo (`bc_pattern_scan`
Caso 50), extraídos via `dd`+`od` nos 4 offsets acima, são **byte-a-byte
idênticos** nas 4 builds: `ff 43 01 d1 fd 7b 02 a9 f6 57 03 a9 f4 4f 04 a9
fd 83 00 91 54 d0 3b d5`. Isso é a prova real (não hipótese) de que o
AOB scan acha a função certa mesmo com endereço mudando drasticamente
entre builds — o mesmo tipo de mudança que um update do jogo produz.

**Prova mais forte ainda — update de VERSÃO real, não só cross-região**:
`tools/battlecats-offsets.json` (banco de assinaturas já mantido pelo
projeto, gerado por `tools/bc_offset_check.py`) tem 2 capturas da região
EN em versões diferentes do jogo, **build_id ELF distinto** (update real
publicado pela ponos, não simulação):

| Versão | `build_id` (ELF note)         | RVA `appInit` | RVA `appUpdateDraw` | RVA `appTouch` | RVA `appKey` |
|--------|-------------------------------|----------------|----------------------|-----------------|---------------|
| 15.5.0 | `ceae8883fe17...`              | `0x31EB7C`     | `0x31EC4C`           | `0x31EFB8`      | `0x31F078`    |
| 15.6.0 | `338b0601243a...`              | `0x32788C`     | `0x32795C`           | `0x327CC8`      | `0x327D88`    |

RVA dos 4 hooks mudou **~40KB (0x9D10)** entre as duas versões — um hook
por RVA fixo quebraria nos 4 ao atualizar de 15.5.0 pra 15.6.0 (exigiria
regenerar e reempacotar `offsetsdb.h`, exatamente o custo de manutenção
que este recurso existe pra eliminar). Comparação byte-a-byte das
assinaturas `bytes_prologue` das duas versões (script no commit): **os 4
prólogos são idênticos entre 15.5.0 e 15.6.0**, apesar do RVA ter mudado
nos 4. Essa é a prova concreta — um update de jogo já aconteceu de
verdade, capturado nos dados do projeto, e o pattern scan sobrevive; RVA
fixo não sobreviveria sem reempacotar o módulo.

### 2. `jni/companion.cpp` — processo companion (root)

Daemon root separado (`REGISTER_ZYGISK_COMPANION`), spawnado pelo
`zygiskd64` — sobrevive via double-fork. Expõe um socket Unix abstract
(`@bc_companion`) autenticado por **`SO_PEERCRED`** (kernel, não-forjável):
aceita root (UID 0), shell/adb (UID 2000) e o UID real do Termux, rejeita
qualquer outro processo — fail-closed por padrão. Ver
`COMPANION_TERMUX_ARCHITECTURE.md`.

### 3. Clientes de controle

- **`termux_client.py`** — cliente mínimo, roda dentro do Termux, conecta
  direto no socket abstract.
- **`bc_log_viewer.py`** — cliente para uso **fora** do device, via
  `adb forward tcp:PORT localabstract:bc_companion` (nenhuma porta TCP fica
  aberta no device). Trata timeout, forward morto e rejeição de UID com
  mensagens claras em vez de travar ou devolver traceback.

### 4. `termux-console/bepin-console` — console ao vivo no Termux

Equivalente ao console que o BepInEx abre no Windows — em vez do usuário
abrir manualmente, o companion dispara isso sozinho via
`launch_termux_console()` (companion.cpp), 1x por sessão do jogo, usando
`RUN_COMMAND` do Termux (`RunCommandService`, pacote `termux-app`).

**Requisito obrigatório**: `allow-external-apps=true` em
`~/.termux/termux.properties` (criar se não existir, `termux-reload-settings`
depois) — sem isso o `RunCommandService` recusa silenciosamente e o console
não abre (o companion segue funcionando normal, só não dispara o Termux).

`bepin-console` roda o stream de log em background + lê comando digitado no
foreground (REPL) — o BepInEx no PC não tem input nenhum no console
(confirmado no código-fonte: `ConsoleManager.cs`/`WindowsConsoleDriver.cs`
não têm nenhum `Read`/`ReadLine`), então isso é além da paridade, não invenção.

Log persistido em disco em `/data/local/tmp/bc_poc_LogOutput.log`
(equivalente ao `LogOutput.log` do BepInEx — trunca a cada boot por
padrão, mesmo comportamento confirmado em `DiskLogListener.cs`). Log
nativo do próprio jogo (não só do módulo) é unificado no mesmo arquivo via
bridge de `logcat` — ver `ROADMAP.md` Fase 3.

### Formato das linhas de log

`[HH:MM:SS] [Nível   :    Fonte] corpo` — nível alinhado à esquerda em 7,
fonte à direita em 10, mesmo layout do `LogEventArgs.ToString()` do BepInEx
(`[Level,-7:Source,10]`). Duas diferenças deliberadas: (1) timestamp na
frente (BepInEx não mostra hora em nenhum sink real); (2) cada mod dinâmico
loga com seu próprio nome como fonte (equivalente ao `ManualLogSource` por
plugin do BepInEx), extraído da convenção `"[nome] mensagem"`. Níveis
`Fatal|Error|Warning|Message|Info` por padrão, sem `Debug` — mesmos defaults
de `[Logging.Console]`/`[Logging.Disk]` do BepInEx.

## Build

Requer o NDK (usei o bundle do próprio Magisk, mas qualquer NDK recente
com `clang`/`libc++` serve):

```bash
export ANDROID_NDK_HOME=/caminho/pro/ndk
"$ANDROID_NDK_HOME/ndk-build" NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=jni/Android.mk -B
```

Saída: `libs/arm64-v8a/libbc-poc.so`.

### Nota crítica de linkagem (não reverter)

`jni/Application.mk` **não** linka `-landroid`. Em builds recentes de
HyperOS/MIUI, adicionar essa lib puxa uma cadeia NEEDED transitiva quebrada
(`libandroid.so → libharfbuzz_ng.so → libicu.so`, `libicu.so` só existe na
APEX i18n, fora do namespace padrão do `zygiskd64`) — o `.so` do app carrega
normal (zygote tem acesso à APEX), mas o `dlopen()` **dentro do companion**
falha silenciosamente. `-Wl,--as-needed` garante que nenhuma NEEDED morta
volte a entrar. NEEDED final esperado: `liblog`, `libc`, `libdl`.

## Empacotamento (módulo Magisk)

```bash
# estrutura exigida pelo Zygisk:
#   module.prop
#   zygisk/arm64-v8a.so   ← nome fixo por ABI, não é o nome do seu .so
zip -r seu-modulo.zip module.prop zygisk/arm64-v8a.so
```

Instalar via Magisk (Módulos → Instalar do armazenamento) e **reiniciar** —
módulos Zygisk só materializam/carregam depois de um reboot real, mesmo
scripts simples de `post-fs-data.sh` não rodam sem reiniciar primeiro.

## Uso

```bash
# dentro do Termux, direto no socket abstract:
python3 termux_client.py ping

# fora do device (PC), via adb forward:
adb forward tcp:17654 localabstract:bc_companion
python3 bc_log_viewer.py --host 127.0.0.1 --port 17654 ping
python3 bc_log_viewer.py --host 127.0.0.1 --port 17654 status
python3 bc_log_viewer.py --host 127.0.0.1 --port 17654 list_patches
```

## Testado ao vivo

Device físico rooted (Magisk), Android 16/HyperOS. 4/4 hooks ativos em
gameplay real, zero crash/ANR. Bateria de 55 casos de teste (228
assertions) do hook lifecycle, do loader de mods dinâmico e do AOB
pattern scan (`test/selftest_harness.cpp`, 0
falhas na última execução) cobrindo patch/unpatch/repatch, idempotência,
race entre clientes concorrentes, stress test de 50 ciclos unpatch/repatch
no mesmo hook, grafo de dependência (ciclo/conflito) e os 4 caminhos reais
do loader `.so` dinâmico (ok/inativo/dlopen falha/símbolo ausente).
Módulo recompilado limpo via `ndk-build` após cada mudança.

Ponte de controle confirmada de ponta a ponta via `adb forward`:

```
ping         → pong
status       → companion_active
list_patches → appInit|on|active|1
               appUpdateDraw|on|active|1464
               appTouch|on|active|0
               appKey|on|active|0
```

2 bugs reais encontrados e corrigidos durante o desenvolvimento (não
hipotéticos — reproduzidos ao vivo antes do fix):

- **Semântica invertida de close no accept loop**: o handler de
  `list_patches` virou thread-per-client pra não bloquear o loop de accept;
  o retorno de "adotado" estava invertido, fechando o fd do cliente antes
  da thread terminar de escrever a resposta.
- **Race em property compartilhada entre clientes concorrentes**: 2
  chamadas simultâneas de `list_patches` competiam pela mesma Android
  property (só suporta 1 pedido em voo); resolvido com mutex serializando
  só o ciclo sinaliza→espera→lê, sem travar o resto do accept loop.

## Ver também

- `ROADMAP.md` — roadmap Termux-cêntrico (push_mod + console ao vivo/REPL,
  paridade com o log/console do BepInEx).
- `ROADMAP-COMPETITORS.md` — pesquisa de concorrentes reais (LSPatch, whale, VirtualXposed,
  Riru, shadowhook etc.) via GitHub API/web search, prioridades daí derivadas,
  benchmark de overhead medido ao vivo no device.
- `RESILIENCE_ANALYSIS.md` — análise de resiliência a updates do APK alvo.
- `INTEGRATION_CHECK.md` — verificação de integração build main+companion.
- `COMPANION_TERMUX_ARCHITECTURE.md` — arquitetura completa da ponte
  companion↔Termux, incluindo o modelo de autenticação por `SO_PEERCRED`.
- `context/` — notas técnicas de hardening, comportamento de config, e
  comparação de design contra o BepInEx (o que ele resolve que este projeto
  ainda não precisa, e vice-versa).

## Créditos

- [Dobby](https://github.com/jmpews/Dobby) (jmpews) — inline hook ARM64.
  Ver [NOTICE.md](NOTICE.md).
- [Zygisk](https://github.com/topjohnwu/Magisk) (topjohnwu/Magisk) —
  mecanismo de injeção em todo processo Zygote-forked.

## Licença

MIT — ver [LICENSE](LICENSE). O binário redistribuído (`libdobby.a`) mantém
a licença MIT original do projeto Dobby — ver [NOTICE.md](NOTICE.md).
