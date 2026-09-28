#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
ID="abi-test-$BASHPID"
MOD_DIR="$ROOT/mods/$ID"
[ ! -e "$MOD_DIR" ] || { echo "fixture temporária já existe: $MOD_DIR" >&2; exit 1; }
mkdir -p "$MOD_DIR/libs/arm64-v8a" "$MOD_DIR/libs/armeabi-v7a"
trap 'rm -rf "$MOD_DIR"' EXIT
cat >"$MOD_DIR/manifest.json" <<EOF
{"format":1,"id":"$ID","name":"ABI Test","version":"1.0","engine":"native","type":"native","game":"*"}
EOF
python3 - "$MOD_DIR" <<'PY'
from pathlib import Path
import struct, sys

root = Path(sys.argv[1])
for abi, elf_class, machine in (("arm64-v8a", 2, 183), ("armeabi-v7a", 1, 40)):
    header = bytearray(20)
    header[:7] = b"\x7fELF" + bytes((elf_class, 1, 1))
    struct.pack_into("<H", header, 18, machine)
    (root / "libs" / abi / f"lib{root.name}.so").write_bytes(header)
PY

check_payload() {
    local abi=$1
    python3 - "$MOD_DIR/$ID.bmod" "$abi" <<'PY'
from zipfile import ZipFile
import sys

path, abi = sys.argv[1:]
want = (2, 183) if abi == "arm64-v8a" else (1, 40)
with ZipFile(path) as zf:
    payload = zf.read("mod.so")
if payload[:4] != b"\x7fELF" or (payload[4], int.from_bytes(payload[18:20], "little")) != want:
    raise SystemExit(f"payload de ABI errada: esperado {abi}")
print(f"pack_bmod {abi}: mod.so corresponde à ABI")
PY
}

"$ROOT/tools/pack_bmod.sh" "$ID" arm64-v8a
check_payload arm64-v8a
"$ROOT/tools/pack_bmod.sh" "$ID" armeabi-v7a
check_payload armeabi-v7a
if "$ROOT/tools/pack_bmod.sh" "$ID" > /dev/null 2>&1; then
    echo "pack_bmod aceitou native sem ABI explícita" >&2
    exit 1
fi
echo "pack_bmod sem ABI: recusado"

abi=$(printf 'primaryCpuAbi=armeabi-v7a secondaryCpuAbi=arm64-v8a\n' \
    | "$ROOT/tools/parse_primary_abi.sh")
[ "$abi" = armeabi-v7a ]
echo "dumpsys armeabi-v7a: selecionado"
abi=$(printf 'primaryCpuAbi=arm64-v8a secondaryCpuAbi=armeabi-v7a\n' \
    | "$ROOT/tools/parse_primary_abi.sh")
[ "$abi" = arm64-v8a ]
echo "dumpsys arm64-v8a: selecionado"
if printf 'primaryCpuAbi=null\n' | "$ROOT/tools/parse_primary_abi.sh" >/dev/null 2>&1; then
    echo "ABI desconhecida foi aceita" >&2
    exit 1
fi
echo "dumpsys ABI desconhecida: recusada"
