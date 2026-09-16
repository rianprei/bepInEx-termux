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
| xhook (iqiyi) | hook nativo | 1.468★ | ativo, mas só PLT hooking (mais fraco que inline hook) | busca hermes |
| SandHook | hook ART | — | licença **Anti-996** (não é OSS padrão, zona cinzenta legal p/ uso comercial) | busca freebuff |
| Substrate/Cydia Substrate (Android) | hook nativo histórico | — | efetivamente abandonado, sem release relevante recente | busca hermes |
| **shadowhook (ByteDance)** | inline hook nativo C, **mesma categoria do Dobby que usamos** | — | **ATIVO**, licença MIT, "stably used in production apps", Android 4.1→17 QPR1 Beta 4, arm/arm64, 4 modos de hook, overhead medido **0,26µs** em runtime | busca freebuff |

**Conclusão honesta**: não existe hoje um "BepInEx mobile" consolidado e vivo pra
comparar de igual pra igual — o campo é fragmentado e majoritariamente abandonado.
O único concorrente direto, ativo e tecnicamente comparável (hook nativo inline,
não bytecode ART) é o **shadowhook** da ByteDance. Nossa escolha de infraestrutura
(Zygisk em vez de Riru) já está alinhada com o que sobreviveu no ecossistema —
Riru foi descontinuado oficialmente a favor de Zygisk.

## Prioridades (ordem de execução)

### 1. Benchmark real: nosso overhead de hook vs shadowhook (0,26µs)
Não temos essa métrica medida pro bc-poc hoje. Sem medir, qualquer comparação
de "performance" é hipótese, não fato — próxima ação concreta.

### 2. Avaliar "multi hook no mesmo endereço" (recurso real do shadowhook)
shadowhook tem modo MULTI que resolve vários hooks concorrentes no mesmo
endereço sem conflito. bc-poc hoje assume 1 hook por PLANS[] slot. Gap real
a avaliar — não confirmado se é necessidade prática do bepin-termux ainda
(só 4 hooks fixos + mods dinâmicos que resolvem símbolo/pattern próprios).

### 3. Confirmar posição de licença
Já somos MIT (mais permissivo que SandHook/Anti-996, equivalente a shadowhook).
Sem ação — já correto.

### 4. Publicar esta pesquisa como parte da documentação de arquitetura
Registra a análise honesta de mercado pra não repetir a pesquisa do zero
numa sessão futura.

## O que NÃO vamos fingir

Não existe "vencer BepInEx PC em tudo" nem "vencer todo o ecossistema mobile
em tudo" de forma honesta — BepInEx tem ecossistema/IL2CPP/maturidade que não
fecha numa sessão; no lado mobile, a maior parte da concorrência morreu, o que
não é mérito nosso, é fato de mercado. O trabalho real e honesto é: manter as
vantagens estruturais já provadas (pattern scan sobrevive update, Zygisk fora
do storage do app, SO_PEERCRED), e agora medir/comparar contra o único
concorrente nativo vivo (shadowhook) em vez de contra fantasmas.
