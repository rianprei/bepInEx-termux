#!/usr/bin/env bash
# Procura, em $SYMDIR, o build-id de um .so pelo NOME do arquivo. Script
# separado (e não função) porque o AWK do symbolize.sh precisa chamá-lo por
# dentro de $( ), onde um `source` não existe.
#
# Tombstone de Android 8- e anteriores não escreve BuildId no backtrace, e sem
# esta busca o crash de um aparelho antigo não cruza com os símbolos guardados.
set -euo pipefail
name="$(basename "${1:-}" .so)"
[ -n "$name" ] || exit 0
found=0
while IFS=$'\t' read -r bid nm _; do
    [ "$nm" = "$name" ] || continue
    # Nome repetido em builds diferentes: só o primeiro (INDEX é ordenado por
    # build-id) — e o symbolize.sh avisa quando há ambiguidade.
    [ "$found" = 0 ] || continue
    found=1
    printf '%s' "$bid"
done < <(sort -u "${SYMDIR:?}/INDEX" 2>/dev/null || true)
exit 0
