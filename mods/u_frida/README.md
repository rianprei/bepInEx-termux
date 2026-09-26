# u_frida — scripts Frida `.js` como mod (F11, runtime-only)

O usuário solta `meu_mod.js` na pasta do jogo (`/data/local/tmp/mods/<pkg>/`,
o Manager copia). Este `u_frida.so` copia `frida-gadget.bin` pra
`/data/data/<pkg>/files/bepinex/` (o jogo não escreve em `/data/local/tmp`,
só lê — binário + config moram onde ele escreve), escreve
`frida-gadget.config` ao lado (modo `script-directory` apontando pra pasta
de mods, de onde os `.js` são só lidos), espera o il2cpp se houver
(`libil2cpp.so` à vista em 10s, como o sa2ammo) e dá `dlopen` no gadget.
Sem PC, sem frida-server, sem patch de APK.

- Binário SEM extensão `.so` de propósito: se terminasse em `.so`, o loader
  daria `dlopen` sozinho, sem config — e o gadget travaria o jogo no modo
  padrão (`listen`/`wait`). Nome neutro também ajuda contra detecção
  ingênua por nome ("Frida" no nome da lib), que a doc oficial sugere evitar.
- Scripts com `import` (ex: `frida-il2cpp-bridge`, `Java` bridge) precisam vir
  **já bundlados** (frida-compile/esbuild) — desde o Frida 17 as bridges não
  vêm embutidas no runtime. Script só com API built-in
  (`Interceptor`/`Module`/`Memory`) roda direto, sem build.
- `console.log` do script vai pra stdout do jogo (`/dev/null` no Android):
  não aparece no logcat. Script de teste tem que ter efeito observável
  (escrever arquivo, mudar comportamento), não só logar.

Log: logcat `u_frida` + `/data/data/<pkg>/files/bepinex/log.txt`.
