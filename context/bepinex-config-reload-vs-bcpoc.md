---
name: bepinex-config-reload-vs-bcpoc
description: SettingChanged do BepInEx (callback por assignment, não file-watch) vs nosso polling de property no event_thread — mecanismo real com arquivo:linha e veredito polling vs push
metadata:
  type: project
  status: documented
---

# Config reload: BepInEx SettingChanged vs bc-poc polling

**Data:** 2026-09-14
**Fonte:** fork NextBep local (`~/battlecats-mods/nextbep/BepInEx.Android/`, lido agora com line numbers) + código real do bc-poc.

## 1. Como o BepInEx reage a mudança de config (mecanismo real)

**É callback por assignment, NÃO file-watch.** Cadeia verificada:

1. `ConfigEntry<T>.Value` setter (`ConfigEntryBase.cs:26–33`): aplica `ClampValue`, **só dispara se o valor mudou de fato** (`if (Equals(_typedValue, value)) return;`), depois chama `OnSettingChanged`.
2. `ConfigFile.OnSettingChanged` (`ConfigFile.cs:592–605`): auto-save (`Save()` se `SaveOnConfigSet`) → invoca o evento `SettingChanged` do arquivo.
3. Fan-out pra entry individual: o construtor de `ConfigEntry<T>` registra um handler que filtra `args.ChangedSetting == this` e re-emite o evento da entry (`ConfigEntryBase.cs:11–16`).
4. Payload: `SettingChangedEventArgs.ChangedSetting` → o `ConfigEntryBase` mudado (ler `.Value` tipado no handler). Classe: `SettingChangedEventArgs.cs`.

**O que isso significa na prática:** o evento dispara quando **alguém escreve no objeto `ConfigEntry` em memória** (plugin em runtime, ou `SetSerializedValue` durante o `Reload()` — que passa pelo mesmo setter, `ConfigEntryBase.cs:126–136`). O `Reload()` só roda 1× no construtor do `ConfigFile` (`ConfigFile.cs:36`) — **não há FileSystemWatcher em lugar nenhum do core** (grep: zero). Mudar o `.cfg` no disco **não notifica ninguém** até um `Reload()` manual; e um `Reload()` que re-aplica valores iguais aos atuais **não dispara evento nenhum** (guard do setter).

Ou seja: o "hot-reload" idiomático do BepInEx é **push no objeto**, não watch de arquivo. Plugins tipicamente assinam `entry.SettingChanged += (s,e) => aplicar()` no `Load()`.

## 2. Nosso mecanismo atual (main.cpp, verificado)

`event_thread` faz polling: loop `sleep(1)` → `__system_property_get("persist.bc_poc.reload_config")` → se `"1"`: `load_mods_config()` (re-parse + re-clamp) + zera a property + `publish_log("Info", "config recarregado")`. O companion seta a property no comando `reload_config`. Custo: 1 syscall inofensiva/1s, fora do hot path de hooks. Semântica de aplicação: valores novos sobrescrevem os `std::atomic` / `g_cfg` que os hooks leem na próxima chamada.

Diferença conceitual chave pro veredito: no BepInEx o evento entrega **o que mudou** (delta, com valor novo pronto); no nosso polling nós **re-lemos tudo e substituímos o estado global** — não há noção de "essa chave mudou".

## 3. Veredito: mudar pra push-based via socket?

**Não — polling atual é suficiente e push via socket é pior pro nosso caso. Motivos técnicos:**

1. **O socket do companion não chega no game process.** O módulo só tem acesso ao companion em `preAppSpecialize` (depois o Zygisk é descarregado do processo — restrição SELinux documentada no próprio main.cpp). Push pós-boot exigiria manter um fd vivo através do specialize (viola o modelo do Zygisk) ou o companion injetando sinal por outro canal — exatamente o que a property já é.
2. **System property É o push nativo do Android.** `__system_property_set/get` com watch real existe na plataforma (`__system_property_wait` / FUTEX_WAKE no byte do valor) — o mecanismo de notificação "grátis, sem threads extras, sem socket" do SO. Se o objetivo é eliminar a latência de 1s, o upgrade correto é **trocar polling por `__system_property_wait`** (bloqueia até a property mudar — push verdadeiro do kernel), não introduzir socket.
3. **Latência de 1s é adequada ao caso de uso.** Config de mod é mudada por humano no Termux; ninguém percebe 1s. O hot path (hooks por frame) não é afetado — polling roda na própria thread de fundo.
4. **Frequência de escrita é baixíssima e o estado é pequeno** (6 chaves). O benefício do delta-notify do BepInEx (saber *qual* chave mudou) é irrelevante quando `load_mods_config()` inteiro custa microssegundos.

## 4. O que copiar do BepInEx (barato e vale a pena)

- **Guard de no-op**: o setter do BepInEx não dispara evento se o valor não mudou. Nosso `reload_config` re-parseia e re-loga mesmo sem mudança. Adicionar comparação pós-parse (comparar `g_cfg` novo vs antigo; só `publish_log` se houve delta) — evita spam de log e degrada silenciosa de semântica.
- **Payload delta**: se um dia o número de chaves crescer, aplicar somente chaves cujo valor mudou (comparação campo a campo) — aí sim o modelo do `SettingChanged` vira relevante.

## 5. Registro da decisão

**Manter polling (1s) + property.** Migração pra `__system_property_wait` fica como upgrade opcional de latência (documento aqui; não bloqueia nada). Push via socket: **descartado** — incompatível com o ciclo de vida do Zygisk no processo (fd não sobrevive a specialize), sem ganho real pro caso de uso.

NÃO COMPROVADO: comportamento de `__system_property_wait` em Android 16/HyperOS (API pública desde N, mas não exercitada no nosso código); se algum fork de launcher NextBep adiciona file-watch próprio (fora do core).
