# bepInEx-termux

[![licença-MIT](https://img.shields.io/badge/licen%C3%A7a-MIT-green)](LICENSE)

Carregador de mods pra jogos Android que funciona **sem mexer no jogo**: você
instala um módulo no Magisk (uma vez), joga os mods numa pasta e eles são
carregados na memória quando o jogo abre. Nada de desinstalar/reinstalar APK,
nada de quebrar assinatura, nada de perder login do Google Play — o arquivo
do jogo fica intocado do começo ao fim (regra dura do projeto).

Como o mod entra **em tempo de execução** (runtime), ele sobrevive a updates
do jogo sem reinstalar nada. No caminho genérico (Unity IL2CPP — Swamp
Attack 2, TABS e amigos), os mods resolvem classe/método/campo por **nome**
usando a API que o próprio jogo expõe. No caminho Battle Cats (histórico),
os hooks acham a função por assinatura de bytes (AOB), com RVA e build-id
conferidos como primeira tentativa — o efeito é o mesmo: update muda
endereço interno, o mod acha de novo; o que muda o CORPO da função é que
quebra, em qualquer um dos dois caminhos.

Hoje o projeto é validado em: **Swamp Attack 2** (Unity IL2CPP, mods em
produção: munição infinita e conteúdo extra) e **Battle Cats** (caminho
próprio, histórico, intacto). TABS Pocket Edition é o próximo alvo. O app
**bepInEx Manager** (instalar/ligar/desligar mods com um toque, sem terminal)
está **em desenvolvimento** — enquanto ele não chega, o botão **Ação** do
Magisk mostra o diagnóstico (mods instalados, log, SELinux) e os scripts
`tools/` fazem a instalação.

## Requisitos

- Celular **rooteado** com **Magisk** (Zygisk ligado) ou **KernelSU +
  ZygiskNext**. Sem root não existe caminho (veja o FAQ).
- **Android 8+** e processador **arm64** (praticamente todo celular de 2018
  pra cá). arm32 não é suportado.
- O jogo tem que ser de um engine suportado e você precisa de um **mod
  compatível** (ver "Que mods rodam").

## Instalar em 3 passos

1. **Instale o zip no Magisk.** Gere o zip com `tools/build_module.sh` (ou
   pegue um pronto quando houver release) e abra ele pelo app do Magisk:
   *Módulos → Instalar do armazenamento*.
2. **Reinicie o celular.** Módulo Zygisk só carrega depois de um reboot de
   verdade.
3. **Toque no botão "Ação"** do módulo (app Magisk/KernelSU). Ele faz
   exatamente uma dessas duas coisas: abre o **bepInEx Manager**, se ele
   estiver instalado; senão, mostra o diagnóstico na tela — versão, estado
   do SELinux, os jogos com mods, o que está ligado/desligado e as últimas
   linhas do log de cada jogo. O botão não instala nem liga mod.

Dá pra conferir a instalação pelo app do Magisk mesmo: módulo
**bepInEx-termux** ativo, Zygisk ligado na tela inicial.

## Instalar um mod

> **Pelo Manager (em desenvolvimento, experimental):** quando chegar,
> você vai abrir o arquivo `.bmod` (baixado, recebido no WhatsApp, de onde
> for) e ele mostra o que o mod faz, instala com um toque e oferece
> liga/desliga e opções. Nada disso funciona hoje. O que segue é o caminho
> que **já funciona**, sem Manager.

Hoje, instalar um mod = colocar os arquivos dele na pasta do jogo e
reiniciar o jogo. O jeito mais curto é um script que faz tudo (build,
cópia, permissão, rótulo SELinux e reinício do jogo):

```bash
# no PC, com o celular conectado (adb) e o jogo instalado:
tools/deploy_mod.sh <id-do-mod> <pacote-do-jogo>
# exemplo real (Swamp Attack 2):
tools/deploy_mod.sh sa2ammo com.hyperdotstudios.swampattack2
```

- **`<pacote-do-jogo>`** é o identificador do jogo, tipo
  `com.hyperdotstudios.swampattack2`. Ele aparece no link da Play Store
  (`...?id=<pacote>`) e em apps que mostram detalhes de um app instalado.
- O mod vai pra `/data/local/tmp/mods/<pacote>/` e o log dele aparece em
  `/data/data/<pacote>/files/bepinex/log.txt` (o botão **Ação** já mostra as
  últimas linhas — nem precisa de terminal).
- **Ligar/desligar na mão:** renomeie o arquivo — `abc.so` (ligado) vira
  `abc.so.off` (desligado) — e reinicie o jogo.
- **Remover:** apague os arquivos do mod da pasta e reinicie o jogo. Nada
  fica dentro do jogo.

### Se o seletor de arquivos do celular falhar

Alguns celulares têm problemas com o seletor de arquivos padrão do Android.
Se isso acontecer ao tentar instalar um mod pelo Manager:

1. Abra o **gerenciador de arquivos** do celular.
2. Mova o arquivo do mod para a pasta **Download**.
3. No Manager, toque em **Escolher da pasta Download**.
4. Selecione o arquivo.

Pra criar um mod seu do zero (C++), veja [docs/SDK.md](docs/SDK.md). O
formato dos arquivos (`.bmod`, `.patch`, `.conf`) está em
[docs/BMOD-FORMAT.md](docs/BMOD-FORMAT.md).

Para gerar uma release local completa (zip Magisk, APK do Manager, exemplos
nativos, `SHA256SUMS` e `BUILD-INFO.txt`), use `tools/build_release.sh`.
A árvore Git precisa estar limpa. Sem
`MANAGER_KEYSTORE=/caminho/fora/do/repo`, o APK é produzido como
`UNSIGNED-DEBUG`; uma release assinada exige uma chave fixa fornecida
externamente. A reprodução pode ser conferida com
`VERIFY_RELEASE=1 tools/verify_all.sh`.

## Criar mod sem código (Mod Maker) — em desenvolvimento (experimental)

A meta (fase F6 do roadmap): no Manager, você toca em **"Escanear jogo"**,
o jogo roda uma vez e devolve a lista de classes/métodos/campos (`dump.tsv`);
você busca (ex.: `HasAmmo`), escolhe uma ação ("sempre verdadeiro", "sempre
retornar N", "multiplicar por N", "fixar campo em N") e salva — sem escrever
uma linha. O mod salvo é um `.bmod` que dá pra compartilhar.

Hoje isso já funciona por partes, sem Manager:

- **Descobrir nomes:** o scanner `u_dump` (já mergeado) gera o `dump.tsv`
  com todas as classes, métodos e campos do jogo (Unity IL2CPP), pela API em
  runtime — sem depender de ferramenta de dump externa. Caminho de hoje (com
  terminal, no PC com adb): `tools/deploy_mod.sh u_dump <pacote>` — builda,
  instala e reinicia o jogo; o dump sai em
  `/data/data/<pacote>/files/bepinex/dump.tsv` na primeira vez que o jogo
  abrir. Só gera se o arquivo não existir (refazer = apagar + reiniciar o
  jogo); remova o `/data/local/tmp/mods/<pacote>/u_dump.so` quando terminar
  de usar.
  No Manager isso vira o botão "Escanear jogo".
- **Aplicar regras:** um `.patch` com regras declarativas simples (verbos
  `return`/`mul`/`static`/`field`), servido pelo `u_patch` — **em
  integração** (fase F4, experimental): o contrato das regras já está fixado
  no roadmap e a lição que criou o verbo `field` veio de teste real no
  device, mas nada de `.patch` é instalável até o F4 mergear. De propósito não
  há instrução operacional aqui — quando mergear, esta seção e o
  [docs/BMOD-FORMAT.md](docs/BMOD-FORMAT.md) ganham o passo a passo.

## Que mods rodam

O Manager (em desenvolvimento) vai identificar o arquivo **pelo conteúdo**,
não pela extensão, e dizer em português se roda. A tabela honesta:

| Arquivo | Roda? | Observação |
|---|---|---|
| `.bmod` (nosso pacote) | **sim** | zip com manifest + mod |
| `.so` Android arm64 (mod nativo) | **sim** | copia pra pasta do jogo |
| `.patch` (regras declarativas) | **em integração** (experimental) | precisa do `u_patch` (F4, ainda não mergeado) |
| `.so` de outra arquitetura (arm32, x86) | não | "feito pra outra arquitetura" |
| `.js` script Frida | em desenvolvimento (F11) (experimental) | via frida-gadget |
| `.dll` de BepInEx/MelonLoader **IL2CPP** | depois (F12) | exige runtime .NET no processo |
| `.dll` de BepInEx/MelonLoader **Mono** em jogo Android **Mono** | depois (F13) | Harmony roda nativo em Mono |
| `.dll` Mono de PC em jogo **IL2CPP** (ex.: mods de TABS PC) | **não automático** | os dois binários falam línguas diferentes; use o Mod Maker/SDK pra recriar |
| `.exe`, `.dylib` iOS, `.CT` Cheat Engine | não | outra plataforma/binário |
| `.lua` GameGuardian | depois (F10) | fora do caminho crítico |

### Tipos de arquivo que NÃO funcionam (e por quê)

- **`.dll` de BepInEx (mod de PC):** é um mod da versão de PC do jogo,
  embalado para o BepInEx do computador (a pasta `BepInEx` com plugins está
  aí dentro). O `.dll` dele é feito para o jogo rodando no computador e
  não funciona no celular. Em breve vai dar para converter mods simples
  para o formato que o celular roda (`.patch`); por enquanto, procure a
  versão para Android deste mod.

- **`.apk`:** é o instalador do jogo, não um mod. O Manager nunca modifica
  arquivo do jogo: nem pacote do aplicativo, nem expansão. Para instalar
  mod, escolha o arquivo de mod em si (`.so`, `.patch` ou `.js`).

- **`.xapk`:** é um pacote de expansão do jogo, não um mod. O Manager nunca
  modifica arquivo do jogo.

- **`.zip` comum:** é um arquivo compactado sem pacote de mod dentro. O
  Manager só instala pacote de mod (`.bmod`) ou arquivo de mod avulso.
  Descompacte no gerenciador de arquivos e volte aqui com o arquivo de
  dentro.

- **`.so` de outra arquitetura (arm32, x86):** é um mod de verdade, mas
  feito para outro tipo de aparelho. O seu celular só roda a versão para
  ARM de 64 bits; procure o download para ARM 64 deste mod.

- **`.so` estragado:** o arquivo é um mod para Android, mas está estragado
  (download cortado ou corrompido). Instalar mod pela metade fecha o jogo.
  Baixe o mod de novo e tente outra vez.

- **Frida Gadget:** é o programa do Frida (a ferramenta que roda os scripts),
  não um mod. Se ele entrar como mod, o jogo abre e fica travado esperando
  um computador conectar. O Manager sabe cuidar dele: toque em instalar e
  ele vai para o lugar certo, junto com a configuração que faz os scripts
  `.js` da pasta rodarem sozinhos.

### Suporte por engine

O tipo de engine é detectado pelas libs do jogo. O que cada engine suporta:

- **Unity IL2CPP:** você pode escanear o jogo para criar mods de valores
  sem programar e instalar mods prontos feitos para este jogo. O uso de
  scripts JavaScript pelo carregador no celular é experimental; mods de
  outro jogo não têm compatibilidade garantida.

- **Unity Mono:** você pode instalar mods prontos feitos para este jogo e
  usar scripts JavaScript pelo carregador (experimental no celular). Ainda
  não é possível criar mods sem programar para Unity Mono.

- **Outras engines (Cocos2d-x, Unreal, Godot, etc.):** você pode instalar
  mods prontos feitos para este jogo e usar scripts JavaScript pelo
  carregador (experimental no celular). Ainda não é possível criar mods
  sem programar para estas engines.

Nenhum caminho modifica arquivo do jogo.

## FAQ

### Precisa de root?
Precisa. Root (Magisk/KernelSU) é o que permite carregar código dentro do
processo do jogo **sem** modificar o APK. Apps "sem root" que prometem mod
de jogo fazem isso reempacotando o APK — quebra a assinatura, o login Google
e os updates. Não é o que fazemos, nem vamos fazer.

### O Play Protect vai reclamar?
Ele pode reclamar de **APKs instalados fora da Play Store** — quando o
Manager for distribuído, é o caso dele (é sideload). O módulo Magisk em si
não é um APK e não passa pelo Play Protect. Nada aqui é instalado "por
dentro" de outro app.

### Funciona em jogo online?
O projeto é pra **jogos offline/single-player**. Jogo online com
sincronização de servidor pode detectar mudanças, reverter o mod ou banir a
conta — nem tudo que roda localmente o servidor aceita. Nada pra burlar
anti-cheat, compra, IAP ou verificação de pagamento. Se o jogo tem
multiplayer online (ex.: TABS tem), use mods só no que é local (campanha,
sandbox) e por sua conta.

### E mod de PC (`.dll` do BepInEx)?
Depende do jogo Android ser Mono ou IL2CPP e do mod ser pra qual dos dois —
tabela acima. O caso comum ("jogo Android é IL2CPP, meu mod de PC é Mono")
**não tem conversão automática**: o mod de PC referencia tipos que não
existem no binário IL2CPP. O caminho é recriar o mod aqui (Mod Maker ou SDK).

### Meu celular usa SELinux Enforcing — funciona?
É o padrão em celular de fábrica, e o módulo já carrega as regras
(`module/sepolicy.rule`, tipo próprio `bepinex_mod_file`) que liberam só o
mínimo: o zygote achar a pasta e o jogo ler/executar os mods.

O que foi **medido de verdade** (não promessa): num **POCO C75 (HyperOS)**,
com o módulo **v0.4.0** e `setenforce 1`, o **Swamp Attack 2** (mods
nativos `sa2ammo` + `sa2content` via Dobby, e o `u_dump`) e o **Battle
Cats** (4/4 mods, hooks ativos) funcionaram, sem nenhum `avc: denied` do
nosso caminho. Isso é o teste que existe — não "qualquer celular": política
de fábrica varia por fabricante, e se algo não carregar o botão **Ação**
mostra o estado do SELinux e o rótulo da pasta pra diagnosticar.

Instalar mod depois do boot exige aplicar o rótulo no arquivo — o
`deploy_mod.sh` e o Manager (em desenvolvimento) fazem isso sozinhos; se
você copia na mão, aplique
`chcon u:object_r:bepinex_mod_file:s0 <arquivo>` via root.

### Como desinstalo sem perder meus mods?
Remover o módulo pelo app Magisk **não apaga nada seu**: os mods
(`/data/local/tmp/mods/`, `/data/local/tmp/bc_mods/`) e os logs
(`/data/data/<pacote>/files/bepinex/`) continuam no lugar — o desinstalador
só lista o que ficou. Reinstalar o módulo depois volta tudo a funcionar. Pra
apagar de vez: `rm -rf /data/local/tmp/mods` (e as pastas listadas na
mensagem de desinstalação).

### Instalei o mod e o jogo não carrega nada
Primeiro o botão **Ação**: ele mostra SELinux, os mods ativos e as últimas
linhas do log. Os erros comuns estão lá com nome ("dlopen falhou",
"classe X não encontrada", "il2cpp não subiu"). Se o jogo fechar sozinho
logo depois de abrir, tire o mod da pasta e reinicie o jogo pra isolar —
regra ruim não derruba nada por design, mas um mod com bug de memória pode
derrubar (é código nativo rodando no processo).

## Escopo ético

- **Jogos offline/single-player.** Sem preset, recurso ou ferramenta pra
  burlar compra (IAP), licença ou verificação de pagamento.
- **Sem PvP online com anti-cheat.** Nada de vantagem injusta contra outras
  pessoas.
- **Nunca modificar APK, OBB ou arquivos do jogo** — tudo em runtime. Sem
  exceção.
- Proibido redistribuir binários de jogos (nada de `.so`/`.apk` de jogo
  dentro deste repo).

---

# Para desenvolvedores

Conteúdo técnico, histórico e decisões de arquitetura. Pra usar o projeto
como pessoa comum, as seções acima já bastam.

- **Novo (roadmap universal):** o projeto deixou de ser só o POC do Battle
  Cats e virou um loader genérico — `docs/ROADMAP-UNIVERSAL.md` tem o mapa
  completo (objetivo, contratos C1–C7, fases F1–F13 e o que já mergeou).
- **Módulo completo com uma linha:** `tools/build_module.sh` gera
  `out/bepinex-termux-<versão>.zip` com `module.prop`, `zygisk/arm64-v8a.so`,
  `sepolicy.rule`, scripts de boot e (quando disponível) o APK do Manager —
  a seção "Empacotamento" mais abaixo é o caminho manual histórico.
- **SDK de mod nativo:** [docs/SDK.md](docs/SDK.md) — template, `new_mod.sh`,
  `deploy_mod.sh`, `pack_bmod.sh` e a API do `mod_common.h`.
- **Formato dos arquivos de mod:** [docs/BMOD-FORMAT.md](docs/BMOD-FORMAT.md)
  (`.bmod`, `.conf`, `.patch`, `dump.tsv`).
- **Testes:** `test/selftest_harness.cpp` (host, sem device) +
  `test/mod_common_test.cpp` (SDK).

O texto abaixo é a documentação técnica original do projeto.

## Visão original (POC Battle Cats)


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
recompile porque nome/assinatura não mudam. bepInEx-termux (nativo, sem
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
update grande do jogo. bepInEx-termux é um módulo **Zygisk** (Magisk) que
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
analogia melhor no PC. O canal de controle do bepInEx-termux
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
`.so` colocado em `/data/local/tmp/bc_mods/` (ordem alfabética do nome,
`strcmp`: use prefixo com zero à esquerda, `01_`, `02_`..., senão `10_`
vem antes de `2_`; `requires` no manifest ainda reordena) é descoberto, `dlopen()`+`dlsym("bc_mod_register")` no
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
O mod de referência incluído (`mods/mechabun`) existe como prova de
conceito dessa infra de ponta a ponta num jogo real, não como produto
final — ver o README dele.

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

Gap estrutural real entre bepInEx-termux e BepInEx PC: BepInEx/Harmony hooka
por **metadata .NET** (`Type.GetMethod(nome, assinatura)`), sobrevive a
recompiles porque nome/assinatura não mudam mesmo com o binário realocado.
bepInEx-termux (Dobby) hooka por **endereço fixo** (RVA em `offsetsdb.h`),
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

### Assinar o Manager (chave de release)

O `manager/build.sh` assina o APK. Por padrão ele usa o keystore de DEBUG
(`manager/.debug.keystore`, senha pública `android`) — é um arquivo de
build, entra no `.gitignore` e não serve para distributing nada.

Para assinar com a sua chave de release:

```bash
# A chave mora AQUI, fora do repo (o build não versiona nem copia ela):
#   ~/.config/bepinex-termux/manager-release.jks   (alias "manager")
# 1) senha no ambiente, build não interativo (não aparece no `ps`):
export MANAGER_KS_PASS='sua senha'
export MANAGER_KEY_PASS="$MANAGER_KS_PASS"   # se a chave privada tiver outra
bash manager/build.sh
# 2) sem MANAGER_KS_PASS: o apksigner PERGUNTA a senha no terminal
bash manager/build.sh
```

Variáveis: `MANAGER_KEYSTORE` (caminho da chave), `MANAGER_KEY_ALIAS`
(default `manager`), `MANAGER_KS_PASS` (senha do keystore) e
`MANAGER_KEY_PASS` (senha da chave privada, quando for diferente).
`tools/build_release.sh` usa a chave de release automaticamente se ela
existir no caminho acima, e grava o fingerprint SHA-256 do certificado em
`BUILD-INFO.txt` (extraído do APK assinado com `apksigner verify
--print-certs` — o certificado é público; a chave nunca é aberta).

**Faça backup da chave. Perder a chave obriga DESINSTALAR o Manager antes de
instalar a versão nova**: Android recusa atualização quando a assinatura não
bate com a do APK instalado (erro de `INSTALL_FAILED_UPDATE_INCOMPATIBLE`),
e a única saída é remover o app (`adb uninstall` ou o botão do sistema) e
instalar de novo — com o Manager, sem root, você perde as regras e configs
salvos. A chave de debug não tem esse problema (é pública e reconstruível),
mas também não é assinatura de release.

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
gameplay real, zero crash/ANR. Bateria de 60 casos de teste (311
assertions, contagem real reverificada — compilar e rodar
`test/selftest_harness.cpp` confirma) do hook lifecycle, do loader de mods dinâmico e do AOB
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
  (Depois disso o protocolo de sinal foi redesenhado — v0.4.1: o
  companion, que é root, **escreve** properties como **contadores de
  sequência**; o processo do jogo **só lê** e age quando o valor muda —
  e nunca escreve property, que em SELinux Enforcing é negado pro app.
  Ver `jni/bc_signal.h`.)

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
  Ver [NOTICE.md](docs/NOTICE.md).
- [Zygisk](https://github.com/topjohnwu/Magisk) (topjohnwu/Magisk) —
  mecanismo de injeção em todo processo Zygote-forked.

## Licença

MIT — ver [LICENSE](LICENSE). O binário redistribuído (`libdobby.a`) mantém
a licença MIT original do projeto Dobby — ver [NOTICE.md](docs/NOTICE.md).
