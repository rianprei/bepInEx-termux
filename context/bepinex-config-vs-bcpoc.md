---
name: bepinex-config-vs-bcpoc
description: Config system do BepInEx (ConfigFile/ConfigEntry tipado, hot-reload via evento) vs nosso bc_mods.conf (bool por hook) — mecanismos reais com arquivo:linha e veredito de expansão
metadata:
  type: project
  status: documented
---

# Config system BepInEx vs bc_mods.conf

**Data:** 2026-09-14
**Fonte:** fork NextBep local `~/battlecats-mods/nextbep/BepInEx.Android/BepInEx.Core/Configuration/` — lido diretamente nesta sessão com line numbers (mesmo código do upstream `BepInEx/BepInEx`; linhas podem deslocar ±2 no master).

## 1. Como o ConfigFile real funciona

### 1.1 Formato e parse (ConfigFile.cs)

- Arquivo `.cfg` por... **não é um por plugin automaticamente**: cada plugin costuma ter um `ConfigFile` próprio (`BasePlugin` cria `Config/{GUID}.cfg` — `Runtimes/Unity/BepInEx.Unity.IL2CPP/BasePlugin.cs:13`), mas o sistema em si aceita qualquer caminho; o core tem um global (`ConfigFile.cs:40` `CoreConfig` → `Paths.BepInExConfigPath`).
- Formato TOML-flat: `[Section]` + `Key=Value`, comentários `#` (`Reload()`, ConfigFile.cs:265–304: trim, pula `#`, detecta `[section]`, split no primeiro `=`).
- **Chave = `(Section, Key)`** (`ConfigDefinition.cs:12,40,45` — `IEquatable`, equality por ambos os campos).

### 1.2 Tipos suportados (TomlTypeConverter.cs:18–98)

`string`, `bool`, `sbyte`, `byte`, `short`, `ushort`, `int`, `uint`, `long`, `ulong`, `float`, `double`, `decimal`, **e qualquer `Enum`** (linha 98 — converter genérico de enum). Tipos fora da lista: `Bind<T>` **lança exceção** (`ConfigFile.cs:455–457` — `Type {T} is not supported by the config system`).

### 1.3 Bind — como o plugin expõe config

`ConfigFile.cs:452–482` `Bind<T>(definition, defaultValue, description)`:
1. valida tipo conversível;
2. se entry já existe, retorna o mesmo (idempotente);
3. cria `ConfigEntry<T>` com default;
4. **se existia valor órfão no arquivo** (chave presente no `.cfg` antes do `Bind` — `OrphanedEntries`, ConfigFile.cs:47, preenchida no Reload:299–300), aplica o valor do arquivo **sobre** o default e remove do orfanato (`:469–476`).

Isso é o mecanismo central: **o arquivo manda sobre o default, na ordem em que os Binds acontecem**, sem ordem de inicialização frágil.

### 1.4 Validação/clamp (ConfigEntryBase.cs:26–29 + Acceptable*)

`ConfigEntry<T>.Value` setter → `ClampValue(value)` **antes de aceitar**:
- `AcceptableValueList<T>` (`AcceptableValueList.cs:33–36`): valor fora da lista → **vira o primeiro aceitável** (`Clamp` retorna `AcceptableValues[0]`);
- `AcceptableValueRange<T>` (`AcceptableValueRange.cs`): clamp pro min/max.

Ou seja: entrada hostil do usuário nunca chega ao plugin — é coagida no limite do tipo/range.

### 1.5 Persistência e eventos (hot-reload)

- **Auto-save**: `SaveOnConfigSet = true` por default (`ConfigFile.cs:74`); todo `Value` setter que muda de fato → `Save()` (`ConfigFile.cs:596–597`).
- **SettingChanged** evento por entry (`ConfigEntryBase.cs:11–16`) e por file (`ConfigFile.cs:590`).
- **ConfigReloaded** evento (`ConfigFile.cs:585`), disparado no fim do `Reload()` (`:304`).
- **Hot-reload real**: **não há FileSystemWatcher no core** (grep: zero ocorrências em todo o repo). `Reload()` é público mas é o *host* que decide chamá-lo (o launcher NextBep/chamadas manuais). "Hot-reload de config" no BepInEx Android = chamar `Reload()` programaticamente + handlers de `ConfigReloaded`/`SettingChanged` reagirem. NÃO COMPROVADO que algum launcher popular faça watch automático de arquivo.

## 2. Nosso bc_mods.conf hoje (comparação honesta)

