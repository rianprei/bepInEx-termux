# u_frida — scripts Frida `.js` como mod (F11, runtime-only)

O usuário solta `meu_mod.js` na pasta do jogo (`/data/local/tmp/mods/<pkg>/`,
o Manager copia). Este `u_frida.so` escreve `frida-gadget.config` (modo
`script-directory` apontando pra pasta) e dá `dlopen` no `frida-gadget.bin`.
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
