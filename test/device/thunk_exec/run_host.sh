#!/usr/bin/env bash
# verify da execução real do thunk — roda no host (qemu) + harness estático.
# Exit 0 só se: harness 0 falhas, run fixed OK, buga e bugb2 falharem.
set -u
cd "$(dirname "$0")"
NDK=${NDK:-$HOME/Android/Sdk/ndk/23.2.8568313}
QEMU=${QEMU:-/usr/bin/qemu-aarch64}
rc=0

echo "== build thunk_exec =="
"$NDK/ndk-build" -B -j4 2>&1 | grep -E "error|warning" | grep -v "static-libstdc++" && rc=1
python3 fix_tls_palign.py libs/arm64-v8a/thunk_exec || rc=1

echo "== run fixed (tem que passar) =="
"$QEMU" libs/arm64-v8a/thunk_exec fixed || rc=1

echo "== run buga (tem que FALHAR) =="
timeout 90 "$QEMU" libs/arm64-v8a/thunk_exec buga >/dev/null 2>&1
[ $? -eq 0 ] && { echo "buga passou — teste NÃO pega o bug (a)"; rc=1; } \
             || echo "buga falhou como esperado"

echo "== run bugb2 (tem que FALHAR — hang/timeout) =="
timeout 90 "$QEMU" libs/arm64-v8a/thunk_exec bugb2 >/dev/null 2>&1
[ $? -eq 0 ] && { echo "bugb2 passou — teste NÃO pega o bug (b)"; rc=1; } \
             || echo "bugb2 falhou como esperado"

echo "== harness estático (Caso 57: campos + estrutura + âncoras) =="
( cd ../../.. && g++ -std=c++17 -Ijni -Imods/common -Imods/u_patch/jni \
    -o /tmp/selftest_f4 test/selftest_harness.cpp ) || rc=1
/tmp/selftest_f4 >/dev/null || rc=1
/tmp/selftest_f4 | tail -1

echo "== bugb: só no DEVICE (TCG do qemu é atômico entre instruções do bloco) =="

[ $rc -eq 0 ] && echo "== verify_thunk: OK ==" || echo "== verify_thunk: FALHOU =="
exit $rc
