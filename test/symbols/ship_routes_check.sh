#!/usr/bin/env bash
# test/symbols/ship_routes_check.sh — nenhuma rota entrega .so NÃO-stripado.
#
# Com APP_STRIP_MODE := none (jni/repro.mk) o .so do diretório de build tem
# ~1,8 MB de DWARF. Todo .so que sai da máquina (adb push, cp pra /data,
# asset de APK) tem que passar pelo symbols_ship; rotas manuais nos docs
# apontam para o artefato stripado, nunca para libs/ ou obj/.
#
# O que esta etapa pega (texto, nos arquivos versionados *.md/*.sh/*.py):
#   1. `adb push` com origem literal em libs/ ou obj/ terminada em .so;
#   2. `cp` com origem literal em libs/ ou obj/ terminada em .so e destino
#      em /data/ (device);
#   3. `adb push "$VAR"` onde VAR foi atribuída de um caminho libs//obj/
#      .so, sem symbols_ship no mesmo arquivo (a indireção por variável).
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
fail=0

while IFS= read -r line; do
    [ -n "$line" ] || continue
    echo "  ROTA NÃO-STRIPADA: $line"
    fail=1
done < <(git -C "$ROOT" grep -n -E 'adb push [^ ]*(libs/|/obj/)[^ ]*\.so' \
    -- '*.md' '*.sh' '*.py' || true)

while IFS= read -r line; do
    [ -n "$line" ] || continue
    echo "  ROTA NÃO-STRIPADA: $line"
    fail=1
done < <(git -C "$ROOT" grep -n -E 'cp [^ ]*(libs/|/obj/)[^ ]*\.so [^ ]*/data/' \
    -- '*.md' '*.sh' '*.py' || true)

while IFS= read -r file; do
    [ -n "$file" ] || continue
    if grep -Eq '^[A-Za-z_]+=".*(libs/|/obj/).*\.so"' "$ROOT/$file" \
        && grep -Eq 'adb push "\$[A-Za-z_]+"' "$ROOT/$file" \
        && ! grep -q 'symbols_ship' "$ROOT/$file"; then
        echo "  ROTA NÃO-STRIPADA (variável sem symbols_ship): $file"
        fail=1
    fi
done < <(git -C "$ROOT" grep -l -E 'adb push "\$[A-Za-z_]+"' -- '*.sh' || true)

if [ "$fail" -ne 0 ]; then
    echo "ship-routes: HÁ ROTAS ENTREGANDO .so NÃO-STRIPADO (veja acima)" >&2
    exit 1
fi
echo "ship-routes: OK (nenhuma rota entrega .so de libs/ ou obj/ sem strip)"
