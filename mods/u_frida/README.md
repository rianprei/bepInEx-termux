# u_frida — scripts Frida `.js` como mod (F11, runtime-only)

Na pasta do jogo (`/data/local/tmp/mods/<pkg>/`) ficam `meu_mod.js`,
`frida-gadget.bin` e `frida-gadget.config` (modo `script-directory`
apontando pra pasta). Este `u_frida.so` SÓ verifica os três e dá `dlopen` no
binário (depois de esperar o il2cpp se houver — `libil2cpp.so` à vista em
10s, como o sa2ammo). Sem frida-server, sem patch de APK.

**Quem instala hoje: só `tools/deploy_frida.sh`** (PC + adb + su). O Manager
(F5) ainda não tem código frida — instalar pelo celular, sem PC, é plano.

- **Só modo script.** O config inteiro (até 4KB) tem que ser JSON válido com
  `interaction.type` = `script` ou `script-directory`
  (`uf_config_is_script_mode()` em `jni/u_frida_config.h`). Vazio, `{}`,
  `listen`, `connect`, JSON inválido, chave duplicada ou >4KB = gadget NÃO
  carregado, com o `type` achado no log. Motivo: o default do gadget é
  `listen` com `on_load: wait` — o jogo congelaria esperando cliente e
  abriria socket (127.0.0.1, porta 27042).
- Tudo mora na pasta de mods de propósito: ela é `bepinex_mod_file`, e o
  `module/sepolicy.rule` tem
  `allow appdomain bepinex_mod_file file { getattr open read map execute }`.
  **[NAO VERIFICADO EM ENFORCING]** O teste de device foi em Permissive.
  Teoria: `dlopen` a partir de `/data/data/<pkg>/files` (`app_data_file`)
  seria negado em Enforcing, porque AOSP `private/app.te` só dá
  `create_file_perms` (sem `execute`) em `app_data_file`. O jogo nunca
  escreve na pasta de mods; o que script precisa escrever vai pra
  `files/bepinex/`.
- **[NAO VERIFICADO EM ENFORCING]** `execmod`: a regra não dá `execmod` em
  `bepinex_mod_file`. Se o gum fizer `mprotect(+PROT_EXEC)` em página do
  próprio binário, Enforcing nega. Não se adiciona permissão por teoria: a
  rodada de device Enforcing (ROADMAP F11) coleta o `avc: denied` do u_frida
  e só então a permissão negada entra na regra.
- Binário SEM extensão `.so` de propósito: se terminasse em `.so`, o loader
  daria `dlopen` sozinho, sem config — e o gadget travaria o jogo no modo
  padrão (`listen`/`wait`).
- **G1 — o que é isolado e o que não é.** Falha de CARGA é isolada: sem
  `.js`/`.bin`/config válido ou `dlopen` falhando, o u_frida loga e o jogo
  segue. SCRIPT que crasha NÃO é isolado: o `.js` roda dentro do processo do
  jogo, e crash nativo ali derruba o jogo (mesmo limite do loader —
  `jni/bc_loader.h`: crash no init de mod não é contido). A única rede é o
  crashguard F1d: 2 mortes em 20s depois de carregar mods ⇒ na abertura
  seguinte o loader não carrega mod nenhum daquele jogo até reativar.
- Scripts com `import` (ex: `frida-il2cpp-bridge`, `Java` bridge) precisam vir
  **já bundlados** (frida-compile/esbuild) — desde o Frida 17 as bridges não
  vêm embutidas no runtime. Script só com API built-in
  (`Interceptor`/`Module`/`Memory`) roda direto, sem build.
- `console.log` do script vai pra stdout do jogo (`/dev/null` no Android):
  não aparece no logcat. Script de teste tem que ter efeito observável
  (escrever arquivo, mudar comportamento), não só logar.

Instalação: `sh tools/fetch_frida_gadget.sh` (baixa o gadget pinado por
sha256) e `tools/deploy_frida.sh <pkg> <mod.js> [...]` (valida o pacote,
empurra `.js` + `.bin`, escreve o `.config`; um arquivo por vez com
su+chmod+chcon conferidos — qualquer falha aborta mostrando o erro).

Log: logcat `u_frida` + `/data/data/<pkg>/files/bepinex/log.txt` (C1, via
`mods/common/mod_common.h`).
