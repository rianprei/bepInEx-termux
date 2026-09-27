#!/usr/bin/env python3
"""Lê o método e o offset de DADOS de uma entrada de um zip, pelo cabeçalho LOCAL.

Por que existe: o check de "APK instalável" precisa saber onde começam os
dados de `resources.arsc` dentro do APK. Quem decide isso é o PackageManager,
que lê o cabeçalho LOCAL (o zipalign preenche o campo extra local, e é ele
que alinha a entrada). Ler o diretório central ou os bytes errados do local dá
offset diferente do que o zipalign reporta — e o check acusava "não alinhado"
num APK alinhado (FAIL falso em 2026-09-27, achado do OpenCode no device).

Cabeçalho local (spec PKWARE, 30 bytes fixos):
   0..3   assinatura PK\\x03\\x04
   4..5   versão necessária        <- NÃO é daqui que sai o offset
   6..7   flags gerais
   8..9   método de compressão
  10..13  hora/data
  14..17  crc32
  18..21  tamanho comprimido
  22..25  tamanho descomprimido
  26..27  name_len   <-- com extra_len, é daqui
  28..29  extra_len
  30..    nome, depois o campo extra, e então os dados

Uso: zip_arsc_info.py <apk> [entrada]  ->  "method offset name_len extra_len"
 Sai != 0 se a entrada não existir ou o arquivo não for zip.
"""
import struct
import sys
import zipfile

LOCAL_NAME_OFF = 26
LOCAL_EXTRA_OFF = 28
LOCAL_DATA_BASE = 30


def arsc_info(apk_path, entry="resources.arsc"):
    with zipfile.ZipFile(apk_path) as z:
        info = z.getinfo(entry)
        method = info.compress_type  # 0 = STORED
        with open(apk_path, "rb") as f:
            f.seek(info.header_offset)
            if f.read(4) != b"PK\x03\x04":
                raise ValueError("cabeçalho local não encontrado em %s" % entry)
            # name_len/extra_len do LOCAL (o que o PackageManager usa).
            f.seek(info.header_offset + LOCAL_NAME_OFF)
            name_len, extra_len = struct.unpack("<HH", f.read(4))
            offset = info.header_offset + LOCAL_DATA_BASE + name_len + extra_len
    return method, offset, name_len, extra_len


def main():
    if len(sys.argv) < 2:
        print("uso: %s <apk> [entrada]" % sys.argv[0], file=sys.stderr)
        return 2
    try:
        method, offset, name_len, extra_len = arsc_info(sys.argv[1],
                                                        sys.argv[2] if len(sys.argv) > 2 else "resources.arsc")
    except (KeyError, zipfile.BadZipFile, ValueError) as e:
        print("erro: %s" % e, file=sys.stderr)
        return 1
    print("%d %d %d %d" % (method, offset, name_len, extra_len))
    return 0


if __name__ == "__main__":
    sys.exit(main())
