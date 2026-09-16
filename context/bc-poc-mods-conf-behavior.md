# bc-poc — Comportamento de `bc_mods.conf` e `toggle_mod`

Documentação do design de config runtime dos hooks (freebuff), pra que
"efeito só no próximo boot" não seja confundido com race real depois.

## Mecânica

- Arquivo: `BC_MODS_CONF_PATH` (definido em `bc_mods_conf.h`, companion e
  módulo compartilham o mesmo header).
- **Companion** (root, fora do sandbox): `list_mods` / `toggle_mod <nome>`
  lê o arquivo atual, inverte o estado do hook, persiste com
  `mkstemp` + `rename` (atômico no mesmo filesystem, `companion.cpp:304-333`).
- **App process** (jogo, sandbox): lê o config **uma única vez** no
  `postAppSpecialize` (`main.cpp:689` → `load_mods_config()`), antes de
  subir a event_thread que instala os hooks.

## Sem race (verificado, análise estática)

- `toggle_mod` × `toggle_mod`: sem race — `termux_accept_loop` é
  single-threaded (`accept4` → `handle_termux_request` → resposta → volta),
  serializa comandos concorrentes.
- `load_mods_conf` × `save_mods_conf` dentro do companion: sem race — mesma
  thread.

## Race aparente, mas é design load-once (NÃO é bug)

- Se o usuário roda `toggle_mod` enquanto o jogo ainda está bootando, o app
  process pode ler o config no instante entre `open()` e `rename()` e ver o
  estado antigo. Mesmo sem essa janela, o config é lido **1x por boot** —
  o efeito do toggle só aparece no **próximo boot do jogo**, nunca em
  runtime.
- Isso é comportamento esperado do design "config load-once no boot"
  (decisão de gate dos hooks é tomada antes da install_all). Não é uma
  race de escrita corrompendo o arquivo (rename é atômico); é leitura
  pontual com escrita sob demanda. **Não corrigir como bug.**

## Referências de código

- `jni/main.cpp:689` — `load_mods_config()` no `postAppSpecialize` (única
  leitura do config no processo do jogo).
- `jni/companion.cpp:304-333` — `save_mods_conf()` com `mkstemp`+`rename`
  atômico.
- `jni/companion.cpp:363-420` — `handle_toggle_mod()` (inverte, remove
  linha quando volta pro default ON).
- `jni/bc_mods_conf.h` — parse (`bc_mods_parse`) + constantes.