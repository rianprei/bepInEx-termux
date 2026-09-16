# BepInEx Harmony vs Dobby — Patching Approaches

> Fonte: BepInEx/HarmonyX GitHub (Harmony.cs, PatchProcessor.cs, Patch.cs, PatchFunctions.cs), BepInEx docs (docs.bepinex.dev/articles/dev_guide/runtime_patching.html)

## BepInEx Runtime Patching Stack

BepInEx ships **two** runtime patching libraries:

1. **HarmonyX** (fork of Harmony 2, built on MonoMod.RuntimeDetour) — primary
2. **MonoMod.RuntimeDetour** — alternative, C#-object-based patches

BepInEx docs: "You can use either or both libraries — both of them have different API but it does not matter which one you use." (source: `docs.bepinex.dev/articles/dev_guide/runtime_patching.html`)

HarmonyX is **attribute-based** (define patches via `[HarmonyPatch]` attributes) and also supports manual `Patch()` calls.

## Harmony Patch Types (Real, from PatchProcessor.cs)

A single `Patch()` call accepts **five** optional delegates:

| Type       | Timing          | Behavior                                                                 |
|------------|-----------------|--------------------------------------------------------------------------|
| `Prefix`   | Before original | Runs first; if returns `false`, **skips** the original entirely          |
| `Postfix`  | After original  | Runs after original returns; can modify return value via `ref __result`   |
| `Transpiler`| IL-level       | Modifies the IL instructions of the original method directly              |
| `Finalizer`| After postfix   | Runs after all patches; catches exceptions (like try/finally)             |
| `ILManipulator` | Pre-IL emit | Manipulates the IL before transpilation                                 |

Source: `Harmony.cs:164-173` (`Patch()` method signature) and `PatchProcessor.cs` constructor accepting all 5.

## Multi-Patch + Priority Ordering (Real, from Patch.cs)

Harmony supports **unlimited patches on the same method**, each from a different Harmony instance (owner ID). Ordering is controlled by:

1. **`priority`** (`int`): `HarmonyPriority` enum — `First (100)`, `BeforePrevious (102)`, `Last (-100)`, `AfterPrevious (-102)`, `Normal (0)`. Default = `Normal` if unspecified.
2. **`before` / `after` arrays** (`string[]`): Explicit ordering constraints by Harmony owner ID — "this patch must run before/after these other owners."

Source: `Patch.cs` (lines 295) — `Patch` class constructor takes `(MethodInfo, int index, string owner, int priority, string[] before, string[] after)`.

Patches are sorted by `PatchSorter` (`PatchFunctions.cs:15`): priority first, then `before`/`after` constraints as tiebreaker. Multiple prefixes run in descending priority order; multiple postfixes run in ascending priority order.

This means: **two different BepInEx plugins can both Prefix-patch the same method**, and the order is deterministic.

## Dobby: Inline Native Hook (What We Use)

Dobby (`DobbyHook`) replaces the **first N bytes** of a function with a jump trampoline to the replacement. Key characteristics:

- **Single replacement**: only one hook can exist per function (second `DobbyHook` on same symbol overwrites the first).
- **No ordering**: no priority, no before/after.
- **No Prefix/Postfix**: you get a full replacement function. To "Prefix" you must manually call the original inside your replacement (via the trampoline).
- **No multi-patch**: impossible for two mods to coexist on the same function.
- **Native-only**: works on C/C++ functions (what we hook: `appUpdateDraw` JNI native impl).
- **Reversible**: `DobbyDestroy` can unpatch (unlike Harmony's "patch over" model).

## Key Difference: Granularity

| Capability            | Harmony (BepInEx)                          | Dobby (ours)                          |
|-----------------------|--------------------------------------------|---------------------------------------|
| Multiple patches same fn | **Yes** — unlimited, priority-ordered     | **No** — single replacement           |
| Prefix/Postfix        | **Yes** — declarative, no original call needed | **No** — must manually call original  |
| Transpiler (IL edit)  | **Yes** — modify IL instructions           | N/A — native, operates on bytes       |
| Priority ordering     | **Yes** — by int + before/after arrays     | **No** — first-come, first-served     |
| Conflict resolution   | **Yes** — patches coexist                    | **No** — last DobbyHook wins          |
| Exception safety      | **Yes** — Finalizer catches exceptions     | **No** — hook crashes propagate       |
| Target type           | Managed methods (C#/Mono/.NET)             | Native functions (C/C++)              |
| Rollback              | Re-patch without patches                   | `DobbyDestroy`                        |

## Limitações Reais do Nosso Approach (Dobby)

### 1. Single-hook-per-function (real, blocking)
Two Zygisk modules both calling `DobbyHook("Java_jp_co_ponos_battlecats_MyActivity_appUpdateDraw", ...)` → **second overwrites first**. No multi-mod coexistence on same symbol. BepInEx/Harmony allows this via priority ordering.

### 2. No declarative Prefix/Postfix (real, design trade-off)
Harmony lets you write:
```csharp
[HarmonyPrefix] static bool SkipFrame(ref int frame) {
    if (should_skip()) return false; // skips original
    return true; // runs original
}
```
With Dobby, you write a full replacement that **must** manually call original:
```cpp
static uintptr_t fake_updatedraw(...) {
    if (should_skip()) return 0;  // skip = don't call orig
    return orig_updatedraw(...);  // must call manually
}
```
This works for our simple skip case, but for **interception without replacement** (inspect args, don't modify return), you always pay the call-through cost. No pure-prefix path.

### 3. No priority ordering (real)
Cannot express "my hook runs after mod X but before mod Y." Dobby is first-write-wins by process load order (which mod's `JNI_OnLoad` runs first). On Android Zygote, load order = APK/lib load order, not controllable by the hooks.

### 4. No exception safety net (real, crash risk)
Harmony's Finalizer wraps post-patch code in try/catch. Dobby has none — if our hook function throws/exits, game process crashes. We mitigate with the hardening patterns (Pattern 11: per-hook isolation, try_install fail-closed), but fundamentally Dobby offers no built-in safety.

### 5. No IL manipulation for partial edits (real, irrelevant for native)
Irrelevant for us (we hook native C++, not managed C#), but documented for completeness: Harmony's Transpiler can modify individual IL instructions (e.g., change a constant 1→0 inside a method without replacing the whole thing). Dobby operates at byte-level (x86/arm), requiring full instruction-boundary replacement.

## Implication para BC-POC

Our `HookPlan` struct (main.cpp:296-298) has `sdk_min` gate + fail-closed `try_install`, but **no priority field**. Adding a second mod that hooks the same JNI symbol would silently overwrite our hook (or vice versa, depending on load order). This is an **architectural limitation** of the native inline-hook approach vs BepInEx's Harmony-based model.

Para mods que precisam coexistir (ex: throttle + visual mod + crash-logger em `appUpdateDraw`), solução seria migration pra Harmony (Unity/Mono) ou usar uma shared trampoline library (DobbyHook + manual chain). Fora nosso escopo nativo atual.
