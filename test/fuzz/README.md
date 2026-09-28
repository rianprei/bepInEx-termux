# test/fuzz — fuzzing dos parsers que recebem dado do usuário

O loader nativo roda DENTRO do processo do jogo. Um crash de parser aqui
derruba o jogo, e 2 mortes em 20s travam o jogo inteiro pelo `bc_crashguard`.
Por isso os alvos abaixo são exercitados com AddressSanitizer +
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
| `upatch_encoder` | `mods/u_patch/jni/u_patch_arm64.h` | os valores e offsets que o emissor de thunk arm64 codifica (return/field) |

## Rodadas longas (achar bug — trabalho humano, fora do gate)

Rodadas de 10 min por alvo, com o corpus estável **mais** as sementes dos
`.so` reais que o `ndk-build` produziu (esses ficam em `corpus-local/`, que é
gitignored, porque `libs/` também é):

```sh
python3 test/fuzz/seed_corpus.py --local      # gera corpus/ e corpus-local/
for t in c4_line elf_preflight frida_config selmix upatch_encoder; do
    clang++ -std=c++17 -g -O1 -fsanitize=fuzzer,address,undefined \
        -I jni "test/fuzz/fuzz_$t.cpp" -o "/tmp/fuzz_$t"
done
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
/tmp/fuzz_elf_preflight test/fuzz/corpus/elf_preflight test/fuzz/corpus-local/elf_preflight \
    -max_total_time=600 -max_len=300000 -print_final_stats=1 -print_coverage=1
```

Achado entra no repo por um destes caminhos, sempre:
1. a entrada mínima em `test/fuzz/fixtures/<alvo>/` (o gate replaya);
2. um `[Caso N]` no `test/selftest_harness.cpp` (prova a correção sem
   sanitizer, para quem roda o host sem toolchain de fuzz).

## Etapa do gate (o que o `verify_all.sh` roda)

`test/fuzz/run_fuzz_gate.sh`: um harness por alvo da lista dele, `-seed` fixa,
`-runs` fixa, sobre o corpus versionado. O tempo total não é uma meta do gate
(o que importa é exercitar todos os alvos com sanitizer dentro do orçamento);
~25s no total, determinístico (sem `-max_total_time`, que tornaria o resultado
dependente da carga da máquina).

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

## Limites

O gate recusa contagem literal de alvo no texto de fuzz
(`test/fuzz/check_no_stale_counts.py`, chamado pelo `run_fuzz_gate.sh` antes de
compilar qualquer coisa). O vocabulário dele é fechado e vive numa lista só, no
código: os padrões de casamento são montados a partir dessa lista, e o
`test/fuzz/stale_counts_vocab_test.py` falha se um item da lista não estiver
escrito no docstring do check e nesta seção — e também se um item escrito não se
comportar como a lista promete.

**Cobre, e nada mais:**

- dígito colado no substantivo (`\d+`);
- número por extenso, em português: zero, um, uma, dois, duas, três, quatro,
  cinco, seis, sete, oito, nove, dez, onze, doze, treze, quatorze, catorze,
  quinze, dezesseis, dezessete, dezoito, dezenove, vinte, trinta, quarenta,
  cinquenta, sessenta, setenta, oitenta, noventa, cem;
- número por extenso, em inglês: zero, one, two, three, four, five, six, seven,
  eight, nine, ten, eleven, twelve, thirteen, fourteen, fifteen, sixteen,
  seventeen, eighteen, nineteen, twenty, thirty, forty, fifty, sixty, seventy,
  eighty, ninety, hundred;
- quantificadores: dúzia, dezenas, dozens, par de, pair of, couple of, meia
  dúzia, half a dozen;
- marcador de quantidade, que transforma artigo em contagem: só, sozinho,
  apenas, only, just, single, único;
- artigo antes de substantivo no plural — é a troca de substantivo que carrega
  a contagem junto, e ela falha mesmo sem marcador por perto.

O número pode vir separado do substantivo por espaço, hífen ou travessão, e
aceita a grafia sem acento (`três` e `tres`, `dúzia` e `duzia`).

**Fora de escopo, por decisão escrita:**

- `vários`, `alguns`, `some` e `several`: dizem que há mais de um sem dizer
  quantos, e por isso não envelhecem quando a lista cresce;
- `integrado`, `done` e `landed`: sinônimos de merge, e quem confere merge é o
  gate de hash do `CHANGELOG.md`, não este.

Acrescentar palavra é decisão documentada: entra na lista do check primeiro, e
este parágrafo é obrigado a acompanhar. E contagem de execs, de segundos, de
entradas de watch e de linhas não é contagem de alvo — é calibração medida.
