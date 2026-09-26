import struct, sys
# Sobe p_align do PT_TLS pra 64 (workaround do bug do lld<13 no arm64:
# bionic exige TLS alinhado a 64; lld 12 emite p_align=8).
path = sys.argv[1]
with open(path, 'r+b') as f:
    d = f.read(64)
    assert d[:4] == b'\x7fELF' and d[4] == 2, 'nao e ELF64'
    is_le = d[5] == 1
    e_phoff = struct.unpack_from('<Q', d, 0x20)[0]
    e_phentsize = struct.unpack_from('<H', d, 0x36)[0]
    e_phnum = struct.unpack_from('<H', d, 0x38)[0]
    f.seek(e_phoff)
    n = 0
    for i in range(e_phnum):
        ph = f.read(e_phentsize)
        p_type = struct.unpack_from('<I', ph, 0)[0]
        if p_type == 7:  # PT_TLS
            f.seek(e_phoff + i * e_phentsize + 48)
            f.write(struct.pack('<Q', 64))
            n += 1
    print(f'PT_TLS ajustado: {n}')
