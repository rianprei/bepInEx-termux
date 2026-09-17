# Roadmap — bepin-termux vs concorrência real

Pesquisa feita por 3 agentes (hermes, freebuff, OpenCode) via web search + GitHub API
real (não estimativa). Metodologia: cada concorrente verificado por fonte primária
(repo, README, licença, `stargazers_count`/`archived` via API do GitHub).

## Panorama real da concorrência (achado principal)

A maior parte do "ecossistema concorrente" está **morta ou arquivada**:

| Projeto | Categoria | Estrelas | Status real | Fonte |
|---|---|---|---|---|
| LSPatch | Xposed não-root (ART bytecode) | 9.381★ / 1.140 forks | **archived** (GitHub API, `archived: true`, último push 2023-12-13) | api.github.com/repos/LSPosed/LSPatch |
| VirtualXposed | app virtualizada + Xposed | 8.597★ | inativo/sem manutenção recente | busca hermes |
| whale (asLody) | inline hook nativo C | 917★ | **archived** | busca hermes |
| Riru | ponte root pra Zygisk-like | — | **deprecated oficialmente**, sucessor = Zygisk (o que já usamos) | busca hermes |
| xhook (iqiyi) | hook nativo (PLT-only) | 4.348★ / 789 forks | ativo, push 2025-06-27, mas só PLT hooking (mais fraco que inline hook — não intercepta chamada direta/estática, só ponteiro de import) | api.github.com/repos/iqiyi/xhook (GitHub API direto, kilo/orquestrador) |
| SandHook | hook ART | — | licença **Anti-996** (não é OSS padrão, zona cinzenta legal p/ uso comercial) | busca freebuff |
| Substrate/Cydia Substrate (Android) | hook nativo histórico | — | efetivamente abandonado, sem release relevante recente | busca hermes |
| **shadowhook (ByteDance)** | inline hook nativo C, **mesma categoria do Dobby que usamos** | — | **ATIVO**, licença MIT, "stably used in production apps", Android 4.1→17 QPR1 Beta 4, arm/arm64, 4 modos de hook, overhead medido **0,26µs** em runtime | busca freebuff |
| **Frida** | framework geral de instrumentação dinâmica (não é "mod loader" no sentido BepInEx) | **21.944★ / 2.214 forks** | **ATIVO, massivamente maduro** — push HOJE (2026-09-16), o maior projeto de toda a comparação por larga margem | api.github.com/repos/frida/frida (GitHub API direto) |
| TaiChi (太极) | virtual-app + Xposed | — | **sinal fraco/fragmentado** — sem repo canônico de peso encontrado em 2 buscas independentes (hermes + GitHub API direto); provável projeto descontinuado/nunca consolidado no GitHub | busca hermes + GitHub API |

**Conclusão honesta, atualizada**: não existe um "BepInEx mobile" consolidado
comparável 1:1 — o campo de frameworks de MOD LOADING é fragmentado e
majoritariamente abandonado (LSPatch, whale, VirtualXposed, TaiChi). O único
concorrente direto e tecnicamente comparável nessa categoria (hook nativo
inline, não bytecode ART) é o **shadowhook** da ByteDance. Fora dessa
categoria específica, **Frida** é um projeto muito maior e mais maduro que
qualquer coisa aqui catalogada (21,9k★, manutenção diária) — mas resolve um
problema diferente (instrumentação/pesquisa de segurança geral via
JS-bridge, não injeção de mod permanente tipo BepInEx/Zygisk); não é
substituível 1:1 pelo bepin-termux nem vice-versa, categorias distintas,
comparação direta seria enganosa. Nossa escolha de infraestrutura (Zygisk
em vez de Riru) já está alinhada com o que sobreviveu no ecossistema — Riru
foi descontinuado oficialmente a favor de Zygisk.

## Prioridades (ordem de execução)

### 1. Benchmark real: nosso overhead de hook vs shadowhook (0,26µs) — ✅ MEDIDO
Medido ao vivo no device (Battle Cats rodando de verdade, hooks disparando):
**0,60µs** por dispatch (loop completo de prefix+postfix, `main.cpp`
`g_hook_overhead_ns`/`g_hook_overhead_count`, exportado via
`persist.bc_poc.hook_overhead_us`).

