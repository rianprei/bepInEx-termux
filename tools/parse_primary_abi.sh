#!/usr/bin/env sh
set -eu

abi=$(sed -n 's/.*primaryCpuAbi=\([^[:space:]]*\).*/\1/p' | head -n 1)
case "$abi" in
    arm64-v8a|armeabi-v7a) printf '%s\n' "$abi" ;;
    *) echo "ABI ARM instalada não suportada ou desconhecida: ${abi:-ausente}" >&2; exit 1 ;;
esac
