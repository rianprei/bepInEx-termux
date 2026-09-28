#!/usr/bin/env bash
# test/fuzz/run_fuzz_gate.sh — etapa CURTA e DETERMINÍSTICA de fuzzing com
# sanitizers, para o tools/verify_all.sh.
#
# O que ela é: os harnesses de test/fuzz/ (ver TARGETS abaixo) compilados com
# -fsanitize=fuzzer,address,undefined, rodando sobre o corpus versionado
# (test/fuzz/corpus/<alvo>/) com seed fixa e contagem de execs fixa. Nenhuma
# dependência de tempo, de rede ou de artefato de build.
#
# O que ela NÃO é: uma substituta das rodadas longas de 10 min por alvo, que
# são o trabalho de achar bug (ver test/fuzz/README.md). A etapa do gate é o
# piso: 20-30s no total, sempre as mesmas entradas, crash = FAIL.
#
# REGRA DURA: sanitizer ausente é FAIL, nunca SKIP. Um gate que pula o fuzzing
# quando o host não tem clang voltaria a ser SKIP=0 com ZERO cobertura de
# parser — que é exatamente o buraco que esta etapa fecha.
#
# Contrato de falha (qualquer um destes = exit != 0, com a causa no stderr):
#   - clang++ ausente, ou sem -fsanitize=fuzzer/address/undefined (UBSan exige
#     instrumentação de toda a TUnit, o que o GCC não faz em modo fuzzing)
#   - o corpus versionado de algum alvo sumiu ou ficou vazio
#   - crash / heap-buffer-overflow / OOB / leak / UB em qualquer exec
#   - regressão de crash (ver test/fuzz/fixtures/<alvo>/) que não reproduz
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
FUZZ_DIR="$ROOT/test/fuzz"
CORPUS="$FUZZ_DIR/corpus"

# Nenhuma contagem literal de alvo no texto de fuzz, e ela roda AQUI, antes de
# compilar qualquer coisa: um texto desatualizado é barato de achar e caro de
# descobrir depois. Como o gate chama este script, a regra entra no verify_all
# sem um passo novo lá. FAIL, nunca SKIP — se o check sumir, o gate inteiro
# perde a regra em silêncio.
# A mensagem sai direto (o die() do script ainda não existe nesta altura).
python3 "$FUZZ_DIR/check_no_stale_counts.py" "$ROOT" || {
    echo "fuzz-gate: contagem literal de alvo no texto de fuzz; a lista cresce e o texto nao" >&2
    exit 1
}

# Semente fixa: as mutações do libFuzzer saem sempre da mesma sequência, então
# um "funciona no meu host" e um "funciona no gate" são a mesma coisa.
FUZZ_SEED=${FUZZ_FUZZ_SEED:-20260926}
TIMEOUT_TARGET=${FUZZ_FUZZ_TIMEOUT:-60}

TARGETS=(c4_line elf_preflight frida_config selmix upatch_encoder)

# Execs POR ALVO, não um número único: os alvos não custam o mesmo por exec.
# O selmix é ~20x mais lento que o frida_config (le arquivo em disco e monta
# 512 entradas de watch por exec), então um número igual para todos faria o
# gate passar de 30s por causa do alvo mais caro e o resto ia pro ar. As
# execs de cada alvo estão em RUNS_DEFAULT, abaixo; o tempo total não é uma
# meta, e sim consequência dessa tabela.
declare -A RUNS_DEFAULT=(
    [c4_line]=40000
    [elf_preflight]=40000
    [frida_config]=40000
    [selmix]=8000
    [upatch_encoder]=40000
)

die() { echo "fuzz-gate: $*" >&2; exit 1; }

PROBE=$(mktemp)
BIN=$(mktemp -d)
trap 'rm -rf "$BIN" "$PROBE"' EXIT

# --- 0. o compilador tem que existir E ter os sanitizers --------------------
CXX=${FUZZ_CXX:-clang++}
command -v "$CXX" >/dev/null 2>&1 ||
    die "$CXX ausente: o gate de fuzz exige clang com libFuzzer+ASan+UBSan (instale o clang, ou aponte FUZZ_CXX=)"
if ! printf 'extern "C" int LLVMFuzzerTestOneInput(const unsigned char*d,unsigned long s){(void)d;(void)s;return 0;}' \
     | "$CXX" -x c++ -std=c++17 -fsanitize=fuzzer,address,undefined -o "$PROBE" - 2>/dev/null; then
    die "$CXX nao compila com -fsanitize=fuzzer,address,undefined: sem libFuzzer/ASan/UBSan nao da para rodar os harnesses (FAIL de proposito, nao SKIP)"
fi

# --- 1. o corpus versionado tem que existir e estar cheio -------------------
# Sem isso, o corpus de algum alvo "esvaziado" por engano passaria a etapa sem exercitar
# parser nenhum — o mesmo buraco do SKIP, só menor.
for t in "${TARGETS[@]}"; do
    dir="$CORPUS/$t"
    [ -d "$dir" ] || die "corpus do alvo '$t' ausente: $dir"
    n=$(find "$dir" -type f | wc -l)
    [ "$n" -gt 0 ] || die "corpus do alvo '$t' vazio: $dir"
done

