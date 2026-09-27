#!/bin/bash
# pack_bmod.sh <id> — gera mods/<id>/<id>.bmod, o zip distribuível (C2):
# manifest.json + mod.so (type=native) ou mod.patch (type=patch).
# Valida o manifest antes de fechar o zip (format 1, id, engine, type, game).
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$PWD"

[ $# -eq 1 ] || { echo "uso: $0 <id>" >&2; exit 2; }
id=$1
[[ "$id" =~ ^[a-z0-9-]{3,48}$ ]] || { echo "id inválido: '$id' ([a-z0-9-]{3,48})" >&2; exit 2; }
dir="mods/$id"
[ -f "$dir/manifest.json" ] || { echo "$dir/manifest.json ausente" >&2; exit 1; }

# O .so que entra no .bmod tem que ser STRIPPED. O ndk-build agora sai
# não-stripado (jni/repro.mk, para o release ter símbolo de crash), e o .bmod
# é o arquivo que o usuário baixa e compartilha: mandar o .so de 1,8MB de
# símbolo aí seria Gift-Wrapping the ELF. symbols_ship também confere que o
# build-id sobreviveu ao strip — é o que liga um crash do device aos símbolos
# guardados em symbols/.
. "$ROOT/tools/symbols.sh"
stage="$dir/.bmod-stage"
rm -rf "$stage"; mkdir -p "$stage"
if [ -f "$dir/libs/arm64-v8a/lib$id.so" ]; then
    symbols_ship "$dir/libs/arm64-v8a/lib$id.so" "$stage/mod.so" || exit 1
fi

python3 - "$dir/manifest.json" "$id" "$dir" "$stage" <<'PY'
import json, os, sys, zipfile

man_path, mod_id, mod_dir, stage = sys.argv[1:5]

def die(msg):
    print(f"manifest inválido: {msg}", file=sys.stderr)
    sys.exit(1)

try:
    man = json.load(open(man_path, encoding="utf-8"))
except (OSError, json.JSONDecodeError) as e:
    die(str(e))

if man.get("format") != 1: die("format deve ser 1")
if man.get("id") != mod_id: die(f"id '{man.get('id')}' != diretório '{mod_id}'")
if not man.get("name"): die("name ausente")
if not man.get("version"): die("version ausente")
if man.get("engine") not in ("unity-il2cpp", "unity-mono", "cocos2dx", "native"):
    die("engine inválida (unity-il2cpp|unity-mono|cocos2dx|native)")
if man.get("type") not in ("native", "patch"): die("type inválido (native|patch)")
if not man.get("game"): die("game ausente")

if man["type"] == "native":
    # Já stripado pelo symbols_ship acima.
    src, arc = f"{stage}/mod.so", "mod.so"
else:
    src, arc = f"{mod_dir}/{mod_id}.patch", "mod.patch"
if not os.path.isfile(src): die(f"artefato ausente: {src} (build primeiro)")

out = f"{mod_dir}/{mod_id}.bmod"
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    z.write(man_path, "manifest.json")
    z.write(src, arc)
print(out)
PY
rm -rf "$stage"
