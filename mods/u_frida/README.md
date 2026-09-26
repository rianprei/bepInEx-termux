# u_frida — scripts Frida `.js` como mod (F11, runtime-only)

Quem instala (Manager/deploy via su + chcon) coloca na pasta do jogo
(`/data/local/tmp/mods/<pkg>/`): `meu_mod.js`, `frida-gadget.bin` e
`frida-gadget.config` (modo `script-directory` apontando pra pasta).
Este `u_frida.so` SÓ verifica os três e dá `dlopen` no binário (depois de
esperar o il2cpp se houver — `libil2cpp.so` à vista em 10s, como o
sa2ammo). Sem PC, sem frida-server, sem patch de APK.

- Tudo mora na pasta de mods de propósito: ela é `bepinex_mod_file` com
  `map`+`execute` no `sepolicy.rule`. `dlopen` a partir de
  `/data/data/<pkg>/files` (`app_data_file`) é NEGADO em Enforcing (AOSP
  `private/app.te` não dá `execute` em `app_data_file` pro appdomain) —
  passava só porque o device de teste está Permissive. O jogo nunca
  escreve na pasta de mods; o que script precisa escrever vai pra
  `files/bepinex/` (o smoke test já faz isso).
- Binário SEM extensão `.so` de propósito: se terminasse em `.so`, o loader
  daria `dlopen` sozinho, sem config — e o gadget travaria o jogo no modo
  padrão (`listen`/`wait`).
- Scripts com `import` (ex: `frida-il2cpp-bridge`, `Java` bridge) precisam vir
  **já bundlados** (frida-compile/esbuild) — desde o Frida 17 as bridges não
  vêm embutidas no runtime. Script só com API built-in
  (`Interceptor`/`Module`/`Memory`) roda direto, sem build.
- `console.log` do script vai pra stdout do jogo (`/dev/null` no Android):
  não aparece no logcat. Script de teste tem que ter efeito observável
  (escrever arquivo, mudar comportamento), não só logar.

Instalação manual: `tools/deploy_frida.sh <pkg> <mod.js> [...]` (empurra
`.js` + `.bin`, escreve o `.config`, tudo com su+chcon).

Log: logcat `u_frida` + `/data/data/<pkg>/files/bepinex/log.txt`.
