# BepInEx Versioning vs BC POC - Game Update Handling

**Data:** 2026-09-14
**Fonte:** GitHub BepInEx/BepInEx, Il2CppInterop, issues #1274, #1266, #492

---

## BepInEx IL2CPP - Regeneração Automática

### Configurações de Auto-Update

**Il2CppInteropManager.cs** - Configs principais:

```csharp
// Regenera assemblies automaticamente quando desatualizados
UpdateInteropAssemblies = true (default)

// URL template para baixar Unity base libraries
UnityBaseLibrariesSource = "https://unity.bepinex.dev/libraries/{VERSION}.zip"
// {VERSION} substituído pela versão do Unity do jogo
// Se .zip já existe em unity-libs, usa cache (sem download)

// Regex para renomear tipos obfuscados por assinatura
UnhollowerDeobfuscationRegex = ""
// Tipos que batem com regex recebem nome baseado em assinatura
// Persiste entre updates (mais resiliente que nome original)
```

### Detecção Automática de Versão

**Preloader.cs** - Lógica de detecção:

```csharp
// Config: se vazio, determina automaticamente do processo
ConfigUnityVersion = string.Empty (default)

// Runtime:
InitializeUnityVersion() // extrai versão do metadata Unity
UnityVersion = automaticamente parseada do global-metadata.dat

// Check se precisa regenerar:
if (ProxyAssemblyGenerator.CheckIfGenerationRequired())
    ProxyAssemblyGenerator.GenerateAssemblies()
```

**CheckIfGenerationRequired()** - Lógica (não visível no source, mas inferida do uso):
- Compara metadata atual vs assemblies gerados
- Se mudou → regenera automaticamente
- Se mesmo → usa cache (BepInEx/interop/)

---

## Limitações do Approach BepInEx

### 1. Dependência de LibCpp2IL (metadata version)

**Issue #1274 - ASKA update crash:**
- ASKA atualizou para Unity 6 (metadata v39)
- BepInEx suportava v23-31
- **Resultado:** Crash completo no startup
- **Fix:** Atualizar LibCpp2IL para suportar v23-106 (PR #1284)

**Error log:**
```
[Error :InteropManager] Failed to generate Il2Cpp interop assemblies:
Cpp2IL.Core.Exceptions.LibCpp2ILInitializationException: Fatal Exception initializing LibCpp2IL!
---> System.FormatException: Unsupported metadata version found! We support 23-31, got 39
```

**Problema:** Quando Unity muda metadata version drasticamente, BepInEx quebra até que LibCpp2IL seja atualizado. Não há fallback automático.

### 2. Overflow em XrefScanner (Issue #492)

**Cenário:** Game update mudou estrutura IL2CPP
```
Arithmetic operation resulted in an overflow
at Il2CppInterop.Common.XrefScans.XrefScanner.XrefScanImpl
```

**Fix:** Bump Il2CppInterop (PR #503) - atualização manual da dependência.

### 3. Harcode de Hooks Unity 6 (Il2CppInterop PR #253)

**Problema:** Unity 6 usa estrutura diferente de hook
- BepInEx usava hook Unity 5 hardcoded
- Unity 6 quebrava todos os hooks

**Fix:** Detectar version.Major == 6000 → usar hook Unity 6 específico
```csharp
if (version.Major is 6000)
    hook = new Unity6Hook();
else
    hook = new LegacyHook();
```

---

## Comparação: BepInEx vs BC POC

| Aspecto | BepInEx IL2CPP | BC POC (Zygisk) |
|---------|----------------|-----------------|
| **Target** | IL2CPP metadata global | JNI exports específicos |
| **Regeneração** | Automática (Cpp2IL) | Manual (offsetsdb.h) |
| **Detecção de mudança** | CheckIfGenerationRequired() | build-id (GNU note) |
| **Fallback** | Nenhum (se metadata não suportado) | Symbol resolution + signature scan |
| **Dependência** | LibCpp2IL (metadata version) | offsetsdb.h (RVA fixo + signatures) |
| **Complexidade** | Alta (full reverse engineering) | Baixa (hooks simples) |
| **Superfície de quebra** | Toda metadata Unity | 4 funções JNI |

---

## O Que BC POC Poderia Copiar

### 1. Check de build-id automático (JÁ IMPLEMENTADO)

**BepInEx:** `CheckIfGenerationRequired()` - compara metadata atual vs cache
**BC POC:** `verify_build_id()` - compara GNU build-id vs esperado

**Status:** ✅ Já temos build-id check (linha 162-176 main.cpp)

### 2. Fallback automático (JÁ IMPLEMENTADO)

**BepInEx:** Nenhum - se metadata não suportado, crash
**BC POC:** Cascata de fallback:
1. RVA fixo + build-id confirmado
2. Symbol resolution (DobbySymbolResolver)
3. Signature scan (scan_exec_unique)

**Status:** ✅ Já temos cascata (linha 314-353 main.cpp)

### 3. Regeneração automática de offsets (NÃO IMPLEMENTADO)

**BepInEx:** Regenera assemblies automaticamente via Cpp2IL
**BC POC:** offsetsdb.h é manual - precisa rodar `bc_offset_check.py` quando jogo atualiza

**Opção possível:**
- Companion process detecta build-id diferente
- Companion notifica usuário para regenerar offsets
- Companion poderia tentar scan genérico se offsetsdb.h não bater

**Problema:** Sem analyzer como Cpp2IL, não podemos regenerar automaticamente. Só podemos:
- Detectar mudança (build-id)
- Usar symbol resolution (já funciona)
- Usar signature scan (já funciona)
- Fall back para DORMANT se nada bater

---

## Conclusão

**BepInEx approach:**
- Pró: Regeneração automática completa
- Contra: Depende de LibCpp2IL (metadata version), quebra quando Unity atualiza drasticamente

**BC POC approach:**
- Pró: Simples, 4 alvos fixos, cascata de fallback
- Contra: offsetsdb.h manual (mas build-id + symbol resolution mitigam)

**O que temos é mais resiliente que BepInEx IL2CPP em um aspecto:**
- Se symbol resolution funcionar, sobrevivemos update sem regenerar offsets
- BepInEx se metadata version não suportado → crash completo

**O que BepInEx faz melhor:**
- Detecção automática de mudança + regeneração (mas depende de toolchain pesado)

**Recomendação:** BC POC está OK com approach atual. Notificação ao usuário quando build-id muda já está implementada via `publish_log("Error", "build-id MISMATCH: ...")` (linha 171 main.cpp) - envia warning pro stream Termux ao vivo. Testado em produção nesta sessão (jogo atualizou, log disparou corretamente).
