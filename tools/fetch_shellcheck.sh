#!/usr/bin/env bash
# ShellCheck release source: https://github.com/koalaman/shellcheck/releases
# Pinned v0.11.0 linux.x86_64 asset; SHA-256 verified before extraction.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
VERSION=v0.11.0
ARCHIVE="shellcheck-${VERSION}.linux.x86_64.tar.xz"
SHA256=8c3be12b05d5c177a04c29e3c78ce89ac86f1595681cab149b65b97c4e227198
URL="https://github.com/koalaman/shellcheck/releases/download/${VERSION}/${ARCHIVE}"
CACHE="$ROOT/out/shellcheck/$VERSION"
BIN="$CACHE/shellcheck-${VERSION}/shellcheck"

mkdir -p "$CACHE"
if [ ! -x "$BIN" ]; then
    archive="$CACHE/$ARCHIVE"
    if [ ! -f "$archive" ]; then
        curl --fail --location --silent --show-error "$URL" -o "$archive"
    fi
    echo "$SHA256  $archive" | sha256sum --check --status
    tar -xJf "$archive" -C "$CACHE"
fi
[ -x "$BIN" ] || { echo "ShellCheck binary missing after extraction" >&2; exit 1; }
printf '%s\n' "$BIN"
