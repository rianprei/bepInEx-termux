#!/bin/bash
# pack_bmod.sh <id> — gera mods/<id>/<id>.bmod, o zip distribuível (C2):
# manifest.json + mod.so (type=native) ou mod.bpatch (type=patch).
# Valida o manifest antes de fechar o zip (format 1, id, engine, type, game).
set -euo pipefail
cd "$(dirname "$0")/.."

[ $# -eq 1 ] || { echo "uso: $0 <id>" >&2; exit 2; }
id=$1
[[ "$id" =~ ^[a-z0-9-]{3,48}$ ]] || { echo "id inválido: '$id' ([a-z0-9-]{3,48})" >&2; exit 2; }
dir="mods/$id"
[ -f "$dir/manifest.json" ] || { echo "$dir/manifest.json ausente" >&2; exit 1; }

python3 - "$dir/manifest.json" "$id" "$dir" <<'PY'
import json, os, sys, zipfile

man_path, mod_id, mod_dir = sys.argv[1:4]

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
    src, arc = f"{mod_dir}/libs/arm64-v8a/lib{mod_id}.so", "mod.so"
else:
    src, arc = f"{mod_dir}/{mod_id}.bpatch", "mod.bpatch"
if not os.path.isfile(src): die(f"artefato ausente: {src} (build primeiro)")

out = f"{mod_dir}/{mod_id}.bmod"
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    z.write(man_path, "manifest.json")
    z.write(src, arc)
print(out)
PY
