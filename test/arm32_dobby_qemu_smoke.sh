#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
LOCK="$ROOT/tools/deps.lock"
ZIG=${ZIG:-$(command -v zig || true)}
QEMU=${QEMU:-$(command -v qemu-arm || true)}
CMAKE=${CMAKE:-"$HOME/Android/Sdk/cmake/4.1.2/bin/cmake"}
[[ -n "$ZIG" && -x "$ZIG" ]] || { echo "Zig não encontrado; defina ZIG" >&2; exit 2; }
[[ -n "$QEMU" && -x "$QEMU" ]] || { echo "qemu-arm não encontrado; defina QEMU" >&2; exit 2; }
[[ -x "$CMAKE" ]] || { echo "CMake não encontrado; defina CMAKE" >&2; exit 2; }
command -v curl >/dev/null
command -v patch >/dev/null
command -v sha256sum >/dev/null

IFS='|' read -r _ REV SHA URL < <(grep '^dobby-arm32|' "$LOCK")
TMP=$(mktemp -d)
cleanup() {
    if [[ "${KEEP_QEMU_BUILD:-0}" == 1 ]]; then
        echo "QEMU build retained at $TMP" >&2
    else
        rm -rf "$TMP"
    fi
}
trap cleanup EXIT
curl -fsSL "$URL" -o "$TMP/dobby.tar.gz"
echo "$SHA  $TMP/dobby.tar.gz" | sha256sum -c -
mkdir "$TMP/src"
tar -xzf "$TMP/dobby.tar.gz" -C "$TMP/src" --strip-components=1
patch --directory="$TMP/src" -p1 < "$ROOT/tools/patches/dobby-arm32.patch"

cat >"$TMP/arm-gcc" <<EOF
#!/usr/bin/env python3
import os, sys
tool = "$ZIG"
args = [arg for arg in sys.argv[1:] if not arg.startswith("--target=")
        and not arg.startswith("-march=armv7")]
os.execv(tool, [tool, "cc", "-target", "arm-linux-musleabihf", *args])
EOF
cat >"$TMP/arm-g++" <<EOF
#!/usr/bin/env python3
import os, sys
tool = "$ZIG"
args = [arg for arg in sys.argv[1:] if not arg.startswith("--target=")
        and not arg.startswith("-march=armv7")]
os.execv(tool, [tool, "c++", "-target", "arm-linux-musleabihf", *args])
EOF
chmod 755 "$TMP/arm-gcc" "$TMP/arm-g++"
"$CMAKE" -S "$TMP/src" -B "$TMP/build" \
    -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=arm \
    -DCMAKE_C_COMPILER="$TMP/arm-gcc" -DCMAKE_CXX_COMPILER="$TMP/arm-g++" \
    -DCMAKE_ASM_COMPILER="$TMP/arm-gcc" -DCMAKE_BUILD_TYPE=Release \
    -DDOBBY_DEBUG=OFF -DNearBranch=ON -DPlugin.SymbolResolver=ON \
    -DPlugin.ImportTableReplace=OFF -DPlugin.Android.BionicLinkerUtil=OFF \
    -DDOBBY_BUILD_EXAMPLE=OFF -DDOBBY_BUILD_TEST=OFF
"$CMAKE" --build "$TMP/build" --target dobby_static --parallel 4
"$ZIG" c++ -target thumb-linux-musleabihf -mthumb -std=c++17 -O2 \
    -I "$TMP/src/include" "$ROOT/test/arm32_dobby_qemu_smoke.cpp" \
    "$TMP/build/libdobby.a" -static -pthread -ldl -lm -o "$TMP/smoke"
if ! timeout 30 "$QEMU" "$TMP/smoke" | tee "$TMP/output"; then
    echo "qemu-arm encerrou com falha ao executar o smoke" >&2
    exit 1
fi
grep -Fq 'DobbyHook=0 target(7)=24' "$TMP/output" || {
    echo "Dobby ARM32 não alterou o resultado do hook" >&2
    exit 1
}
echo "Dobby $REV ARM32 hook smoke: PASS"
