Ver também: [ROADMAP-COMPETITORS.md](ROADMAP-COMPETITORS.md) — pesquisa de concorrência (fechada, 4/4).

# bepin-termux — Termux-centrico: Roadmap + TODOs

**Escopo reduzido (2026-09-16):** watchdog Termux:Boot, atalhos Termux:Widget,
notificação por Termux:API e CLI `bepin` unificado foram descartados a
pedido do usuário — não fazem parte do objetivo real. O objetivo é só:

1. **`push_mod`** — deploy de `.so` via socket, sem `su` (já implementado, `af898ca`).
2. **Console ao vivo** — Termux abre automaticamente com o stream de log
   quando o jogo inicia, replicando a janela de console que o BepInEx abre
   no Windows.

## Fontes pesquisadas (confirmadas, não inventadas)

- **Termux:API RUN_COMMAND** — app `com.termux.api` expõe `RunCommandService`
  (intent `com.termux.RUN_COMMAND`), que roda um script no Termux a partir
  de outro processo (com `allow-external-apps=true` em
  `~/.termux/termux.properties`). Fonte: github.com/termux/termux-api.
- **push_mod** — já implementado (`af898ca`): protocolo `push_mod <nome> <tamanho>`
  via socket no companion.cpp, sem precisar `su` no Termux (UID Termux é aceito
  por `is_authorized_uid`).

---

## Fase 1 — P0: fundamento
- [x] `push_mod <nome> <tamanho>` no companion (socket binário, sem su).

## Fase 2 — console ao vivo (objetivo atual)
- [ ] **2.1** `launch_termux_console()` no companion (`companion_handler`,
      1x por spawn = 1x por sessão do jogo): `am start` abre o Termux, depois
      `am startservice` no `RunCommandService` roda `bcpoc-stream`
      (stream de log ao vivo), foreground (`RUN_COMMAND_BACKGROUND=false`).
      Fire-and-forget — se Termux/RUN_COMMAND não estiver disponível, loga
      warning e segue normal (não trava o companion).
- [ ] **2.2** Requisito documentado no README: `allow-external-apps=true`
      em `~/.termux/termux.properties`, senão o RunCommandService recusa
      silenciosamente.

## Dependências manuais não-automatizáveis (documentadas)
- `allow-external-apps=true` precisa estar setado manualmente 1x no Termux.
- `pkg install termux-api` + app Termux:API instalado (mesma fonte/assinatura
  do Termux — F-Droid↔F-Droid ou GitHub↔GitHub).
