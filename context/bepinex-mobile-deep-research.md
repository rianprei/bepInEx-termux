# BepInEx PC vs zygisk-bc-poc — Deep Research: Dependencies, Config Watch, Logging

> Fontes: BepInEx/BepInEx GitHub master — BaseChainloader.cs (ModifyLoadOrder, LoadPlugins), ConfigFile.cs (SettingChanged event), PluginInfo.cs (Dependencies property), docs.bepinex.dev (plugin tutorial, configuration). Pesquisado 2026-09-15.

## 1. Plugin Dependency Resolution (BepInEx PC)

### BepInEx: `[BepInDependency]` attribute + TopologicalSort

**How it works (BaseChainloader.cs lines 140-210, `ModifyLoadOrder`):**

1. Cada plugin declara dependências via `[BepInDependency("GUID", DependencyFlags.HardDependency)]` (hard) ou `SoftDependency` (soft)
2. Chainloader coleta todos os plugins + suas deps (via `BepInDependency.FromCecilType(type)` — le linha 92 do source)
3. **Resolução via `Utility.TopologicalSort`** (line 222-223 do source):

```csharp
var sortedPlugins = Utility.TopologicalSort(
    dependencyDict.Keys,
    x => dependencyDict.TryGetValue(x, out var deps) ? deps : emptyDependencies
).ToList();
```

4. **Hard deps missing → plugin skipped + error logged** (line 228-241: `missingDependencies`)
5. **Soft deps missing → plugin ainda carrega** (não entra na lista de erros)
6. **Version range support:** `[BepInDependency("GUID", "~1.2")]` → `dependency.VersionRange.IsSatisfied(pluginVersion)` (line 196)
7. **Incompatibilities:** `[BepInIncompatibility]` → plugins na lista de incompatibilidade são removidos (line 166-185)
8. Plugins já carregados ficam no `Dictionary<string, PluginInfo> Plugins` (line 82) — ordem de load é topológica

**Key detail:** resolução é feita **antes** de instanciar plugins (na fase `ModifyLoadOrder`), e é **global** — um plugin não pode ser carregado sem que suas deps já estejam em `Plugins`.

### zygisk-bc-poc: não tem dependency resolution

**Current state:** cada Zygisk module é carregado independente pelo `zygiskd` daemon. Nenhum módulo sabe se outro foi carregado. Hooks são instalados direto via `DobbyHook` — não há topo sort, nem fallback se dependency ausente.

**Gap real:** dois mods zygisk hookando `appUpdateDraw`:
- Mod A hooka primeiro → Dobby instala trampoline
- Mod B hooka segundo → `DobbyHook` **sobrescreve** o hook do Mod A (Dobby é single-replacement, não Harmony multi-patch)
- Mod B não sabe que Mod A existia → **Mod A hook é perdido silenciosamente**

**BepInEx equivalent:** Harmony permite multi-patch na mesma função (Priority ordering) — múltiplos mods coexistem.

---

## 2. ConfigEntry Watch (BepInEx PC)

### BepInEx: per-key `SettingChanged` event

**How it works (ConfigFile.cs lines 444-478):**

```csharp
public event EventHandler<SettingChanged> SettingChanged;

internal void OnSettingChanged(object sender, ConfigEntryBase changedEntryBase)
{
    if (SaveOnConfigSet) Save();  // salva pra disco automaticamente
    var settingChanged = SettingChanged;
    if (settingChanged == null) return;
    var args = new SettingChangedEventArgs(changedEntryBase);
    foreach (EventHandler<SettingChangedEventArgs> callback in settingChanged.GetInvocationList())
        try { callback(sender, args); }
        catch (Exception e) { Logger.Log(LogLevel.Error, e); }
}
```

3 tipos de eventos:
1. **`ConfigReloaded`** — disparado no fim de `Reload()` (arquivo inteiro recarregado do disco)
2. **`SettingChanged`** — disparado **por chave individual** quando `ConfigEntry<T>.Value` setado (`ConfigEntry.cs:53-63`, via `OnSettingChanged`)
3. **`ConfigReloaded` + individual `SettingChanged`** — quando config é reloadado, cada entrada que **changed** dispara seu evento individual

**Key detail:** callback é **por chave** (ConfigDefinition = section+key). Plugin A subscreve `throttle_every` → callback dispara só quando `throttle_every` muda, não quando outra chave muda.

### zygisk-bc-poc: reload_config compara antes/depois inteiro

**Current state (main.cpp lines 887-928):**

```cpp
struct bc_mod_entry before[8];
memcpy(before, g_cfg, sizeof(before));  // snapshot do estado anterior
load_mods_config();                     // parseia arquivo inteiro
// diff: compara before[i] vs g_cfg[i] campo por campo
for (int i = 0; i < BC_SCHEMA_N; i++) {
    switch (BC_SCHEMA[i].type) {
        case BC_MOD_BOOL: same = (before[i].b == g_cfg[i].b); break;
        case BC_MOD_INT:  same = (before[i].i == g_cfg[i].i); break;
        default:          same = (strcmp(before[i].s, g_cfg[i].s) == 0); break;
    }
    if (!same) {
        publish_log("Info", "reload_config: %s mudou", g_cfg[i].name);
    }
}
```

