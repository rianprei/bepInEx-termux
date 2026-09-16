# bepin-termux — Termux-centrico: Roadmap + TODOs

Rumo: fazer do **Termux** a peça central (deploy de mods, controle, boot-watchdog,
notificações), não um cliente de monitoramento opcional. Termux.deixa de ser só
stream/ping — vira o painel de controle do device.

## Fontes pesquisadas (confirmadas, não inventadas)

- **Termux:Boot** — F-Droid `com.termux.boot` (v0.8.1, GPL-3.0, autor Tarek Sander).
  Executa scripts em `~/.termux/boot/` em ordem **sorted** (alfabética) após
  BOOT_COMPLETED. **Requer abrir o app 1 única vez** pra registrar o receiver.
  Recomenda `termux-wake-lock` como primeira linha pra evitar sleep.
  Fonte: f-droid.org/packages/com.termux.boot + github.com/termux/termux-boot/README.md.
- **Termux:API** — app `com.termux.api` + pkg `termux-api` (comandos `termux-*`).
  Relevantes: `termux-notification` (opts `-t título`, `-c conteúdo` ou stdin,
  `--id`, `--priority`, `--sound`, `--vibrate`, `--ongoing`, `--action`),
  `termux-wake-lock`, `termux-wake-unlock`, `termux-battery-status` (JSON).
  Fonte: github.com/termux/termux-api-package/scripts/termux-notification.in +
  wiki.termux.com/wiki/Termux-notification + mintlify.wiki/termux/termux-app/plugins/termux-api.
- **push_mod** — já implementado (`af898ca`): protocolo `push_mod <nome> <tamanho>`
  via socket no companion.cpp, sem precisar `su` no Termux (UID Termux é aceito
  por `is_authorized_uid`).

## Decisão de design
Notificação por **evento** (companion manda `!WARN`/`!ERR` no stream que o
watchdog lê) é superior ao **polling**. Mas depende de CLI/stream estáveis —
por isso nota-se polling primeiro, evento depois como evolução. Polling via
`list_patches` (já existe) + diff, ou flag de DORMANT já presente nas properties
`persist.bc_poc.*` herdadas.

---

## Fase 1 — P0: fundamento (já em curso)
- [x] `push_mod <nome> <tamanho>` no companion (socket binário, sem su).
- [ ] **1.1** Tests + `dlopen` do .so recebido pelo MESMO loader de
      `load_dynamic_mods` (reusar `bc_loader` + `bc_mod_graph`) — o push não
      deve ter caminho de load separado/diferente do diretório.
- [ ] **1.2** Comando `list_mods_dynamic` — lista .so carregados + estado
      (active/inactive/rejected), par do `list_patches` estático.
- [ ] **1.3** `reload_dynamic` — recarrega .so de `BC_MODS_DIR` sem reboot do
      jogo (dlclose + re-dlopen; hot-reload).

## Fase 2 — P1: Termux peça central
- [ ] **2.1** CLI `bepin` (bash) — interface única documentada, substitui
      `termux_client.py`/`bc_log_viewer.py` como canônico. Subcomandos:
      `status`, `list_patches`, `list_mods`, `push_mod <arq>`, `unpatch`,
      `repatch`, `toggle_mod`, `set_mod`, `stream`, `log`. Conecta no socket
      abstract direto (UID Termux autorizado, sem su).
- [ ] **2.2** `bepin push_mod http://…` melhor: ler binário, handshake
      `push_mod <nome> <tam>`, mandar bytes, verificar ack.
- [ ] **2.3** Notificação DORMANT/falha de mod (Termux:API): watchdog lê estado
      (via `bepin list_patches` + diff, ou property) e chama
      `termux-notification -t "bepin: DORMANT" -c "<hook> caiu"`.
- [x] **2.4** Boot-watchdog (Termux:Boot): `termux-boot/bepin-watchdog.sh`
      (`b016e92`) — `termux-wake-lock` primeiro; loop: ping companion, loga
      estado via `logger`. Limitação real: não pode reiniciar o companion
      (filho do processo do jogo, não existe antes do jogo abrir) — só
      monitora e loga, documentado no próprio script.
- [x] **2.5** Termux:Widget 1-toque (`termux-shortcuts/`, `cc8babd`):
      `bcpoc-stream` (foreground) + `tasks/bcpoc-{toggle,status,sweep}`
      (background). Fonte: github.com/termux/termux-widget README.

## Fase 3 — P2: robustez
- [ ] **3.1** Notificação por evento: companion manda `!WARN`/`!ERR` no stream;
      watchdog lê `bepin stream` e dispara `termux-notification` ao ver.
- [ ] **3.2** `termux-battery-status` no watchdog — pular notif `--ongoing` se
      bateria baixa (economia).
- [ ] **3.3** Documentar auth da CLI (socket abstract já não precisa su; UID
      autorizado é Termux/2000/0).
- [ ] **3.4** `bepin setup` — cria `~/.termux/boot/`, `pkg install termux-api`,
      chmod +x; instrui "abrir Termux:Boot 1x" (limitação de UI do Android,
      não automatizável).

## Ordem de execução recomendada (prioridade)
1. **Fase 1** (1.1→1.3): fecha ciclo push de mod via socket (a base).
2. **2.1 CLI `bepin`**: interface única; tudo depois usa `bepin`.
3. **2.3 notificação** + **2.4 boot-watchdog**: automação prática do device.
4. **Fase 3**: robustez/conveniência.

## Dependências manuais não-automatizáveis (documentadas)
- **Termux:Boot** precisa ser aberto 1 vez (UI Android) — `bepin setup` só instrui.
- **pkg `termux-api`** precisa estar instalado.
- Termux e Termux:Boot devem ser do **mesmo canal de install** (F-Droid) e
  assinados pela mesma key — doc oficial do termux-boot.