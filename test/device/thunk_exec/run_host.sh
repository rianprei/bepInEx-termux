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
if timeout 60 "$QEMU" libs/arm64-v8a/thunk_exec buga >/dev/null 2>&1; then
    echo "buga passou — teste NÃO pega o bug (a)"; rc=1
else
    echo "buga falhou como esperado"
fi

echo "== run bugb2 (tem que FALHAR — hang/timeout) =="
if timeout 60 "$QEMU" libs/arm64-v8a/thunk_exec bugb2 >/dev/null 2>&1; then
    echo "bugb2 passou — teste NÃO pega o bug (b)"; rc=1
else
    echo "bugb2 falhou como esperado"
fi

echo "== harness estático (Casos 61-68: parse, emissores, guarda, field) =="
# Compila de test/ como o gate faz (CWD afeta includes relativos do harness).
# De test/device/thunk_exec, ../.. = test/ (nao ../../test = test/device/test).
( cd ../.. && pwd && g++ -std=c++17 -Wall -Wextra -Werror -I../jni -I../mods/common -I../mods/u_patch/jni \
    -o /tmp/selftest_f4 selftest_harness.cpp ) || rc=1
/tmp/selftest_f4 >/dev/null || rc=1
/tmp/selftest_f4 | tail -1

echo "== bugb: só no DEVICE (TCG do qemu é atômico entre instruções do bloco) =="

[ $rc -eq 0 ] && echo "== verify_thunk: OK ==" || echo "== verify_thunk: FALHOU =="
exit $rc
