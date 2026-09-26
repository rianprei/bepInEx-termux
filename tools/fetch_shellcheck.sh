#!/usr/bin/env bash
# ShellCheck release source: https://github.com/koalaman/shellcheck/releases
# Pinned v0.11.0 linux.x86_64 asset; SHA-256 verified before extraction.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
IFS='|' read -r _ VERSION SHA256 URL _ < <(grep '^shellcheck|' "$ROOT/tools/deps.lock")
ARCHIVE="shellcheck-${VERSION}.linux.x86_64.tar.xz"
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