**Gap real:**
- **Granularidade por chave:** nosso diff é `for i in 0..N` → loga todas as chaves que mudaram, mas **não dispara callbacks por chave**. Um mod que subscreve `throttle_every` não tem jeito de ser notificado só quando `throttle_every` muda — ele precisa fazer polling ou parsear o log.
- **No individual event callbacks:** BepInEx permite `config.Entry.SettingChanged += MyCallback` — nosso modelo cross-process (property polling) não suporta subscription.
- **SaveOnConfigSet automático:** BepInEx salva pro disco quando um valor setado. Nosso modelo: companion escreve arquivo, game process só lê (não escreve).

**Portabilidade do conceito:** no modelo Zygisk, a subscrição seria: game process mantém uma **lista de watchers por chave** (callback function pointer). `publish_log("Info", "%s mudou", key)` vira uma chamada para o watcher registrado daquela key. Mas cross-process não funciona (companion não pode registrar callback no game process via property).

---

## 3. Logging (BepInEx PC vs zygisk-bc-poc)

### BepInEx: structured logging com listeners

**How it works (Logger.cs lines 28-120, LogListenerCollection):**

1. **`LogSourceCollection`** (thread-safe via `SpinLock`) — mantém lista de `ILogSource`
2. **`LogListenerCollection`** — mantém lista de `ILogListener` (Console, Disk, Trace)
3. **`LogLevel`** bitmask: `Fatal | Error | Warning | Message | Info | Debug | Verbose` (line 69 — flags enum)
4. **Disk logging** (BaseChainloader.cs lines 304-312):
   ```csharp
   Logger.Listeners.Add(new DiskLogListener("LogOutput.log", LogLevel...));
   ```
   - File: `BepInEx/LogOutput.log`
   - Configurável: append, instant flush, log levels, file limit (5 arquivos simultâneos)
5. **`InternalLogSource`** = singleton "BepInEx" source (line 30) — todos os logs internos usam ele

### zygisk-bc-poc: logcat + socket streaming

**Current state:**
- `__android_log_print(ANDROID_LOG_INFO, "BCPOC", ...)` → logcat (volátil, rotacionado)
- `publish_event`/`publish_log` → socket pro companion → Termux (formato BepInEx-style `[Level:Source] msg`)
- **Nenhum disco logging** — logs são perdidos no reboot

**Gap real:**
- **Persistência:** `LogOutput.log` do BepInEx persiste entre sessions. Nosso logcat é volátil.
- **Structured access:** BepInEx tem API `Logger.Log(LogLevel.X, ...)` — nível como enum. Nosso LOGI/LOGE é macro direto pro logcat.
- **Multi-listener:** BepInEx envia pro console + disco + trace simultaneamente. Nós só logcat + socket.

**Superioridade mobile (o ganho):**
- **Out-of-band access:** BepInEx logging só funciona se processo anexado (IDE/debugger) ou lendo arquivo. Nosso socket `@bc_companion` permite logging via network/device sem debugger.
- **`hook_overhead` property:** exportamos métrica real pra Termux — algo que BepInEx console não faz.

---

## 4. Recommendation: O que portar?

### Portar (alto valor):
1. **Dependency resolution via config schema** — não topo sort entre mods (impossível no Zygisk scope), mas **hook-level depends-on** (ex: `appKey` hook depends on `appInit` hook estar instalado primeiro). Isso **já foi implementado** (campo `requires` no HookPlan). ✅

2. **Per-key config watch via callback list** — no game process, manter `struct { const char *key; void (*callback)(const char*); }` registry. `reload_config` diff per-key dispara callbacks específicos. **Portável**, mantém cross-process property como trigger. **Não precisa de device para testar no harness.**

3. **Disk log fallback** — companion já tem acesso root ao filesystem. Ele pode escrever `LogOutput.log` em `/data/local/tmp/` enquanto lê do socket. **Portável,** teste host-only via mock.

### Não portar (impossível/inútil no mobile):
1. **Multi-mod topo sort via DLL metadata** — Zygism modules não expõem metadata de dependência (manifest.xml, não assembly attributes).
2. **Harmony multi-patch** — Dobby é single-replacement (limitação fundamental). Múltiplos mods hookando o mesmo JNI symbol se sobrescrevem. **Workaround:** usar `HookCallbacks` dispatcher (já implementado — múltiplas prefixes/postfixes, mas dentro do MESMO mod, não cross-mod).
3. **ConfigEntry<T>.SettingChanged event subscription de outros mods** — cross-process property não suporta delegates.

---

## 5. Cross-reference: zygisk-bc-poc gaps não documentados

- **`list_patches` accept loop serial** (companion.cpp line 720) — `handle_list_patches` bloqueia ~2.5s no accept loop. **Fix implementado:** thread-per-client + atomic seq. Teste no harness (Cases 33-34) valida atomicidade e seq correlation. **Teste device:** 2 clients simultâneos confirmados (16091 vs 16117 hits). ✅
