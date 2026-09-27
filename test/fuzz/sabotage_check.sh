#!/usr/bin/env bash
# test/fuzz/sabotage_check.sh — prova de que o gate de fuzz ENXERGA bug.
#
# As rodadas longas de 10 min por alvo não acharam crash em nenhum dos 4
# alvos. Isso, sozinho, NÃO prova nada: um harness que morre na primeira
# validação também roda 26 milhões de vezes sem achar nada. Este script
# injeta um OOB PROPOSITAL em cada parser, mostra o sanitizer pegando, e
# restaura o arquivo. Se o gate não falhar com o OOB injetado, o gate não
# está olhando o código de parse — e aí o "sem achado" das rodadas longas não
# vale nada.
#
# Não faz parte do verify_all: é verificação de VERIFICAÇÃO, roda à mão.
#   bash test/fuzz/sabotage_check.sh
set -uo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
FUZZ_DIR="$ROOT/test/fuzz"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
CXX=${FUZZ_CXX:-clang++}

export ASAN_OPTIONS="detect_leaks=1:allocator_may_return_null=1:exitcode=1"
export UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1:exitcode=1"

pass=0
fail=0

# backup/restore: cp em vez de git checkout, para não mexer no índice.
save() { cp "$1" "$WORK/$(basename "$1").bak"; }
restore() { cp "$WORK/$(basename "$1").bak" "$1"; }

# run_target <alvo> <header> <n runs>
run_target() {
    local t=$1 runs=$2
    ( cd "$ROOT" && "$CXX" -std=c++17 -g -O1 -fsanitize=fuzzer,address,undefined \
        -I "$ROOT/jni" "$FUZZ_DIR/fuzz_$t.cpp" -o "$WORK/$t" ) 2>"$WORK/$t.cc.log" || {
        echo "SABOTAGEM $t: falha ao compilar"; cat "$WORK/$t.cc.log" >&2; return 2; }
    ( cd "$WORK" && timeout 120 "$WORK/$t" "$FUZZ_DIR/corpus/$t" \
        -seed=20260926 -runs="$runs" -max_len=131072 ) >"$WORK/$t.run.log" 2>&1
    return $?
}

report() {
    local name=$1 verdict=$2 log=$3
    if [ "$verdict" = 0 ]; then
        echo "FALHOU  $name: o gate PASSOU com o OOB injetado — o harness nao enxerga bug"
        fail=$((fail + 1))
    else
        local decisive
        decisive=$(grep -m1 -E "ERROR: (AddressSanitizer|LeakSanitizer)|runtime error:|SUMMARY: " "$log" || true)
        echo "OK      $name: gate falhou como deve"
        echo "        $decisive"
        pass=$((pass + 1))
    fi
}

# --- 1. u_patch_parse.h: tira a guarda de tamanho 0 do up_split_class -------
# O achado #12 do repo: com nsz == 0, `nlen = nsz - 1` vira SIZE_MAX e o
# memcpy copia o resto do heap.
F1="$ROOT/mods/u_patch/jni/u_patch_parse.h"
save "$F1"
python3 - "$F1" <<'PY'
import sys
p = sys.argv[1]; s = open(p).read()
old = "    if (!cls || !*cls || nsz == 0 || nmsz == 0) return false;"
new = "    if (!cls || !*cls) return false;  // SABOTAGEM: guarda de tamanho 0 removida"
assert old in s, "ancora do up_split_class nao encontrada"
open(p, "w").write(s.replace(old, new))
PY
run_target c4_line 4000; report "c4_line / up_split_class sem guarda de nsz==0" $? "$WORK/c4_line.run.log"
restore "$F1"

# --- 2. bc_elf_symtab.h: tira o bounds-check do st_name no strtab -----------
F2="$ROOT/jni/bc_elf_symtab.h"
save "$F2"
python3 - "$F2" <<'PY'
import sys
p = sys.argv[1]; s = open(p).read()
old = "        if (s->st_name == 0 || s->st_name >= strtab_size) continue;  // nunca lê fora do strtab"
new = "        if (s->st_name == 0) continue;  // SABOTAGEM: st_name >= strtab_size sem check"
assert old in s, "ancora do st_name nao encontrada"
open(p, "w").write(s.replace(old, new))
PY
run_target elf_preflight 4000; report "elf_preflight / st_name sem bounds-check no strtab" $? "$WORK/elf_preflight.run.log"
restore "$F2"

# --- 3. u_frida_config.h: tira o bounds-check de 'j.p < j.end' ------------
# `uf_json_str` abre com `if (j.p >= j.end || *j.p != '"') return false;`. Sem
# a primeira metade, um config truncado (nível de string começa em j.end)
# lê UM BYTE além do buffer do usuário. O harness aloca o buffer com o
# TAMANHO EXATO do input justamente para essa leitura de 1 byte ser visível.
F3="$ROOT/mods/u_frida/jni/u_frida_config.h"
save "$F3"
python3 - "$F3" <<'PY'
import sys
p = sys.argv[1]; s = open(p).read()
old = '''static inline bool uf_json_str(uf_json &j, const char **s, size_t *len, bool *esc) {
    if (j.p >= j.end || *j.p != '"') return false;'''
new = '''static inline bool uf_json_str(uf_json &j, const char **s, size_t *len, bool *esc) {
    if (*j.p != '"') return false;  // SABOTAGEM: sem o guarda de j.p >= j.end'''
assert old in s, "ancora do uf_json_str nao encontrada"
open(p, "w").write(s.replace(old, new))
PY
run_target frida_config 4000; report "frida_config / uf_json_str sem j.p >= j.end" $? "$WORK/frida_config.run.log"
restore "$F3"

# --- 4. bc_signal.h: tira o guarda de cap 0 do bc_seq_take -----------------
# `n == 0` é o que impede `n - 1` de virar SIZE_MAX no strncpy — o mesmo
# formato do achado #12 do up_split_class, no caminho do valor da property
# persist.* (que vem do JNI, ou seja, de fora).
#
# NOTA sobre a tentativa anterior: tirar `pat->len > len` do
# bc_pattern_scan_buffer NÃO é sabotagem válida — o `for (i = 0; i + pat->len
# <= len; i++)` já impede o acesso, então remover a checagem redundante não
# introduz bug nenhum. Um teste de sabotagem que não quebra nada prova que o
# teste é ruim, não que o alvo é seguro.
F4="$ROOT/jni/bc_signal.h"
save "$F4"
python3 - "$F4" <<'PY'
import sys
p = sys.argv[1]; s = open(p).read()
old = "    if (cur == nullptr || cur[0] == '\\0' || last_seen == nullptr || n == 0) return false;"
new = "    if (cur == nullptr || cur[0] == '\\0' || last_seen == nullptr) return false;  // SABOTAGEM: sem o guarda de n == 0"
assert old in s, "ancora do bc_seq_take nao encontrada"
open(p, "w").write(s.replace(old, new))
PY
run_target selmix 4000; report "selmix / bc_seq_take sem guarda de n == 0" $? "$WORK/selmix.run.log"
restore "$F4"

echo
echo "sabotage_check: $pass/$((pass + fail)) alvos com o OOB injetado foram PEGOS pelo gate"
if [ "$fail" -ne 0 ]; then
    echo "sabotage_check: FALHOU — o gate nao distingue parser corrigido de parser quebrado" >&2
    exit 1
fi
