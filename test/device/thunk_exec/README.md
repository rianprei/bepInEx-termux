# thunk_exec — teste de EXECUÇÃO real do thunk mul (u_patch F4)

Executável arm64 estático que executa de verdade o thunk emitido por
`up_emit_mul_thunk()` (o emissor real do mod, nada duplicado): mmap RWX,
thunks int/float/recursivo, originais que sujam x9–x17 de propósito,
recursão através do fast path e 8 threads × 100k chamadas concorrentes.

## O que o run `fixed` prova (exit 0)

| Teste | Prova |
|---|---|
| `mul int/float` | resultado = orig×fator mesmo com x9–x17 sujos pelo orig (caller-saved de verdade) |
| `recursão` | 4 níveis de chamada aninhada passando pelo **fast path** (lock ocupado pela chamada externa): `3*(1+2+3+4) = 30`, sem self-loop |
| `8 threads × 100k` | sem corrupção (só `orig` ou `orig×fator` — contrato do lock por ocupação), sem crash, sem hang (alarm 120 s), `lock == 0` no fim |

## Modos de prova de falha (exit != 0 esperado)

| Modo | Bug reintroduzido | Falha observada |
|---|---|---|
| `buga` | 2º `add x17,x16,#24` vira NOP → unlock usa x17 morto pós-call | **lock preso pra sempre** (todas as 800k chamadas passam direto, `lock != 0` no fim) + SIGSEGV no stlr do teste 1 (endereço lixo 0xcafebabe00) |
| `bugb` | swap restore-lr ↔ unlock (ordem errada real) | **no DEVICE (SMP real)**: outra thread adquire o lock e sobrescreve `slot.lr` dentro da janela → retorno errado. **No qemu-user não falha** — TCG executa o bloco de instruções atomicamente entre vCPUs, a janela de 2 instruções nunca perde a corrida |
| `bugb2` | `ldr x30` vira NOP (o efeito prático da corrida do bugb) | **hang determinístico**: `ret` usa o x30 que o `blr` gravou (endereço dentro do próprio thunk) → loop infinito → timeout/`alarm` |

Regra: `fixed` verde + modos de bug vermelhos = o teste pega a classe de bug.

## Build

```bash
cd test/device/thunk_exec
~/Android/Sdk/ndk/23.2.8568313/ndk-build -B -j4
python3 fix_tls_palign.py libs/arm64-v8a/thunk_exec   # p_align do PT_TLS 8→64
```

`fix_tls_palign.py` contorna bug do lld 12 (NDK r23): em imagem estática ele
emite PT_TLS com `p_align=8` e o loader bionic aborta ("TLS segment is
underaligned, needs at least 64"). Só sobe o alinhamento do segmento — sem
efeito na lógica. (NDKs com lld ≥ 13 não precisam do passo; o script é idempotente.)

## Rodar no host (verify_all — não depende do device)

```bash
/usr/bin/qemu-aarch64 test/device/thunk_exec/libs/arm64-v8a/thunk_exec fixed   # exit 0
timeout 90 /usr/bin/qemu-aarch64 .../thunk_exec buga                           # exit != 0
timeout 90 /usr/bin/qemu-aarch64 .../thunk_exec bugb2                          # exit != 0 (142)
# bugb: rodar no DEVICE; no qemu-user não exercita a janela (TCG atômico)
```

Ou tudo de uma vez: `./run_host.sh` (build + TLS fix + os 3 modos + harness
estático — exit 0 só se TUDO estiver certo).

## Rodar no device (orquestrador)

```bash
adb push libs/arm64-v8a/thunk_exec /data/local/tmp/thunk_exec
adb shell chmod +x /data/local/tmp/thunk_exec
adb shell /data/local/tmp/thunk_exec fixed    # exit 0
adb shell /data/local/tmp/thunk_exec buga     # exit != 0
adb shell /data/local/tmp/thunk_exec bugb     # exit != 0  ← aqui o bugb REAL falha
```

## Notas de implementação

- **Slot layout** = `up_slot_t` do mod: `orig@0 / value@8 / lr@16 / lock@24`
  (`UP_SLOT_*` de `u_patch_arm64.h`).
- **Distância adrp**: o emissor usa `adrp` (±4 GB). No mod real os slots
  (`.bss`) e a página mmap nunca distam isso; o teste dá hint de mmap perto
  do `.bss` e, se impossível, move os slots pra dentro da página (sempre
  encodável). Primeira versão do teste deixou o mmap do qemu a 140 TB do
  `.bss` → SIGILL por endereço selvagem no próprio thunk.
- **Mapa da página**: int 0x000–0x054 | float 0x080–0x0D4 | slot rec
  0x0E0–0x100 | thunk rec 0x100–0x154. (0xC0 sobrepunha o float thunk —
  escrever `orig` corrompia as últimas palavras dele.)
- **Evidência de disassembly**: 21 palavras emitidas → `.inst` em `.s` →
  `clang --target=aarch64-linux-android23 -c` → `llvm-objdump -d`. Texto
  completo no commit. Os alvos de `adrp/add` são validados semanticamente
  pelo Caso 70 do upatch_harness (decodificação de campo + `decode_pair`, o
  lambda em `upatch_harness.cpp:230`). O número anterior apontava para o caso
  de dump_core/IL2CPP, que não decodifica `adrp/add`.
- **qemu**: `qemu-aarch64 11.1.1` (TCG, MTTCG). Threads reais via host threads.
