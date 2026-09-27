#!/usr/bin/env bash
set -euo pipefail

ZIP=${1:?usage: module_zip_abi_check.sh <module.zip>}
python3 - "$ZIP" <<'PY'
from zipfile import ZipFile
import struct
import sys

expected = {
    "zygisk/arm64-v8a.so": (2, 183),
    "zygisk/armeabi-v7a.so": (1, 40),
}
with ZipFile(sys.argv[1]) as archive:
    for name, (elf_class, machine) in expected.items():
        try:
            header = archive.read(name)[:20]
        except KeyError:
            raise SystemExit(f"ABI ausente no módulo: {name}")
        got_machine = struct.unpack_from("<H", header, 18)[0] if len(header) >= 20 else -1
        if (header[:4] != b"\x7fELF" or len(header) < 20
                or header[4] != elf_class or header[5] != 1 or got_machine != machine):
            raise SystemExit(f"ELF/ABI incorreto em {name}")
        print(f"{name}: ELF{elf_class * 32}, e_machine={machine}")
PY
