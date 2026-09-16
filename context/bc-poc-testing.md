# BC-POC — Testing (harness host-only)

## O que é
Harness que valida as **primitivas puras** de self-test do loader sem precisar de
device/NDK: `prologue_matches()` e `scan_exec_unique()` (extraídas de
`jni/main.cpp`). Cobre 5 cenários com assinaturas **reais** do `offsetsdb.h`
(não bytes duplicados).

**Arquivo:** `zygisk-bc-poc/test/selftest_harness.cpp`

## Como rodar (comando exato)

```bash
cd ~/battlecats-mods/zygisk-bc-poc/test
g++ -std=c++17 -Wall -Wextra -I../jni -o selftest_harness selftest_harness.cpp
./selftest_harness
```

Saída esperada encerra com:

```
== Resultado: TODOS PASSARAM (0 falhas) ==
```

Exit code `0` = sucesso; `1` = alguma assert falhou (detalhe impresso por caso).

## Casos cobertos

| Caso | O que valida |
|------|--------------|
| 1 | Match exato → `prologue_matches` true + `scan_exec_unique` acha 1 match único |
| 2 | Mismatch (byte fixo corrompido) → false + scan `nullptr` |
| 3 | Alvo ausente (modela "dladdr falha") → `nullptr` (caminho DORMANT); range null / size<len sem crash |
| 4 | Wildcard (mask==0) tolera bytes arbitrários |
| 5 | Assinatura não-única (2 matches) → rejeitada (`nullptr`) |

## ⚠️ IMPORTANTE: manter as duas cópias em sync

`prologue_matches()` e `scan_exec_unique()` existem em **dois lugares**:

1. `jni/main.cpp` — a implementação **real**, usada no device.
2. `test/selftest_harness.cpp` — cópia **fiel** pra rodar no host.

Elas são mantidas **manualmente** em sync (o harness não faz `#include` do
`main.cpp` porque este puxa zygisk.hpp/dobby.h/android, que não compilam no host).

**Regra:** toda vez que editar `prologue_matches` ou `scan_exec_unique` em
`main.cpp`, replicar a mesma mudança no `selftest_harness.cpp` (e vice-versa).
Se divergirem, o harness passa verde sobre código que não é mais o que roda no
device — falso negativo de segurança.

## Limitação (honesta)
O `selftest_symbol()` real depende de `dl_iterate_phdr`/`DobbySymbolResolver`
(Android) — não roda no host. O harness valida só a lógica de decisão isolada
(as duas primitivas que `selftest_symbol` chama). Validação end-to-end do hook
continua sendo no device (via logcat, ver `context/bc-poc-hardening-summary.md`).