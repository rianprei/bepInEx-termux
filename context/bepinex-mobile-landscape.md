# BepInEx Mobile Landscape — Research & Verdict

## Complete Project List (Real URLs)

| # | Project | URL | Stars | Status | Layer |
|---|---|---|---|---|---|
| 1 | BepInEx.Android.Launcher | https://github.com/NextBep/BepInEx.Android.Launcher | 7 | Active | Dobby + Pine + IL2CPP |
| 2 | LemonLoader (MelonLoader Android) | https://github.com/LemonLoader/MelonLoader | 79 | Active | C# + Dobby |
| 3 | LSPatch | https://github.com/LSPosed/LSPatch | 9.4k | Active | ART/Java |
| 4 | YAHFA | https://github.com/PAGalaxyLab/YAHFA | 1.7k | Maintained | ART/Java |
| 5 | QuestPatcher | https://github.com/Lauriethefish/QuestPatcher | — | Active v2.10 | IL2CPP binary patch |
| 6 | Dobby | https://github.com/jmpews/Dobby | 4.8k | Maintained | Native inline hook |
| 7 | Pine | https://github.com/canyie/pine | 1.5k | Active | ART/Java |
| 8 | Frida Gadget | https://github.com/frida/frida | — | Active | Native + JS |
| 9 | catlowlevel/ModTemplate | https://github.com/catlowlevel/ModTemplate | 32 | Active | Dobby + JNI |
| 10 | Il2CppModdingTemplate | https://github.com/juanmjacobs/il2cpp-modder | 178 | Active | IL2CPP C++ |
| 11 | NepMods/Android-Mod-Menu | https://github.com/NepMods/Android-Mod-Menu | — | Active | Native C++ + Java |

## Key Dependencies (Real URLs)

| Dependency | URL | Used By |
|---|---|---|
| FusionCore | https://github.com/All-Of-Us-Mods/FusionCore | BepInEx.Android.Launcher |
| BepInExFusion | https://github.com/All-Of-Us-Mods/BepInExFusion | FusionCore |
| MelonLoaderFusion | https://github.com/All-Of-Us-Mods/MelonLoaderFusion | FusionCore |
| Starlight | https://allofus.dev/starlight | All-Of-Us-Mods |
| LSPlant | https://github.com/LSPosed/LSPlant | LSPatch (inspired by Pine) |
| Sandwitcher | https://github.com/IR0NBYTE/Sandwitcher | Pine wrapper |
| Tine | https://github.com/taisuii/tine | Pine fork |
| VirtualHook | https://github.com/PAGalaxyLab/VirtualHook | YAHFA |
| MelonLoaderInstaller | https://github.com/LemonLoader/MelonLoaderInstaller | LemonLoader |

## Verdict: Can We Reuse for Cocos2d-x?

### Direct Reuse (High Confidence)

**Dobby** (jmpews/Dobby) — YES, already in use.
- Our bc-poc links libdobby.a directly
- Same inline hook mechanism works for any native library including Cocos2d-x
- No changes needed — already proven working

**Frida Gadget** (frida/frida) — YES, as reference architecture.
- No-root injection via shared library
- JavaScript plugin system is well-documented
- Could serve as alternative/companion to our Zygisk approach
- Repo: https://github.com/frida/frida

### Partial Reuse (Needs Adaptation)

**Pine** (canyie/pine) — NO for Cocos2d-x.
- Hooks Java methods only (ArtMethod manipulation)
- Cannot hook native C++ functions
- BUT: the ArtMethod entry-point replacement pattern is instructive for understanding ART internals
- Could be useful if we ever need to hook the Java JNI bridge layer

**BepInEx.Android.Launcher** — PARTIALLY.
- The launcher architecture (native libmain.so + libfusion.so controlling dlopen order) is clever
- The ClassLoader hook + Instrumentation hook pattern could work for our case if we ever need Java-side interception
- BUT: it targets Unity IL2CPP, not Cocos2d-x
- The modpack management, config editor, log viewer are Unity-specific — not reusable
- Repo: https://github.com/NextBep/BepInEx.Android.Launcher

**LemonLoader Android** — PARTIALLY.
- The Dobby-based native hooking is reusable
- The APK patching flow (MelonLoaderInstaller) is instructive
- BUT: C# plugin system requires CoreCLR + Il2CppInterop — too heavy for Cocos2d-x
- Repo: https://github.com/LemonLoader/MelonLoader

### No Reuse

**LSPatch** — NO. Java-only (Xposed API). No native C++ support.
**YAHFA** — NO. Java-only (ArtMethod). No native C++ support.
**QuestPatcher** — NO. Unity IL2CPP binary patching at build time. Not runtime.
**Il2CppModdingTemplate** — NO. Unity-specific. Requires Cpp2IL + dump.cs.
**NepMods/Android-Mod-Menu** — NO. Mod menu UI framework, not a general loader.

## Bottom Line

For our Cocos2d-x case (native C++ game, no Unity/IL2CPP, no Java layer to hook):

- **Dobby is the only directly reusable component** — and we already use it
- **Frida Gadget is the best reference** for no-root injection patterns
- **BepInEx.Android.Launcher's native bootstrap architecture** (libmain.so controlling dlopen order) is worth studying for potential future expansion
- **Pine/LSPatch/YAHFA are irrelevant** — they're ART/Java only
- **Nothing in this landscape provides a Cocos2d-x-specific solution** — our Zygisk + Dobby approach is the right fit

---

## LemonLoader Deep-Dive (Android Dobby Wrapper + APK Patching)

