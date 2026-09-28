#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
LOCK="$ROOT/tools/deps.lock"
NDK=${NDK:-"$HOME/Android/Sdk/ndk/23.2.8568313"}
CMAKE=${CMAKE:-"$HOME/Android/Sdk/cmake/4.1.2/bin/cmake"}
command -v curl >/dev/null
command -v patch >/dev/null
command -v sha256sum >/dev/null
command -v python3 >/dev/null
[[ -x "$NDK/ndk-build" ]] || { echo "ndk-build ausente: $NDK" >&2; exit 1; }
[[ -x "$CMAKE" ]] || { echo "CMake ausente: $CMAKE" >&2; exit 1; }

IFS='|' read -r _ REV SHA URL < <(grep '^dobby-arm32|' "$LOCK")
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
ARCHIVE="$TMP/dobby.tar.gz"
curl -fL "$URL" -o "$ARCHIVE"
echo "$SHA  $ARCHIVE" | sha256sum -c -
mkdir "$TMP/src"
tar -xzf "$ARCHIVE" -C "$TMP/src" --strip-components=1

patch --directory="$TMP/src" -p1 < "$ROOT/tools/patches/dobby-arm32.patch"

"$CMAKE" -S "$TMP/src" -B "$TMP/build" \
    -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI=armeabi-v7a -DANDROID_PLATFORM=android-23 \
    -DCMAKE_BUILD_TYPE=Release -DDOBBY_DEBUG=OFF \
    -DNearBranch=ON -DPlugin.SymbolResolver=ON \
    -DPlugin.ImportTableReplace=OFF -DPlugin.Android.BionicLinkerUtil=OFF \
    -DDOBBY_BUILD_EXAMPLE=OFF -DDOBBY_BUILD_TEST=OFF
"$CMAKE" --build "$TMP/build" --target dobby_static --parallel 4

OUTPUT="$ROOT/jni/lib/armeabi-v7a/libdobby.a"
mkdir -p "$(dirname "$OUTPUT")"
install -m 644 "$TMP/build/libdobby.a" "$OUTPUT"
printf 'Dobby %s ARM32 pronto: ' "$REV"
sha256sum "$OUTPUT"
