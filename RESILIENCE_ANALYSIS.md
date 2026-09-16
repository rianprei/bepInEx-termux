# Análise de Resiliência e Crash Safety — Zygisk BC POC

**Arquivo analisado:** `jni/main.cpp` + `jni/companion.cpp` (estado 2026-09-13)
**Objetivo:** avaliar resiliência a updates do APK do jogo e crash safety do processo.

---

## 1. Resiliência a update do APK (o risco central)

O Battle Cats é **Cocos2d-x** — não Unity/IL2CPP. Cada update do jogo recompila
`libnative-lib.so`, mudando offsets e potencialmente os exports JNI. O módulo foi
desenhado pra sobreviver a isso sem intervenção manual (regra de ouro, ver
`projects/BattleCatsModding.md`).

### Camadas de defesa implementadas

| Camada | Mecanismo | O que faz se o jogo mudar |
|--------|-----------|---------------------------|
| **Build-id gate** | `verify_build_id()` lê `PT_NOTE` e compara com `BC_BUILD_ID` | Mismatch → desliga **só** o caminho base+offset (não bloqueia símbolo/scan) |
| **Resolução em cascata** | `selftest_symbol()`: RVA → símbolo → assinatura | Tenta 3 caminhos antes de dormir o hook |
| **Assinatura de prólogo** | `offsetsdb.h` (`bc_sig_*` com máscara wildcard) | Sobrevive a shift de offsets entre builds menores |
| **Fail-closed** | qualquer divergência → `g_dormant = true` | Hook DORMANT, processo do jogo continua normal |

### Cenários de mudança no jogo

| Cenário | Comportamento | Crash? |
|---------|---------------|--------|
| Offset muda (patch menor) | Cascata cai no scan por assinatura | Não |
| Símbolo renomeado | Resolve por assinatura, ou DORMANT | Não |
| Símbolo removido | Scan falha → hook DORMANT, demais seguem | Não |
| Package renomeado | `is_bc()` false → `DLCLOSE_MODULE_LIBRARY` | Não |
| `libnative-lib.so` renomeada | `wait_lib_loaded` timeout (5s) → sem hooks | Não |

**Conclusão:** nenhum cenário de update leva a crash. Pior caso é módulo
DORMANT (log-only), e a falha é **granular por hook**, não global.

---

## 2. Crash safety — pontos críticos (estado atual)

### 2.1 DobbyHook com falha parcial (`try_install`)

```cpp
int rc = DobbyHook(sym, (void *)p->replacement, (void **)p->backup);
if (rc != 0) { g_dormant.store(true); return false; }
if (*p->backup == nullptr) {
    // NÃO chama DobbyDestroy aqui: estado meio-instalado = comportamento indefinido.
    g_dormant.store(true); return false;
}
```

**Avaliação:** ✅ SEGURO. O bug histórico (chamar `DobbyDestroy` sobre um hook
meio-instalado, risk de corromper o i-cache) foi **corrigido**: agora só marca
dormant e retorna — o fake já trata `orig == nullptr` (retorna 0, não crasha).

### 2.2 Forward do original (`hook_std`)

```cpp
if (orig == nullptr) { LOGE("hook %s sem fallback", name); return 0; }
return orig(a0, ..., a7);
```

**Avaliação:** ✅ SEGURO. Null-check antes do call. Sem use-after-free.

### 2.3 Race g_sdk (`postAppSpecialize`)

`g_sdk.store(get_sdk_level(env))` roda **antes** do `pthread_create`. Se fosse
depois, a thread de hooks leria `g_sdk == 0` e mandaria tudo pra DORMANT por SDK
gate (race determinística). **Corrigido.**

### 2.4 JNI exception (`get_sdk_level` / `jni_clear_exceptions`)

`DeleteLocalRef` em todos os caminhos; `ExceptionCheck`/`ExceptionClear` após
todo call JNI que pode throw. **✅ SEGURO** — sem vazamento de local ref, sem
poison do env.

### 2.5 wait_lib_loaded (poll)

`CLOCK_MONOTONIC` + deadline real (não wall-clock); `std::atomic<int> found`.
**✅ SEGURO** — timeout de 5s garantido, sem estouro por scheduler lag.

---

## 3. Ponte companion (companion.cpp)

| Ponto | Estado |
|-------|--------|
| `SO_PEERCRED` fail-closed | ✅ UID não resolvido → recusa (não libera) |
| `read_command` loop | ✅ trata `EINTR`/`EAGAIN`/`EOF`, não trunca |
| `write_all` loop | ✅ checa retorno, trata parcial |
| `accept4` + `SOCK_CLOEXEC` + timeouts | ✅ backpressure anti-DoS |
| `bind` addrlen correto | ✅ tamanho real, não `sizeof(sockaddr_un)` |

---

## 4. Veredito

- **Crash safety**: ACEITÁVEL → todos os pontos críticos com null-check e
  fail-closed. Nenhum caminho de update leva a crash.
- **Resiliência a update**: BOA → 3 camadas de resolução + build-id gate +
  offset DB regenerável (`bc_offset_check.py --emit-header`).
- **Validado**: hooks 4/4 + ponte Termux funcionando no device físico, zero
  crash/ANR em gameplay real.

**Único trabalho de manutenção pós-update do jogo:** regenerar `offsetsdb.h`
com `bc_offset_check.py --emit-header` (a cascata de resolução cobre o gap até
lá via assinatura/símbolo).