Sources:
- `LemonLoader/MelonLoader_057` branch `lemon` — Rust `libmain/` + C++ `Bootstrap/`
- `LemonLoader/MelonLoaderMerge` branch `master` — C# reference implementation
- `LemonLoader/MelonLoaderInstaller` — APK patching flow

### Dobby Wrapper Pattern (C++ `Bootstrap/Managers/Hook.cpp`)

```cpp
void Hook::Attach(void** target, void* detour)
{
    if (DobbyHookMap.find(detour) == DobbyHookMap.end()) {
        Hook::HookDef* handle = nullptr;
        DobbyHookMap[detour] = handle = (Hook::HookDef*)malloc(sizeof(Hook::HookDef));
        handle->backup = *target;                       // save original target ptr

        void* org = nullptr;
        int dobby = DobbyHook(*target, (dobby_dummy_func_t)detour, (dobby_dummy_func_t*)&org);
        if (dobby != 0)
        {
            std::string dobbyOutput = "Dobby hook failed: code " + std::to_string(dobby);
            Logger::QuickLog(dobbyOutput.c_str(), LogType::Error);
            return;
        }

        if (org == nullptr)
        {
            Logger::QuickLog("Dobby hook failed: null origin", LogType::Error);
            return;
        }

        *target = org;                                  // *target now points to the original trampoline
        return;
    }
    Logger::QuickLog("trying to hook already hooked detour i think?", LogType::Warning);
}

void Hook::Detach(void** target, void* detour)
{
    if (DobbyHookMap.find(detour) == DobbyHookMap.end()) {
        Logger::QuickLog("Hook does not exist, can't unhook", LogType::Error);
    } else {
        void* stub = DobbyHookMap[detour]->backup;
        int dobby = DobbyDestroy(stub);
        if (dobby != 0) {
            std::string dobbyOutput = "Dobby unhook failed: code " + std::to_string(dobby);
            Logger::QuickLog(dobbyOutput.c_str(), LogType::Error);
            return;
        }
        *target = stub;
        stub = nullptr;
        free(DobbyHookMap[detour]);
    }
}
```

Key patterns:
- `DobbyHook(target, detour, &origin)` — standard 3-arg form
- After success: `*target = org` so caller's pointer references the trampoline (original function)
- Idempotency keyed on **detour address** (`DobbyHookMap[detour]`), not target
- Non-fatal error handling: Dobby returning non-zero or null origin → log and return, process NOT killed
- `dobby_enable_near_branch_trampoline()` called once in `Hook::Setup()` — Android ARM64 specific

### APK Patching Flow (MelonLoaderInstaller)

```
1. DetectUnityVersion → parse game's Unity version from APK
2. DownloadUnityDeps → fetch matching libunity.so (unstripped)
3. ExtractDependencies → extract MelonLoader data (melon_data.zip)
4. ExtractUnityLibs → extract Unity native libraries
5. PatchManifest → patch AndroidManifest.xml
6. InstallPlugins → install BepInEx/MelonLoader plugins
7. RepackAPK → repack and align APK
8. GenerateCertificate → sign APK
9. AlignSign → final align + sign
10. CleanUp → cleanup temp files
```

Key: All patching is **build-time** (APK repack), not runtime injection. The patched APK contains `libmain.so` → `libBootstrap.so` → `libunity.so` dlopen chain.

### Native Bootstrap (Rust `libmain/src/lib.rs`)

```rust
fn load(env: JNIEnv, _: JClass, _: JString) -> jboolean {
    load_bootstrap(&env);      // dlopen("libBootstrap.so", RTLD_LAZY)
    load_lib_unity(&env);      // dlopen("libunity.so", RTLD_NOW | RTLD_GLOBAL)
    return 1;
}
```

1. `libmain.so`'s `JNI_OnLoad` self-dlopens (`RTLD_NOW | RTLD_GLOBAL`) and registers `NativeLoader.load`/`unload` JNI methods
2. C# calls `NativeLoader.load(string)` → Rust `load()` 
3. `load_bootstrap`: dlopen libBootstrap.so, call JNI_OnLoad (Core::Inject), dlsym Java_com_melonloader_Bootstrap_Initialize, call it (Core::Initialize)
4. `load_lib_unity`: dlopen libunity.so, call its JNI_OnLoad (boots Unity engine)

### Reusable Patterns for Our Case

| Pattern | Reuse? | Why |
|---|---|---|
| Dobby wrapper with idempotency map | **YES** | Same pattern as our bc-poc — keyed on detour, non-fatal errors |
| `*target = org` trampoline sync | **YES** | Lets caller still call original through the pointer |
| `dobby_enable_near_branch_trampoline()` | **YES** | ARM64 near-branch trampoline — we should call this too |
| funchook fallback (AttachFH/DetachFH) | **YES** | Lower-level assembler fallback when Dobby can't patch |
| APK repack patching | NO | We use Zygisk (runtime), not APK repack |
| CoreCLR + Il2CppInterop | NO | Unity-specific, too heavy for Cocos2d-x |
| C# plugin system | NO | Requires .NET runtime — we're native C++ only |

### Key Differences from Our Approach

| Aspect | LemonLoader | Our bc-poc |
|---|---|---|
| Injection | APK repack (build-time) | Zygisk (runtime) |
| Target | Unity IL2CPP | Cocos2d-x native |
| Plugin system | C# + Harmony | Native C++ only |
| Dobby usage | Hook.cpp wrapper | Direct DobbyHook in main.cpp |
| Entry point | libmain JNI_OnLoad | Zygisk preAppSpecialize |
| Error handling | Non-fatal log + return | Same (already implemented) |