# test/fuzz — fuzzing dos parsers que recebem dado do usuário

O loader nativo roda DENTRO do processo do jogo. Um crash de parser aqui
derruba o jogo, e 2 mortes em 20s travam o jogo inteiro pelo `bc_crashguard`.
Por isso os quatro alvos abaixo são exercitados com AddressSanitizer +
UndefinedBehaviorSanitizer **no gate**, não só nos testes de host: os testes
de host provam que o caminho feliz funciona, o fuzzing prova que o resto da
superfície não mata o jogo.

## Alvos

| alvo | header real | entrada de usuário |
|---|---|---|
| `c4_line` | `mods/u_patch/jni/u_patch_parse.h` | linhas `.bpatch`/`.conf` do u_patch (contrato C4) |
| `elf_preflight` | `jni/bc_elf_file.h` (+ `bc_elf_symtab.h`) | o `.so` do disco, antes do `dlopen`; inclui a guarda de SONAME do frida-gadget |
| `frida_config` | `mods/u_frida/jni/u_frida_config.h` | o `frida-gadget.config`; `uf_config_is_script_mode` decide se o gadget entra (o default dele, `listen` + `on_load=wait`, **congela o jogo**) |
| `selmix` | `jni/bc_mods_conf.h`, `bc_signal.h`, `bc_pattern_scan.h`, `bc_generic_allowlist.h`, `bc_crashguard.h`, `mods/common/dump_core.h`, `mods/u_patch/jni/u_patch_dedupe.h` | o resto da superfície de string que o `selftest_harness.cpp` já exercita com dado externo |

## Rodadas longas (achar bug — trabalho humano, fora do gate)

Rodadas de 10 min por alvo, com o corpus estável **mais** as sementes dos
`.so` reais que o `ndk-build` produziu (esses ficam em `corpus-local/`, que é
gitignored, porque `libs/` também é):

```sh
python3 test/fuzz/seed_corpus.py --local      # gera corpus/ e corpus-local/
for t in c4_line elf_preflight frida_config selmix; do
    clang++ -std=c++17 -g -O1 -fsanitize=fuzzer,address,undefined \
        -I jni "test/fuzz/fuzz_$t.cpp" -o "/tmp/fuzz_$t"
done
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
/tmp/fuzz_elf_preflight test/fuzz/corpus/elf_preflight test/fuzz/corpus-local/elf_preflight \
    -max_total_time=600 -max_len=300000 -print_final_stats=1 -print_coverage=1
```

Achado vira duas coisas, sempre:
1. a entrada mínima em `test/fuzz/fixtures/<alvo>/` (o gate replaya);
2. um `[Caso N]` no `test/selftest_harness.cpp` (prova a correção sem
   sanitizer, para quem roda o host sem toolchain de fuzz).

## Etapa do gate (o que o `verify_all.sh` roda)

`test/fuzz/run_fuzz_gate.sh`: os 4 harnesses, `-seed` fixa, `-runs` fixa,
sobre o corpus versionado. ~25s no total, determinístico (sem
`-max_total_time`, que tornaria o resultado dependente da carga da máquina).

**Sanitizer ausente é FAIL, nunca SKIP.** Um gate que pula o fuzzing quando o
host não tem clang voltaria a reportar PASS sem exercitar parser nenhum — que
é o buraco que a etapa fecha. O mesmo vale para corpus vazio ou ausente.

Variáveis: `FUZZ_CXX`, `FUZZ_FUZZ_SEED`, `FUZZ_FUZZ_RUNS`,
`FUZZ_FUZZ_TIMEOUT`.

## Regra de ouro do harness

**O cap passado ao parser é sempre `sizeof` do buffer de verdade.** Todo
chamador real (`jni/main.cpp`, `mods/u_dump`) faz
`bc_seq_split(buf, key, sizeof(key), name, sizeof(name))` — o cap *é* o
tamanho do buffer, e o parser tem de caber dentro dele. Passar cap maior que
o buffer seria erro do harness, e o ASan acusaria o harness, não o produto.
O fuzzer varia o cap **dimensionando o buffer pelo cap**, nunca o contrário.
