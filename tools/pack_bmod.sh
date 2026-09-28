#!/bin/bash
# pack_bmod.sh <id> [arm64-v8a|armeabi-v7a] — gera mods/<id>/<id>.bmod (C2).
# manifest.json + mod.so (type=native) ou mod.bpatch (type=patch).
# Valida o manifest antes de fechar o zip (format 1, id, engine, type, game).
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$PWD"

[ $# -ge 1 ] && [ $# -le 2 ] || { echo "uso: $0 <id> [arm64-v8a|armeabi-v7a]" >&2; exit 2; }
id=$1
abi=${2:-}
[[ "$id" =~ ^[a-z0-9-]{3,48}$ ]] || { echo "id inválido: '$id' ([a-z0-9-]{3,48})" >&2; exit 2; }
if [ -n "$abi" ] && [ "$abi" != arm64-v8a ] && [ "$abi" != armeabi-v7a ]; then
    echo "ABI inválida: '$abi' (use arm64-v8a ou armeabi-v7a)" >&2
    exit 2
fi
dir="mods/$id"
[ -f "$dir/manifest.json" ] || { echo "$dir/manifest.json ausente" >&2; exit 1; }

# O .so que entra no .bmod tem que ser STRIPPED. O ndk-build agora sai
# não-stripado (jni/repro.mk, para o release ter símbolo de crash), e o .bmod
# é o arquivo que o usuário baixa e compartilha: mandar o .so de 1,8MB de
# símbolo aí seria Gift-Wrapping the ELF. symbols_ship também confere que o
# build-id sobreviveu ao strip — é o que liga um crash do device aos símbolos
# guardados em symbols/.
# REGRA DE ORDEM: a validação ELF/ABI vem ANTES do strip. Entrada inválida
# (não-ELF, ELF de outra ABI) morre aqui com mensagem amigável; se chegasse
# ao strip, o llvm falharia primeiro ("invalid buffer...") sem dizer o que
# está errado no .so.
# shellcheck source=tools/symbols.sh
# shellcheck disable=SC1091
. "$ROOT/tools/symbols.sh"
stage="$dir/.bmod-stage"
rm -rf "$stage"; mkdir -p "$stage"

# Validacao ELF/ABI ANTES do strip: o test/abi_packaging_test.sh exige a
# mensagem amigavel para .so inválido e PROIBE o erro do llvm-strip
# ("invalid buffer"). Os stubs válidos do teste são ELFs mínimos de verdade,
# que o strip aceita e encolhe.
python3 - "$dir/manifest.json" "$id" "$dir" "$abi" <<'PY'
import json, os, sys

man_path, mod_id, mod_dir, abi = sys.argv[1:5]

def die_manifest(msg):
    print(f"manifest inválido: {msg}", file=sys.stderr)
    sys.exit(1)

def die(msg):
    print(f"mod.so inválido: {msg}", file=sys.stderr)
    sys.exit(1)

try:
    man = json.load(open(man_path, encoding="utf-8"))
except (OSError, json.JSONDecodeError) as e:
    die_manifest(str(e))

if man.get("format") != 1: die_manifest("format deve ser 1")
if man.get("id") != mod_id: die_manifest(f"id '{man.get('id')}' != diretório '{mod_id}'")
if not man.get("name"): die_manifest("name ausente")
if not man.get("version"): die_manifest("version ausente")
if man.get("engine") not in ("unity-il2cpp", "unity-mono", "cocos2dx", "native"):
    die_manifest("engine inválida (unity-il2cpp|unity-mono|cocos2dx|native)")
if man.get("type") not in ("native", "patch"): die_manifest("type inválido (native|patch)")
if not man.get("game"): die_manifest("game ausente")

if man["type"] == "native":
    if abi not in ("arm64-v8a", "armeabi-v7a"):
        die("mod nativo exige ABI explícita: passe arm64-v8a ou armeabi-v7a")
    src = f"{mod_dir}/libs/{abi}/lib{mod_id}.so"
    try:
        with open(src, "rb") as so:
            header = so.read(20)
    except OSError as e:
        die(str(e))
    expected = (2, 183) if abi == "arm64-v8a" else (1, 40)
    machine = int.from_bytes(header[18:20], "little") if len(header) >= 20 else -1
    if (len(header) < 20 or header[:4] != b"\x7fELF"
            or header[4] != expected[0] or header[5] != 1 or machine != expected[1]):
        die(f"{src} não é um ELF da ABI selecionada ({abi})")
else:
    src = f"{mod_dir}/{mod_id}.bpatch"
if not os.path.isfile(src): die(f"artefato ausente: {src} (build primeiro)")
PY

# Strip DEPOIS da validacao: o .so do ndk-build sai nao-stripado (repro.mk)
# e o .bmod e o arquivo que o usuario baixa. symbols_ship confere que o
# build-id sobreviveu ao strip.
if [ "$abi" != "" ] && [ -f "$dir/libs/$abi/lib$id.so" ]; then
    symbols_ship "$dir/libs/$abi/lib$id.so" "$stage/mod.so" || exit 1
    src="$stage/mod.so"
    arc="mod.so"
else
    src="$dir/$id.bpatch"
    arc="mod.bpatch"
fi

python3 - "$dir/manifest.json" "$id" "$dir" "$src" "$arc" <<'PY'
import json, os, sys, zipfile

man_path, mod_id, mod_dir, src, arc = sys.argv[1:6]

out = f"{mod_dir}/{mod_id}.bmod"
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    z.write(man_path, "manifest.json")
    z.write(src, arc)
print(out)
PY
rm -rf "$stage"
