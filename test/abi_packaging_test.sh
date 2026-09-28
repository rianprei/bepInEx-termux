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

def minimal_elf64(machine):
    # EHDR(64) + 1 PHDR(56) + 80 bytes de enchimento: ELF valido que o
    # llvm-strip aceita (um stub de 20 bytes nao e um ELF completo e o
    # strip falha com "smaller than an ELF header"). Os 20 primeiros bytes
    # seguem o layout que o check_payload confere abaixo.
    e = bytearray(200)
    e[:7] = b"\x7fELF" + bytes((2, 1, 1))
    struct.pack_into("<H", e, 16, 3)
    struct.pack_into("<H", e, 18, machine)
    struct.pack_into("<I", e, 20, 1)
    struct.pack_into("<Q", e, 32, 64)
    struct.pack_into("<H", e, 52, 64)
    struct.pack_into("<H", e, 54, 56)
    struct.pack_into("<H", e, 56, 1)
    struct.pack_into("<H", e, 58, 64)
    struct.pack_into("<I", e, 64, 1)
    return e

def minimal_elf32(machine):
    e = bytearray(164)
    e[:7] = b"\x7fELF" + bytes((1, 1, 1))
    struct.pack_into("<H", e, 16, 3)
    struct.pack_into("<H", e, 18, machine)
    struct.pack_into("<I", e, 20, 1)
    struct.pack_into("<I", e, 28, 52)
    struct.pack_into("<H", e, 40, 52)
    struct.pack_into("<H", e, 42, 32)
    struct.pack_into("<H", e, 44, 1)
    struct.pack_into("<H", e, 46, 40)
    struct.pack_into("<I", e, 52, 1)
    return e

for abi, elf_class, machine in (("arm64-v8a", 2, 183), ("armeabi-v7a", 1, 40)):
    blob = minimal_elf64(machine) if elf_class == 2 else minimal_elf32(machine)
    (root / "libs" / abi / f"lib{root.name}.so").write_bytes(blob)
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

# .so inválido tem que morrer na validação com mensagem amigável, nunca no
# strip (que diria "invalid buffer" sem explicar o que está errado).
check_rejeitado() {
    local motivo=$1
    local so_out
    if so_out=$("$ROOT/tools/pack_bmod.sh" "$ID" arm64-v8a 2>&1); then
        echo "pack_bmod aceitou $motivo" >&2
        exit 1
    fi
    printf '%s\n' "$so_out" | grep -Fq "não é um ELF da ABI selecionada" || {
        echo "sem mensagem amigável para $motivo" >&2
        printf '%s\n' "$so_out" >&2
        exit 1
    }
    if printf '%s\n' "$so_out" | grep -Fq "invalid buffer"; then
        echo "strip rodou antes da validação ($motivo)" >&2
        exit 1
    fi
    echo "$motivo: recusado com mensagem amigável"
}

# Caso 20 bytes não-ELF.
head -c 20 /dev/zero > "$MOD_DIR/libs/arm64-v8a/lib$ID.so"
check_rejeitado "20 bytes não-ELF"

# Caso ELF de ABI errada (ELF32/ARM onde se pediu arm64-v8a).
python3 - "$MOD_DIR" <<'PY'
from pathlib import Path
import struct, sys

root = Path(sys.argv[1])
e32 = bytearray(164)
e32[:7] = b"\x7fELF" + bytes((1, 1, 1))
struct.pack_into("<H", e32, 16, 3)
struct.pack_into("<H", e32, 18, 40)
struct.pack_into("<I", e32, 20, 1)
struct.pack_into("<I", e32, 28, 52)
struct.pack_into("<H", e32, 40, 52)
struct.pack_into("<H", e32, 42, 32)
struct.pack_into("<H", e32, 44, 1)
struct.pack_into("<H", e32, 46, 40)
struct.pack_into("<I", e32, 52, 1)
(root / "libs" / "arm64-v8a" / f"lib{root.name}.so").write_bytes(e32)
PY
check_rejeitado "ELF de ABI errada"

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