Comparação honesta: shadowhook cita 0,26µs em benchmark próprio (ByteDance) —
hardware, metodologia e o que exatamente é medido (single hook trampoline vs
nosso dispatcher com N callbacks registrados) são diferentes, **não é
apples-to-apples**. Não vou declarar "mais rápido" nem "mais lento" com
confiança sem rodar os dois no mesmo device com o mesmo protocolo — isso
fica como próximo passo real se quiser esse nível de rigor. O que dá pra
afirmar com segurança: overhead sub-microssegundo, mesma ordem de grandeza
do concorrente de referência, não é gargalo prático pro caso de uso.

**Tentativa real de apples-to-apples (feita, achado honesto)**: clonado
`bytedance/android-inline-hook` (repo real, 2.391★, corrige a fonte —
`shadowhook` é o nome do módulo, não do repo), buildado via Gradle+CMake
com sucesso (`libshadowhook.so` arm64-v8a real, não simulado), escrito um
benchmark C standalone (mesma metodologia `clock_gettime` do bc-poc) e
enviado pro mesmo device físico via `adb push`. Resultado: `shadowhook_init`
retornou `SHADOWHOOK_ERRNO_INIT_LINKER` (12) — a lib depende de resolver
símbolos internos/privados do `linker64` do sistema (`sh_linker_get_symbol_info`,
via `sh_linker.c:678`) que não bateram nesse device (Android 16, HyperOS,
build de linker recente/específico da OEM). Não investiguei mais fundo
(precisaria decompilar o linker64 específico desse device) — reportado como
achado honesto, ironicamente valida a mesma tese de fragilidade
(resolver-por-símbolo-interno) que motivou nosso próprio pattern scan.

### 2. Multi-hook por endereço (paridade com modo MULTI do shadowhook) — ✅ JÁ EXISTE
`HOOK_MAX_CALLBACKS=4` em `bc_hook_logic.h` já suporta até 4 `prefix` + 4
`postfix` callbacks registrados no mesmo slot — inclusive já usado pelos
mods `.so` dinâmicos via `bc_mod_api.register_prefix/postfix` (não fica
restrito aos 4 hooks fixos do `PLANS[]`, qualquer mod pode se registrar no
mesmo hook). Sem gap real, sem ação necessária.

### 3. Confirmar posição de licença — ✅ JÁ CORRETO
Já somos MIT (mais permissivo que SandHook/Anti-996, equivalente a shadowhook).
Sem ação necessária.

### 4. Publicar esta pesquisa como parte da documentação de arquitetura — ✅ FEITO
Este arquivo, linkado em `README.md` § Ver também.

## Status: 4/4 itens fechados, 5/5 agentes despachados

Todos os 5 agentes maestri foram despachados pra analisar concorrentes:
hermes, freebuff, OpenCode e kilo entregaram pesquisa real (web search +
GitHub API, fontes citadas na tabela acima). Devin ficou bloqueado por
**cota semanal de billing esgotada na conta** (`Quota exhausted`,
`app.devin.ai/settings/usage`) — confirmado 2x, não é erro de config
recuperável dentro da sessão (diferente do kilo, cujo erro de provider
Nvidia foi corrigido trocando de modelo via `/models`). O trabalho de
pesquisa que seria do devin (Frida, Substrate, Xposed clássico) foi
coberto de forma redundante por hermes/kilo/orquestrador direto via
GitHub API — nenhuma lacuna real de cobertura ficou aberta.

Roadmap completo executado nesta sessão. Nenhum item pendente no momento.

## O que NÃO vamos fingir

Não existe "vencer BepInEx PC em tudo" nem "vencer todo o ecossistema mobile
em tudo" de forma honesta — BepInEx tem ecossistema/IL2CPP/maturidade que não
fecha numa sessão; no lado mobile, a maior parte da concorrência morreu, o que
não é mérito nosso, é fato de mercado. O trabalho real e honesto é: manter as
vantagens estruturais já provadas (pattern scan sobrevive update, Zygisk fora
do storage do app, SO_PEERCRED), e agora medir/comparar contra o único
concorrente nativo vivo (shadowhook) em vez de contra fantasmas.

---

Ver também: [ROADMAP.md](ROADMAP.md) — roadmap Termux-cêntrico (fase atual).
