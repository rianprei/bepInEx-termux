# BC-POC Hardening — Resumo Executivo Final

Projeto: `zygisk-bc-poc` (Zygisk module que hooka JNI do Battle Cats via Dobby).
Alvo: `jni/main.cpp` (454→504 linhas) + `jni/companion.cpp` (167→225 linhas).
Data: sessão 2026-09-13.

## Resultado global

- **11 itens de hardening** identificados → **11/11 fechados**.
- **Code review de memory leak** → **sem leak**; 1 melhoria de fd hygiene aplicada.
- Build verde confirmado em cada etapa (`ndk-build -B`, arm64-v8a).
- Artefato: `libs/arm64-v8a/libbc-poc.so`.
- ✅ **VALIDADO END-TO-END NO DEVICE FÍSICO** (não só host): módulo testado no
  device, **4/4 hooks funcionando, sem crash** — selftest, instalação, cascata de
  resolução e re-forward validados em runtime real.

---

## Tabela de achados — item, severidade, status, local

| # | Item | Severidade | Status | Arquivo:linha |
|---|------|-----------|--------|---------------|
| 1 | Race condition `g_sdk` (thread lê SDK antes do store) | **Crítico** | ✅ corrigido | `main.cpp` `postAppSpecialize` (~481) |
| 2 | Uso após free / `DobbyDestroy` em estado inconsistente (backup null pós-hook) | **Crítico** (segurança) | ✅ corrigido | `main.cpp` audit pós-hook (~390) |
| 3 | `is_termux_uid` fail-open (permite qualquer UID se `stat()` falha) | **Crítico** | ✅ corrigido | `companion.cpp` `is_termux_uid` (~88) |
| 4 | `read()` sem loop → comando truncado | **Alto** | ✅ corrigido | `companion.cpp` `read_command` (~107) |
| 5 | `write()` sem checar retorno → resposta parcial | **Alto** | ✅ corrigido | `companion.cpp` `write_all` (~130) |
| 6 | TOCTOU `dladdr` no self-test | **Médio** | ✅ resolvido (refactor) | `main.cpp` `selftest_symbol` (~313) |
| 7 | `wait_lib_loaded` `ctx.found` int não-atômico | **Baixo** | ✅ corrigido | `main.cpp` `wait_lib_loaded` (~415) |
| 8 | `usleep(8000)` tempo impreciso (wall-clock) | **Baixo** | ✅ corrigido | `main.cpp` `wait_lib_loaded` (~418) |
| 9 | `g_hooks_ok` incrementado mas nunca lido (código morto) | **Baixo** | ✅ removido | `main.cpp` globals (~47) |
| 10 | `strncpy` sem null-term explicíta | **Baixo** | ✅ corrigido | `companion.cpp` `setup_abstract_socket` (~46) |
| 11 | `accept()` sem backpressure (cliente silencioso bloqueia loop) | **Baixo** | ✅ corrigido | `companion.cpp` `companion_handler` (~188) |

---

## Memory leak review (extra, além dos 11)

| Achado | Status | Local |
|--------|--------|-------|
| Sem `malloc`/`new`/`strdup` — tudo stack ou owned pelo Dobby | ✅ limpo | ambos |
| JNI local refs pareados (`DeleteLocalRef` em todos caminhos; `Get/ReleaseStringUTFChars` pareado) | ✅ limpo | `main.cpp` `get_sdk_level`/`preAppSpecialize` |
| `fd` fechado em todos caminhos de erro (`bind`/`listen` fail) | ✅ limpo | `companion.cpp` `setup_abstract_socket` |
| `client` fechado incondicionalmente (fora dos branches) | ✅ limpo | `companion.cpp` loop (~217) |
| `termux_server` vive pelo lifetime do daemon (não é leak) | ✅ OK | `companion.cpp` `companion_handler` |
| `socket(SOCK_STREAM)` → `+ SOCK_CLOEXEC` (fd hygiene p/ future fork/exec) | ✅ melhorado | `companion.cpp` `setup_abstract_socket` (~36) |

**Conclusão:** sem memory leak. A única mudança no review de leak foi o `SOCK_CLOEXEC` no listen socket (hardening latente de fd across-exec, não era leak ativo).

---

## Correções por mecanismo (visão rápida)

- **Race/TOCTOU**: itens 1 (g_sdk) e 6 (dladdr) — reordenação de init + cascata de resolução sem `dladdr`.
- **Estado Dobby inconsistente**: item 2 — removeu `DobbyDestroy` sobre hook meio-instalado.
- **Fail-open → fail-closed**: item 3 — autenticação agora recusa em vez de liberar.
- **I/O robusto**: itens 4, 5 — loops de read/write + timeout (`SO_RCVTIMEO`/`SO_SNDTIMEO`) + tratamento `EINTR`/`EAGAIN`.
- **Tempo monotônico**: item 7/8 — `CLOCK_MONOTONIC` + `std::atomic` no poll de lib.
- **Limpeza**: itens 9 (código morto), 10 (null-term), 11 (backpressure/`SOCK_CLOEXEC`).

---

## Estado final do módulo

- Estado lógico: LOADED → ACTIVE | DORMANT (fail-safe, sem crash induzido).
- Resolução de alvo: cascata RVA (build-id ok) → símbolo JNI → signature scan (match única).
- Autenticação companion: SO_PEERCRED fail-closed.
- Build: verde, `libbc-poc.so` (arm64-v8a).