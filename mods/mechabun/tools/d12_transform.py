#!/usr/bin/env python3
# D12 -- gera copia do ImageDataServer_100600_00_en.pack com a janela
# cifrada de 426_s01.maanim (True Form attack, ver mechabun_mod.cpp) com
# keyframes escalados 26/32 (ciclo 32f->26f pedido pela comunidade).
# Nunca escreve no pack original (BASE/*.pack) -- so le dele e produz uma
# copia em OUT_PACK. Historico/evidencia da escolha 426_s01 (vs 425_f01 e
# 426_f01, ambos descartados) em context/battlecats-d12-backswing-pesquisa.md.
import os
import sys
from Crypto.Cipher import AES

LIST_KEY = b"b484857901742afc"
PACK_KEY = b"89a0f99078419c28"

# Overridable via env (repo é público; o default abaixo é a máquina do autor).
# Requer pycryptodome no host: pip install pycryptodome.
BASE = os.environ.get(
    "BCDATA_DIR", "/home/rianprei/battlecats-mods/BCData/en_server"
)
LIST_PATH = f"{BASE}/ImageDataServer_100600_00_en.list"
PACK_PATH = f"{BASE}/ImageDataServer_100600_00_en.pack"
TARGET_ENTRY = "426_s01.maanim"


def unpad(b: bytes) -> bytes:
    n = b[-1]
    if n < 1 or n > 16:
        raise ValueError(f"bad pad byte {n}")
    return b[:-n]


def pad_to(plain: bytes, total_len: int) -> bytes:
    pad_len = total_len - len(plain)
    if pad_len < 1 or pad_len > 16:
        raise ValueError(f"cannot pad {len(plain)} bytes to {total_len} (pad_len={pad_len})")
    return plain + bytes([pad_len]) * pad_len


def aes_ecb_decrypt(key, data):
    return AES.new(key, AES.MODE_ECB).decrypt(data)


def aes_ecb_encrypt(key, data):
    return AES.new(key, AES.MODE_ECB).encrypt(data)


def find_entry(name: str):
    with open(LIST_PATH, "rb") as f:
        list_ct = f.read()
    list_text = unpad(aes_ecb_decrypt(LIST_KEY, list_ct)).decode("utf-8", errors="replace")
    for line in list_text.splitlines():
        if line.startswith(name + ","):
            _, offset, size = line.strip().split(",")
            return int(offset), int(size)
    raise ValueError(f"{name} not found in list")


def scale_frames(text: str, ratio_num: int, ratio_den: int) -> str:
    lines = text.split("\n")
    out = lines[:3]
    block_count = int(lines[2])
    i = 3
    for _ in range(block_count):
        out.append(lines[i]); i += 1  # header
        kf_count = int(lines[i]); out.append(lines[i]); i += 1
        for _ in range(kf_count):
            fields = lines[i].split(",")
            # achado de review: round() do Python usa banker's rounding
            # (arredonda .5 pro par mais proximo, nao sempre pra cima) --
            # com ratio 26/32, ties acontecem em frames == 4 (mod 16) e
            # arredondam pra baixo nesse caso. Aceito deliberadamente (erro
            # de <=1 frame, imperceptivel em animacao) -- documentado aqui
            # pra nao ser confundido com bug se alguem notar o padrao.
            fields[0] = str(round(int(fields[0]) * ratio_num / ratio_den))
            out.append(",".join(fields))
            i += 1
    out.extend(lines[i:])
    return "\n".join(out)


def build_patched_pack(out_path: str) -> int:
    offset, size = find_entry(TARGET_ENTRY)

    with open(PACK_PATH, "rb") as f:
        pack_data = bytearray(f.read())

    ct_window = bytes(pack_data[offset:offset + size])
    plain_text = unpad(aes_ecb_decrypt(PACK_KEY, ct_window)).decode("utf-8")

    new_text = scale_frames(plain_text, 26, 32)
    new_plain = new_text.encode("utf-8")

    # byte-window constraint: ciphertext size must stay == original size,
    # so plaintext must land in [size-16, size-1] before PKCS7 padding.
    # Shrunk digit widths can push us under that floor -- pad the tail
    # with inert blank lines (parser reads counted blocks only).
    required_min = size - 16
    if len(new_plain) < required_min:
        new_text += "\n" * (required_min - len(new_plain))
        new_plain = new_text.encode("utf-8")

    new_ct = aes_ecb_encrypt(PACK_KEY, pad_to(new_plain, size))
    # achado de review: assert e' removido inteiro com `python -O` -- essas
    # 2 checagens sao a unica garantia de que o pack gerado nao esta
    # corrompido antes de ir pro device (defesa em profundidade: o C++
    # (MECHABUN_D12_PACK_EXPECTED_SIZE) confere de novo, mas nao deveria
    # ser a UNICA linha de defesa). if/raise explicito nao depende de flag.
    if len(new_ct) != size:
        raise ValueError(f"tamanho do ciphertext mudou: {len(new_ct)} != {size}")

    # round-trip check before trusting the patch
    if unpad(aes_ecb_decrypt(PACK_KEY, new_ct)).decode("utf-8") != new_text:
        raise ValueError("round-trip check falhou: decrypt(encrypt(x)) != x")

    pack_data[offset:offset + size] = new_ct
    with open(out_path, "wb") as f:
        f.write(pack_data)
    return len(pack_data)


if __name__ == "__main__":
    out_path = sys.argv[1] if len(sys.argv) > 1 else "/tmp/ImageDataServer_100600_00_en.pack.d12"
    total = build_patched_pack(out_path)
    print(f"wrote {out_path} ({total} bytes)")
