# Playtest learnings — sessões de validação da infra em device real (Poco C75, jp.co.ponos.battlecatsen v15.6.0)

Log de achados durante execução real do mod de teste (Mecha-Bun) via adb:
o objetivo das sessões é validar a infra de ponta a ponta (hook, deploy,
hot-reload, log) — gameplay aqui é instrumento de teste, não o produto.

## 2026-09-18 18:55 — confirmação do hook em jogo real
- `[mechabun] design ideal comunitario aplicado (...)` disparou no logcat durante uma batalha comum (Korea, tutorial) sem eu ter Mecha-Bun no deck — confirma que o loader faz warm-up/parse de CSV de units fora do deck ativo (provavelmente cache de dados no boot ou entre estágios), não só quando a unit é deployada em batalha.
- Framework bc-poc precisou reboot completo pra recarregar versão nova do .so do Magisk module — `setprop ctl.restart zygote` sozinho quebra a injeção zygisk pro device inteiro (todos apps ficam sem hook), `magisk --zygote-restart` manual não resolve sozinho. Reboot completo é o único caminho confiável.
- Save novo (fresh account) não tem Cat Guide destravado até progredir estágios — Menu só mostra "Treasure" até desbloquear.

## Instrução do usuário — troféu dourado
- Sempre pegar o "troféu dourado" (ícone com estrela vermelha, aparece acima do nome/ícone da fase) — necessário pra progredir, é o item de melhor recompensa por fase.
