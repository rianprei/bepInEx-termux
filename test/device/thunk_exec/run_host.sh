#!/usr/bin/env bash
# verify da execução real do thunk — roda no host (qemu) + harness estático.
# Exit 0 só se: harness 0 falhas, run fixed OK, buga e bugb2 falharem.
# O fixed cobre mul int/float, recursão, 8x100k, sinais e o thunk field
# (write+run com canários). buga/bugb2 devem falhar (SIGBUS/lock preso).
set -u
cd "$(dirname "$0")" || exit 1
NDK=${NDK:-$HOME/Android/Sdk/ndk/23.2.8568313}
QEMU=${QEMU:-/usr/bin/qemu-aarch64}
rc=0

echo "== build thunk_exec =="
"$NDK/ndk-build" -B -j4 2>&1 | grep -E "error|warning" | grep -v "static-libstdc++" && rc=1
python3 fix_tls_palign.py libs/arm64-v8a/thunk_exec || rc=1

echo "== run fixed (tem que passar; inclui field) =="
"$QEMU" libs/arm64-v8a/thunk_exec fixed || rc=1

echo "== run buga (tem que FALHAR) =="
if [ ! -x libs/arm64-v8a/thunk_exec ]; then
    echo "binário thunk_exec não existe ou não é executável — gate não pode validar buga/bugb2"
    rc=1
else
    timeout 60 "$QEMU" libs/arm64-v8a/thunk_exec buga >/dev/null 2>&1; buga_ec=$?
    if [ $buga_ec -eq 0 ]; then
        echo "buga passou — teste NÃO pega o bug (a)"; rc=1
    elif [ $buga_ec -eq 124 ]; then
        echo "buga deu timeout — esperado SIGBUS/crash, não hang"; rc=1
    else
        echo "buga falhou como esperado (exit=$buga_ec)"
    fi
fi

echo "== run bugb2 (tem que FALHAR — hang/timeout) =="
if [ ! -x libs/arm64-v8a/thunk_exec ]; then
    echo "binário thunk_exec não existe ou não é executável — gate não pode validar buga/bugb2"
    rc=1
else
    timeout 60 "$QEMU" libs/arm64-v8a/thunk_exec bugb2 >/dev/null 2>&1; bugb2_ec=$?
    if [ $bugb2_ec -eq 0 ]; then
        echo "bugb2 passou — teste NÃO pega o bug (b)"; rc=1
    elif [ $bugb2_ec -eq 124 ]; then
        echo "bugb2 falhou como esperado (timeout 124)"
    else
        echo "bugb2 falhou com exit=$bugb2_ec — esperado timeout 124"; rc=1
    fi
fi

echo "== mods que o harness do loader lê (.so de build) =="
# O Caso 53 do selftest_harness abre ../mods/kungfux/libs/... e
# ../mods/sa2ammo/libs/... para provar o preflight de ELF no .so REAL. Se eles
# não existirem, o caso virou SKIP (achado #4b) e o gate perde essa prova —
# então o run_host.sh constrói os dois antes. São ~10s e é o que torna o
# comando autocontido numa árvore nova.
for m in kungfux sa2ammo; do
    if [ ! -f "../../../mods/$m/libs/arm64-v8a/lib$m.so" ]; then
        "$NDK/ndk-build" -B -C "../../../mods/$m" NDK_PROJECT_PATH=. \
            APP_BUILD_SCRIPT=jni/Android.mk NDK_APPLICATION_MK=jni/Application.mk \
            >/dev/null 2>&1 || { echo "build de mods/$m falhou"; rc=1; }
    fi
done

echo "== harness estático (Casos 69-77: parse, emissores, guarda, field, review) =="
# Compila de test/ como o gate faz (CWD afeta includes relativos do harness).
# De test/device/thunk_exec, ../.. = test/ (nao ../../test = test/device/test).
( cd ../.. && g++ -std=c++17 -Wall -Wextra -Werror -I../jni -I../mods/common -I../mods/u_patch/jni \
    -o /tmp/selftest_f4 selftest_harness.cpp ) || rc=1
# Roda no mesmo CWD do compile: o harness abre ../mods/<m>/libs/... e o
# fixture fica em test/fixtures/. Rodar de test/device/thunk_exec (o CWD do
# script) dava FAIL falso em dois casos e SKIP no resto.
( cd ../.. && /tmp/selftest_f4 >/dev/null ) || rc=1
( cd ../.. && /tmp/selftest_f4 | tail -1 )

echo "== harness do u_patch (Casos 69-77 + fixture C4) =="
( cd ../../.. && mkdir -p /tmp/uph_build && \
  g++ -std=c++17 -Wall -Wextra -Werror -Imods/u_patch/jni \
    -o /tmp/uph_build/upatch_harness mods/u_patch/jni/upatch_harness.cpp ) || rc=1
# Da RAIZ: o fixture C4 é test/fixtures/c4_lines.tsv.
( cd ../../.. && /tmp/uph_build/upatch_harness >/dev/null ) || rc=1
( cd ../../.. && /tmp/uph_build/upatch_harness | tail -1 )

echo "== bugb: só no DEVICE (TCG do qemu é atômico entre instruções do bloco) =="

[ $rc -eq 0 ] && echo "== verify_thunk: OK ==" || echo "== verify_thunk: FALHOU =="
exit $rc
