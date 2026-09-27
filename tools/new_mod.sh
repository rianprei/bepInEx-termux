#!/bin/bash
# new_mod.sh <id> — cria mods/<id>/ a partir de mods/_template (F2).
# id: [a-z0-9-]{3,48} (contrato C2 — vira nome de arquivo no device).
set -euo pipefail
cd "$(dirname "$0")/.."

[ $# -eq 1 ] || { echo "uso: $0 <id>" >&2; exit 2; }
id=$1
[[ "$id" =~ ^[a-z0-9-]{3,48}$ ]] || { echo "id inválido: '$id' (use [a-z0-9-]{3,48})" >&2; exit 2; }
[ -e "mods/$id" ] && { echo "mods/$id já existe" >&2; exit 1; }

cp -r mods/_template "mods/$id"
sed -i "s/__MOD_ID__/$id/g" \
    "mods/$id/jni/mod.cpp" "mods/$id/jni/Android.mk" "mods/$id/manifest.json"

echo "criado: mods/$id"
echo "build + deploy:   tools/deploy_mod.sh $id <pkg> (detecta ABI instalada)"
echo "pacote .bmod:     tools/pack_bmod.sh $id <ABI do jogo>"