# --- 1b. README lista exatamente estes alvos ---------------------------------
# test/fuzz/README.md documenta os alvos na tabela ## Alvos; se a doc e o
# TARGETS divergirem, alguém adicionou alvo sem documentar (ou apagou da
# doc sem tirar do gate) — FAIL.
readme_targets=$(sed -n '/^## Alvos/,/^## /p' "$FUZZ_DIR/README.md" \
    | grep -oE '^\| `[a-z0-9_]+' | tr -d '| `' || true)
[ -n "$readme_targets" ] || die "tabela ## Alvos vazia em test/fuzz/README.md"
for t in "${TARGETS[@]}"; do
    printf '%s\n' "$readme_targets" | grep -qx "$t" \
        || die "alvo '$t' do TARGETS ausente na tabela ## Alvos de test/fuzz/README.md"
done
for t in $readme_targets; do
    printf '%s\n' "${TARGETS[@]}" | grep -qx "$t" \
        || die "test/fuzz/README.md lista '$t', fora do TARGETS"
done

# ASan/UBSan decides: crash, leitura fora do limite e leak são todos FAIL.
# allocator_may_return_null=1: um malloc que devolve null é um caminho de
# "sem memória" que o parser trata, não um OOM que o gate deve acusar.
export ASAN_OPTIONS="detect_leaks=1:allocator_may_return_null=1:detect_stack_use_after_return=1:abort_on_error=1:exitcode=1"
export UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1:exitcode=1"
export LSAN_OPTIONS="exitcode=1"

# --- 2. compila os alvos em paralelo (ver TARGETS) ---------------------------
build_pids=()
for t in "${TARGETS[@]}"; do
    "$CXX" -std=c++17 -g -O1 -Wall -Wextra -Werror \
        -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer \
        -I "$ROOT/jni" "$FUZZ_DIR/fuzz_$t.cpp" -o "$BIN/$t" \
        2>"$BIN/$t.build.log" &
    build_pids+=($!)
done
build_rc=0
for pid in "${build_pids[@]}"; do
    wait "$pid" || build_rc=1
done
if [ "$build_rc" -ne 0 ]; then
    for t in "${TARGETS[@]}"; do
        [ -s "$BIN/$t.build.log" ] && { echo "--- build log de $t ---" >&2; cat "$BIN/$t.build.log" >&2; }
    done
    die "falha ao compilar os harnesses de fuzz (log acima)"
fi

# --- 3. roda: semente fixa, contagem fixa de execs --------------------------
# -runs (e não -max_total_time) é o que mantém a etapa DETERMINÍSTICA: mesmo
# host, mesmos resultados, mesmo tempo.
failed=0
for t in "${TARGETS[@]}"; do
    log="$BIN/$t.run.log"
    # Roda sobre uma CÓPIA do corpus, dentro do tmp, por um motivo que manda:
    #   1. o libFuzzer grava as unidades novas que ELE Descobre no diretório de
    #      corpus. Se fosse o do repo, a 2a execução do gate começaria de onde
    #      a 1a parou — o gate deixaria de ser determinístico E sujaria a
    #      árvore de trabalho no momento exato em que ela deve estar limpa;
    #   2. artifact_prefix dentro do tmp: um crash não pode largar um
    #      crash-<hash> no diretório de onde o verify_all foi chamado.
    cp -r "$CORPUS/$t" "$BIN/$t.corpus"
    runs=${FUZZ_FUZZ_RUNS:-${RUNS_DEFAULT[$t]}}
    if timeout --foreground "$TIMEOUT_TARGET" "$BIN/$t" "$BIN/$t.corpus" \
        -seed="$FUZZ_SEED" -runs="$runs" -max_len=131072 -rss_limit_mb=4096 \
        -artifact_prefix="$BIN/$t." -print_final_stats=1 >"$log" 2>&1; then
        execs=$(grep -Eo 'stat::number_of_executed_units: [0-9]+' "$log" | tail -1 | grep -Eo '[0-9]+$')
        [ -n "$execs" ] || execs="?"
        printf '  %-14s %8s execs, sem achado\n' "$t" "$execs"
    else
        failed=1
        echo "--- fuzz de $t ACHOU bug (ou travou) ---" >&2
        cat "$log" >&2
        for a in "$BIN/$t."crash-* "$BIN/$t."leak-* "$BIN/$t."timeout-* "$BIN/$t."oom-*; do
            [ -f "$a" ] || continue
            # Preserva a entrada que reproduz, pra virar regressão versionada
            # em test/fuzz/fixtures/<alvo>/. Não sai do tmp antes do humans
            # olhar: o nome do arquivo é o hash que identifica a entrada.
            echo "  entrada que reproduz: $a" >&2
            if [ -d "$FUZZ_DIR/fixtures/$t" ]; then
                cp "$a" "$FUZZ_DIR/fixtures/$t/regressao-$(basename "$a")"
                echo "  salva em test/fuzz/fixtures/$t/regressao-$(basename "$a")" >&2
            fi
        done
    fi
done

[ "$failed" -eq 0 ] || die "fuzz de parser encontrou crash/OOB/leak/UB (ver acima)"
echo "fuzz-gate: ${#TARGETS[@]} alvos, seed $FUZZ_SEED, sem achado"