| Aspecto | BepInEx ConfigFile | bc_mods.conf (companion.cpp/main.cpp) |
|---|---|---|
| Formato | `[Section]` + `Key=Value` TOML-flat | `name=on\|off` linha-por-linha |
| Tipos | 14 tipos + enums, com conversores e validação de tipo na carga | **bool only** |
| Chave | (Section, Key) | nome simples (flat) |
| Default/ordem | Bind + OrphanedEntries: arquivo vence default, independente de ordem | default-ON implícito (ausente = on) |
| Validação | Clamp por range/lista, coação no setter | parse tolerante (linha inválida descartada) |
| Escrita | auto-save no setter, atômico não garantido (File.WriteAllText) | mkstemp+fsync+rename atômico (companion), módulo só lê |
| Hot-reload | evento `ConfigReloaded`, mas Reload() é manual | **sem reload**: lido 1× no `postAppSpecialize` (por design — hooks decididos no boot) |
| Consumidor | qualquer plugin .NET, runtime | módulo no boot + companion p/ escrever |
| Testado | — | host harness (10 casos, verde) |

Diferenças estruturais que importam: no BepInEx, config é lida *a qualquer momento* pelo managed runtime (life-cycle longo); no nosso caso o processo do jogo **só lê uma vez no boot** (e o companion é efêmero re-exec'd). Nosso modelo de escrita (atômico, root-only via SO_PEERCRED) é na prática **mais rígido** que o do BepInEx.

## 3. Veredito: expandir pra valores tipados?

**Não agora — e com escopo estreito quando sim.** Racional:

1. **Consumidor único e bool-native.** Hoje a única coisa configurável são hooks individuais on/off (`try_install` gate). Tipado só ganha valor quando existir um segundo parâmetro *quantitativo* (ex.: throttle de frames como valor `int` em vez da property `persist.bc_poc.mod_enabled`, intervalo de broadcast, nível mínimo de log do stream).
2. **O caso int já existe de verdade**: o throttle de `appUpdateDraw` (60 frames hardcoded, `main.cpp` fake_updatedraw) é configurado por system property bool. **Esse** é o candidato natural pra primeira entrada tipada (`appUpdateDraw=60` — int 1..N com clamp estilo AcceptableValueRange).
3. **Custo real é pequeno se seguir o padrão BepInEx**: o formato `key=value` já aceita qualquer string; a expansão é (a) tipo por chave conhecido no código consumidor (tabela estática `nome → tipo → default → clamp`), (b) `strtol`/`strtof` + clamp no parse, (c) rejeitar valor inválido **coagindo ao default/clamp** (semântica `AcceptableValueList.Clamp` — nunca deixar o arquivo quebrar o módulo). Nada de parser TOML, nada de reflection.
4. **O que NÃO copiar**: sections (não temos namespaces de plugins), OrphanedEntries (temos tabela estática de chaves conhecidas — não há Bind tardio), auto-save no setter (nossa escrita é via companion com auth — manter).
5. **Hot-reload**: não vale no módulo (decisão de hook é do boot — re-ler significaria instalar/desinstalar hooks em runtime, Dobby unlink + risco). Se um dia fizer sentido, o caminho é o companion já ter os valores e um comando `stream`/`status` reportá-los — não o módulo re-ler arquivo.

**Formato futuro sugerido** (compatível com o atual — arquivos existentes continuam válidos):

```
# bc_mods.conf v2 — tudo opcional, default documentado no código
appTouch=off            # bool como hoje
appUpdateDraw=60        # int com clamp [1..600] (0 = desliga throttle)
```

Regra de herança: chave com valor fora do domínio → clamp coagido (não descartada como hoje), mantendo o espírito `AcceptableValueList` do BepInEx; chave desconhecida → ignorada como hoje.

## 4. Pendências

- [x] **Feito (2026-09-14, v2):** `bc_mods_conf.h` expandido pra valores tipados — `BC_MOD_BOOL` / `BC_MOD_INT` (com clamp `[min..max]`) / `BC_MOD_ENUM` (domínio fechado), schema estático por chave (o nosso "ConfigDefinition + AcceptableValues"), coação no parse no espírito BepInEx (int fora do range → clamp; enum fora do domínio → default; bool inválido → default). Compatível com arquivo v1 (bools puros continuam parseando). Consumidores atualizados: `main.cpp` (schema espelho + `throttle_every` int substitui a property `persist.bc_poc.mod_enabled` — o throttle agora é `throttle_every=N`, 0/ausente = off), `companion.cpp` (schema espelho, `list_mods` mostra todas as chaves com `(default)` marcado, `toggle_mod` restrito a bools, `set_mod <nome> <valor>` novo com validação estrita — erro em vez de clamp, pois a UI merece saber que errou). Property legada removida dos dois lados. Build verde; harness verde com 7 casos novos (11–17: defaults, tipos mistos, clamp int, domínio enum, chave fantasma, round-trip format, compat v1).
- [ ] Migrar throttle de property pra config int **só** quando o comportamento do throttle estiver validado em device (hoje é feature não testada no hardware). **Atualização:** a migração de código foi feita (throttle lê `throttle_every` do config); falta a validação em device do throttle em si.
- [ ] Schema estático duplicado em 3 lugares (main.cpp, companion.cpp, harness) — sync manual. Se a tabela crescer, extrair pra um header único (`bc_mod_schema.h`) incluído pelos três.
- [ ] `stream_source` (enum game/companion) definido mas ainda sem consumidor — reservado pro comando `stream` escolher a fonte das linhas.
