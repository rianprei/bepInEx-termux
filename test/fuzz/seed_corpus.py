#!/usr/bin/env python3
"""Gera os corpus-semente dos alvos de fuzz a partir das fixtures REAIS.

Fonte de cada semente (nada inventado):
  c4_line        -> test/fixtures/c4_lines.tsv (o contrato C4 compartilhado
                    com o Manager) + os formatos aceitos pelo verbo field
  elf_preflight  -> um ELF sintético com DT_SONAME (o caminho que aciona a
                    guarda do frida-gadget), as arestas de recusa que o
                    Caso 63 do selftest lista, e — quando o ndk-build já
                    rodou — os .so REAIS que o jogo carrega
  frida_config   -> o JSON que uf_build_config() monta de verdade + os
                    configs que o Caso 61 do selftest considera válidos e
                    recusados
  selmix         -> um .conf de mods real, uma allowlist real, valores de
                    property e assinaturas de prólogo reais

Duas saídas, e a diferença importa para o gate:
  test/fuzz/corpus/<alvo>/        ESTÁVEL — sem artefato de build, é o que
                                   entra no git e o que a etapa determinística
                                   do verify_all replaya
  test/fuzz/corpus-local/<alvo>/  SÓ LOCAL — sementes derivadas dos .so que o
                                   ndk-build produz (libs/ é gitignored, então
                                   elas não podem entrar no gate). É o corpus
                                   das rodadas longas de 10 min.

Idempotente: roda quantas vezes quiser, sempre o mesmo conjunto (sem
timestamps nem random) — o gate depende disso ser determinístico.
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
CORPUS = os.path.join(HERE, "corpus")
CORPUS_LOCAL = os.path.join(HERE, "corpus-local")


def put(target, name, data, local=False):
    base = CORPUS_LOCAL if local else CORPUS
    d = os.path.join(base, target)
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, name), "wb") as f:
        f.write(data)


def read(path):
    with open(path, "rb") as f:
        return f.read()


def read_opt(path):
    try:
        return read(path)
    except OSError:
        return None


# ---------------------------------------------------------------- c4_line ---
def seed_c4():
    tsv = read(os.path.join(ROOT, "test", "fixtures", "c4_lines.tsv"))
    # Uma linha por arquivo de corpus: o fuzzer precisa de UMA entrada = UMA
    # linha C4 (é assim que o mod entrega), mas também do arquivo inteiro.
    for i, raw in enumerate(tsv.split(b"\n")):
        line = raw.rstrip(b"\r")
        if not line.strip() or line.lstrip().startswith(b"#"):
            continue
        payload = line.split(b"\t", 1)[0].rstrip()
        if payload:
            put("c4_line", "fixture_%02d" % i, payload + b"\n")
    put("c4_line", "fixture_whole_file", tsv)
    # Arestas de tokenização/recusa que a fixture NÃO tem (o contrato C4
    # aceita/rejeita não cobre limites de buffer nem comentário no meio).
    edges = [
        b"\n",
        b"#\n",
        b"   \t  \n",
        b"return\n",
        b"return A\n",
        b"return A B\n",
        b"return A B 0\n",
        b"return A B 0 int\n",
        b"return A B 0 int 1\n",
        b"return A B 0 int 1 extra\n",
        b"return  .  0 int 1\n",
        b"return . 0 int 1\n",
        b"return A. 0 int 1\n",
        b"return .B 0 int 1\n",
        b"return A.B.C.D 0 int 1\n",
        b"return A B -0 int 1\n",
        b"return A B 00 int 1\n",
        b"return A B 007 int 1\n",
        b"return A B 64 int 1\n",
        b"return A B 65 int 1\n",
        b"return A B 99999999999999999999 int 1\n",
        b"return A B - int 1\n",
        b"return A B --1 int 1\n",
        b"return A B 0 bool true#comentario\n",
        b"return A B 0 int 1 #comentario\n",
        b"#return A B 0 int 1\n",
        b"mul A B 0 int 1\n",
        b"mul A B 0 float 1.5\n",
        b"mul A B 0 bool 1\n",
        b"static A B bool true\n",
        b"static A B int 1\n",
        b"field A B bool true\n",
        b"field A B bool true M 0\n",
        b"field A B bool true M -1\n",
        b"field A B bool true M 65\n",
        b"field A B bool true M 0 1\n",
        b"return " + b"A" * 300 + b" B 0 int 1\n",
        b"return A " + b"B" * 300 + b" 0 int 1\n",
        b"return A B 0 int " + b"9" * 200 + b"\n",
        b"return\tA\tB\t0\tint\t1\n",
        b"return\rA B 0 int 1\r\n",
        b"return A B 0 int 1",
        b"\n\n\n\n\n\n\n\n",
        b"return A B 0 int 1\n" * 64,
        # C3: opções key=value
        b"appInit=on\n",
        b"appInit = on \n",
        b"appInit=\n",
        b"=on\n",
        b"#appInit=on\n",
        b"on=appInit\n",
        b"onion=appInit\n",
        b"on =appInit\n",
        b"on=\n",
        b"on=" + b"x" * 500 + b"\n",
        b"throttle_every=30\n",
        b"stream_source=companion\n",
    ]
    for i, e in enumerate(edges):
        put("c4_line", "edge_%02d" % i, e)


# ----------------------------------------------------------- elf_preflight ---
# sh_type do ELF64 (elf.h). Nomeado porque 2 = SHT_SYMTAB e 11 = SHT_DYNSYM:
# construir com o número errado faz o preflight recusar a seção no primeiro
# if, e o corpus inteiro passa a exercitar SÓ o caminho de recusa.
SHT_NULL = 0
SHT_STRTAB = 3
SHT_DYNAMIC = 6
SHT_DYNSYM = 11
DT_NULL = 0
DT_SONAME = 14
EHDR_FMT = "<HHIQQQIHHHHHH"
SHDR_FMT = "<IIQQQQIIQQ"
SYM_SZ = 24
DYN_SZ = 16


def _shdr(data, off, sh_type, sh_offset, sh_size, sh_link=0, sh_entsize=0):
    # Elf64_Shdr: sh_name sh_type sh_flags sh_addr sh_offset sh_size
    #             sh_link sh_info sh_addralign sh_entsize
    struct.pack_into(SHDR_FMT, data, off,
                     0, sh_type, 0, 0, sh_offset, sh_size, sh_link, 0, 0, sh_entsize)


def make_dynsym_elf(names, soname=b"libkungfux.so"):
    """ELF64 mínimo, mas ESTRUTURALMENTE REAL: .dynstr + .dynsym + .dynamic.

    É a base sobre a qual as mutações de borda são aplicadas. Não depende de
    artefato de build (entra no git e o gate roda em qualquer host), e ainda
    assim leva o preflight pelo caminho feliz completo — inclusive achando um
    bc_mod_register de verdade na .dynsym.
    """
    strtab = bytearray(b"\0")
    offs = []
    for n in names:
        offs.append(len(strtab))
        strtab += n + b"\0"
    if soname is not None:
        soname_off = len(strtab)
        strtab += soname + b"\0"
    else:
        soname_off = None

    # Elf64_Sym: st_name, st_info, st_other, st_shndx, st_value, st_size
    dynsym = bytearray(SYM_SZ)  # índice 0 = símbolo nulo reservado
    for off in offs:
        # Elf64_Sym: st_name st_info st_other st_shndx st_value st_size
        dynsym += struct.pack("<IBBHQQ", off,
                              (1 << 4) | 2,   # STB_GLOBAL | STT_FUNC
                              0, 1, 0x1000, 0)

    dyn_entries = bytearray()
    if soname_off is not None:
        dyn_entries += struct.pack("<QQ", DT_SONAME, soname_off)
    dyn_entries += struct.pack("<QQ", DT_NULL, 0)

    str_off = 0x1000
    dyn_off = str_off + ((len(strtab) + 63) & ~63)
    sym_off = dyn_off + ((len(dyn_entries) + 63) & ~63)
    sh_off = sym_off + ((len(dynsym) + 63) & ~63)
    shnum = 4
    size = sh_off + shnum * 64

    data = bytearray(size)
    data[0:4] = b"\x7fELF"
    data[4] = 2   # ELFCLASS64
    data[5] = 1   # ELFDATA2LSB
    data[6] = 1   # EV_CURRENT
    struct.pack_into(EHDR_FMT, data, 16,
                     2, 0xB7, 1, 0, 0, sh_off, 0, 64, 0, 0, 64, shnum, 1)
    data[str_off:str_off + len(strtab)] = strtab
    data[dyn_off:dyn_off + len(dyn_entries)] = dyn_entries
    data[sym_off:sym_off + len(dynsym)] = dynsym
    _shdr(data, sh_off + 64, SHT_STRTAB, str_off, len(strtab))              # 1 .dynstr
    _shdr(data, sh_off + 128, SHT_DYNSYM, sym_off, len(dynsym), sh_link=1,   # 2 .dynsym
          sh_entsize=SYM_SZ)
    _shdr(data, sh_off + 192, SHT_DYNAMIC, dyn_off, len(dyn_entries),        # 3 .dynamic
          sh_link=1, sh_entsize=DYN_SZ)
    return bytes(data)


def seed_elf():
    """Corpus ESTÁVEL de elf_preflight: sintético, sem artefato de build."""
    # Caminho feliz completo: .dynsym com bc_mod_register + DT_SONAME honesto.
    put("elf_preflight", "elf_dynsym_ok",
        make_dynsym_elf([b"bc_mod_register", b"Java_com_foo_Bar_baz",
                         b"naoJni"], soname=b"libkungfux.so"))
    # Só o SONAME: o caminho que a guarda do frida-gadget examina.
    put("elf_preflight", "soname_frida",
        make_dynsym_elf([], soname=b"libfrida-gadget-raw.so"))
    put("elf_preflight", "soname_agent",
        make_dynsym_elf([], soname=b"frida-agent.so"))
    put("elf_preflight", "soname_gadget_so",
        make_dynsym_elf([], soname=b"frida-gadget.so"))
    put("elf_preflight", "soname_honesto",
        make_dynsym_elf([], soname=b"libsa2ammo.so"))
    put("elf_preflight", "soname_vazio", make_dynsym_elf([], soname=b""))
    put("elf_preflight", "sem_soname", make_dynsym_elf([], soname=None))

    base = make_dynsym_elf([b"bc_mod_register", b"Java_com_foo_Bar_baz"],
                           soname=b"libkungfux.so")
    n = len(base)

    # Arestas de cabeçalho: as mesmas que o Caso 53 do selftest lista.
    put("elf_preflight", "empty", b"")
    put("elf_preflight", "curto_16", base[:16])
    put("elf_preflight", "so_ehdr", base[:64])
    m = bytearray(base); m[0] = ord("X")
    put("elf_preflight", "nao_elf", bytes(m))
    m = bytearray(base); m[4] = 1
    put("elf_preflight", "elf32", bytes(m))
    m = bytearray(base); m[5] = 2
    put("elf_preflight", "big_endian", bytes(m))
    m = bytearray(base); m[6] = 9
    put("elf_preflight", "ei_version_9", bytes(m))
    m = bytearray(base); m[58] = 0
    put("elf_preflight", "shentsize_0", bytes(m))
    m = bytearray(base); m[58] = 8
    put("elf_preflight", "shentsize_8", bytes(m))
    m = bytearray(base); struct.pack_into("<Q", m, 40, n + 1)
    put("elf_preflight", "shoff_fora", bytes(m))
    m = bytearray(base); struct.pack_into("<Q", m, 40, 0)
    put("elf_preflight", "shoff_0", bytes(m))
    m = bytearray(base); struct.pack_into("<H", m, 60, 0)
    put("elf_preflight", "shnum_0", bytes(m))
    m = bytearray(base); struct.pack_into("<H", m, 60, 0xFFFF)
    put("elf_preflight", "shnum_grande", bytes(m))
    m = bytearray(base); struct.pack_into("<H", m, 62, 0xFFFF)
    put("elf_preflight", "shstrndx_grande", bytes(m))

    # Arestas de seção: .dynsym e .dynstr com offset/size/link impossíveis.
    for tag, sec, field, val in (
        ("dynsym_offset_fora", 2, "offset", n + 1),
        ("dynsym_size_fora", 2, "size", n + 1),
        ("dynsym_size_0", 2, "size", 0),
        ("dynsym_entsize_8", 2, "entsize", 8),
        ("dynsym_link_grande", 2, "link", 0xFFFF),
        ("dynsym_type_NOTE", 2, "type", 7),        # sh_type = SHT_NOTE
        ("dynstr_offset_fora", 1, "offset", n + 1),
        ("dynstr_size_0", 1, "size", 0),
        ("dynamic_entsize_8", 3, "entsize", 8),
        ("dynamic_size_0", 3, "size", 0),
        ("dynamic_size_7", 3, "size", 7),
        ("dynamic_link_grande", 3, "link", 0xFFFF),
    ):
        m = bytearray(base)
        _patch_shdr(m, sec, field, val)
        put("elf_preflight", tag, bytes(m))

    # Conteúdo: st_name fora do strtab, strtab sem NUL, símbolo indefinido,
    # DT_SONAME com d_val fora do strtab, e um .dynamic truncado.
    m = bytearray(base); _patch_syms(m, "st_name", 0xFFFFFFFF)
    put("elf_preflight", "st_name_fora", bytes(m))
    m = bytearray(base); _patch_syms(m, "st_name", 0)
    put("elf_preflight", "st_name_0", bytes(m))
    m = bytearray(base)
    _patch_strtab(m, b"X" * _strtab_size(base))
    put("elf_preflight", "strtab_sem_nul", bytes(m))
    m = bytearray(base); _patch_syms(m, "st_info", 0)  # STB_LOCAL | STT_NOTYPE
    put("elf_preflight", "sym_info_local", bytes(m))
    m = bytearray(base); _patch_dynsym_entry(m, shndx=0, value=0)
    put("elf_preflight", "sym_undef", bytes(m))
    m = bytearray(base); _patch_dyn(m, 0, val=n)
    put("elf_preflight", "soname_dval_fora", bytes(m))
    m = bytearray(base); _patch_dyn(m, 0, val=0xFFFFFFFF)
    put("elf_preflight", "soname_dval_max", bytes(m))
    m = bytearray(base); _patch_dyn(m, 0, tag=1)
    put("elf_preflight", "dyn_sem_soname", bytes(m))
    m = bytearray(base); _patch_dyn(m, 0, tag=0x6FFFFFF0)
    put("elf_preflight", "dyn_tag_desconhecido", bytes(m))
    # SONAME que o frida NÃO deve pegar: prefixo/sufixo que confundem strstr.
    for i, s in enumerate((b"libgadget-frida.so", b"frida.so", b"frida-gadget",
                           b"xfrida-gadgety", b"libfrida-agent-extra.so")):
        put("elf_preflight", "soname_%02d" % i,
            make_dynsym_elf([], soname=s))

    # Sem .so real aqui: libs/ é gitignored, então as sementes derivadas do
    # ndk-build vão para corpus-local (ver seed_elf_local).
    print("elf_preflight: %d seeds estáveis"
          % len(os.listdir(os.path.join(CORPUS, "elf_preflight"))), file=sys.stderr)


def _sec_index(base, sh_type):
    shoff = struct.unpack_from("<Q", base, 40)[0]
    shnum = struct.unpack_from("<H", base, 60)[0]
    for i in range(shnum):
        if struct.unpack_from("<I", base, shoff + i * 64 + 4)[0] == sh_type:
            return i
    return None


def _shdr_off(base, idx):
    return struct.unpack_from("<Q", base, 40)[0] + idx * 64


def _patch_shdr(m, sec_index, field, val):
    # sec_index aqui é o ÍNDICE da seção (0..3), não o sh_type.
    off = _shdr_off(bytes(m), sec_index)
    if field == "offset":
        struct.pack_into("<Q", m, off + 24, val)
    elif field == "size":
        struct.pack_into("<Q", m, off + 32, val)
    elif field == "entsize":
        struct.pack_into("<Q", m, off + 56, val)
    elif field == "link":
        struct.pack_into("<I", m, off + 40, val)
    elif field == "type":
        struct.pack_into("<I", m, off + 4, val)


def _patch_syms(m, field, val):
    """Aplica `val` em um campo de TODOS os símbolos (menos o nulo, índice 0)."""
    base = bytes(m)
    idx = _sec_index(base, SHT_DYNSYM)
    off = struct.unpack_from("<Q", base, _shdr_off(base, idx) + 24)[0]
    size = struct.unpack_from("<Q", base, _shdr_off(base, idx) + 32)[0]
    fmt = {"st_name": ("I", 0), "st_info": ("B", 4), "st_other": ("B", 5),
           "st_shndx": ("H", 6), "st_value": ("Q", 8), "st_size": ("Q", 16)}[field]
    for i in range(1, size // 24):
        struct.pack_into("<" + fmt[0], m, off + i * 24 + fmt[1], val)


def _patch_dynsym_entry(m, shndx=None, value=None):
    base = bytes(m)
    idx = _sec_index(base, SHT_DYNSYM)
    off = struct.unpack_from("<Q", base, _shdr_off(base, idx) + 24)[0]
    if shndx is not None:
        struct.pack_into("<H", m, off + 6, shndx)
    if value is not None:
        struct.pack_into("<Q", m, off + 8, value)


def _strtab_size(base):
    idx = _sec_index(base, SHT_STRTAB)
    return struct.unpack_from("<Q", base, _shdr_off(base, idx) + 32)[0]


def _patch_strtab(m, fill):
    base = bytes(m)
    idx = _sec_index(base, SHT_STRTAB)
    off = struct.unpack_from("<Q", base, _shdr_off(base, idx) + 24)[0]
    m[off:off + len(fill)] = fill


def _patch_dyn(m, entry, tag=None, val=None):
    base = bytes(m)
    idx = _sec_index(base, SHT_DYNAMIC)
    off = struct.unpack_from("<Q", base, _shdr_off(base, idx) + 24)[0]
    if tag is not None:
        struct.pack_into("<Q", m, off + entry * 16, tag)
    if val is not None:
        struct.pack_into("<Q", m, off + entry * 16 + 8, val)


def seed_elf_local():
    """Sementes dos .so REAIS do ndk-build — corpus das rodadas longas.

    Vai para test/fuzz/corpus-local/ (gitignored): libs/ não entra no repo,
    então estas sementes não podem ser a fonte do gate.
    """
    target = os.path.join(CORPUS_LOCAL, "elf_preflight")
    os.makedirs(target, exist_ok=True)
    # Limpa o que sobrou de uma rodada anterior (o .so muda a cada build).
    for name in os.listdir(target):
        os.unlink(os.path.join(target, name))
    n = 0
    for mod in ("kungfux", "sa2ammo", "u_patch", "u_frida", "u_dump",
                "mechabun", "sa2content", "t_crash", "u_noads"):
        blob = read_opt(os.path.join(ROOT, "mods", mod, "libs", "arm64-v8a",
                                     "lib%s.so" % mod))
        if blob:
            put("elf_preflight", "so_%s" % mod, blob, local=True)
            n += 1
    for name in ("/bin/ls", "/bin/bash", "/usr/lib/x86_64-linux-gnu/libc.so.6"):
        blob = read_opt(name)
        if blob and len(blob) < 4 * 1024 * 1024:
            put("elf_preflight", "host_" + os.path.basename(name), blob, local=True)
            n += 1
    if n:
        # Mutações dos .so reais: cada uma reproduz uma borda do Caso 53/63.
        big = read_opt(os.path.join(ROOT, "mods", "kungfux", "libs", "arm64-v8a",
                                    "libkungfux.so"))
        if big:
            n2 = len(big)
            put("elf_preflight", "so_cortado_64", big[:64], local=True)
            put("elf_preflight", "so_cortado_terco", big[: n2 // 3], local=True)
            put("elf_preflight", "so_menor_que_ehdr", big[:32], local=True)
            m = bytearray(big); struct.pack_into("<Q", m, 40, n2 + 1)
            put("elf_preflight", "so_shoff_fora", bytes(m), local=True)
            m = bytearray(big); struct.pack_into("<H", m, 60, 0xFFFF)
            put("elf_preflight", "so_shnum_grande", bytes(m), local=True)
            m = bytearray(big); m[58] = 0
            put("elf_preflight", "so_shentsize_0", bytes(m), local=True)
            m = bytearray(big); m[4] = 1
            put("elf_preflight", "so_elf32", bytes(m), local=True)
            m = bytearray(big); m[5] = 2
            put("elf_preflight", "so_big_endian", bytes(m), local=True)
            m = bytearray(big); m[0] = ord("X")
            put("elf_preflight", "so_nao_elf", bytes(m), local=True)
    print("elf_preflight local: %d sementes de .so" % n, file=sys.stderr)


# ----------------------------------------------------------- frida_config ---
FRIDA_SEEDS = [
    # O que uf_build_config() monta de verdade (modo script-directory).
    b'{"interaction":{"type":"script-directory","path":"/data/local/tmp/mods/com.foo.bar","on_change":"ignore"}}',
    # Modo script, o outro caminho aceito.
    b'{"interaction":{"type":"script","path":"/data/local/tmp/mods/com.foo.bar/a.js"}}',
    # Default do gadget = listen + wait: o config que congela o jogo.
    b'{}',
    b'{"teardown":"full"}',
    b'{"interaction":{}}',
    b'{"interaction":{"type":"listen"}}',
    b'{"interaction":{"type":"listen","address":"127.0.0.1","port":27042,"on_load":"wait"}}',
    b'{"interaction":{"type":"connect","address":"10.0.0.0","port":27052}}',
    b'{"port":27042}',
    # JSON malformado.
    b'',
    b'   ',
    b'\x00',
    b'{"interaction":{"type":"script",}',
    b'{"interaction":{"type":"script"}',
    b'{"interaction":{"type":"script"}} x',
    b'interaction: script',
    b'[{"interaction":{"type":"script"}}]',
    b'{"parameters":{"interaction":{"type":"script"}}}',
    # Duplicatas e escapes: as duas armas de "fingir ser script".
    b'{"interaction":{"type":"script"},"interaction":{"type":"listen"}}',
    b'{"interaction":{"type":"script","type":"listen"}}',
    b'{"interac\\u0074ion":{"type":"script"}}',
    b'{"interaction":{"type":"scrip\\u0074"}}',
    b'{"interaction":{"type":"script\\u002d-directory"}}',
    # Prefixo/sufixo/casing.
    b'{"interaction":{"type":"scripts"}}',
    b'{"interaction":{"type":"script-dir"}}',
    b'{"interaction":{"type":"Script"}}',
    b'{"interaction":{"type":"script "}}',
    b'{"interaction":{"type":" script"}}',
    # Tipo não-string.
    b'{"interaction":{"type":1}}',
    b'{"interaction":{"type":null}}',
    b'{"interaction":{"type":true}}',
    b'{"interaction":{"type":["script"]}}',
    b'{"interaction":{"type":{"type":"script"}}}',
    b'{"interaction":"script"}',
    b'{"interaction":1}',
    b'{"interaction":null}',
    # Números e escapes nos valores (o parser JSON também é fuzzado).
    b'{"interaction":{"type":"script"},"n":[1,-2.5e3,true,null,{}]}',
    b'{"interaction":{"type":"script"},"n":[-0.5E+3]}',
    b'{"interaction":{"type":"script"},"n":[01]}',
    b'{"interaction":{"type":"script"},"n":[1.]}',
    b'{"interaction":{"type":"script"},"n":[.5]}',
    b'{"interaction":{"type":"script"},"n":[1e]}',
    b'{"interaction":{"type":"script"},"s":"\\ud800"}',
    b'{"interaction":{"type":"script"},"s":"\\u00"}',
    b'{"interaction":{"type":"script"},"s":"\\q"}',
    b'{"interaction":{"type":"script"},"s":"\x01"}',
    b'{"interaction":{"type":"script"},"s":"\\ud83d\\ude00"}',
    b'{ }',
    b'{,}',
    b'{"a"::1}',
    b'{"a" 1}',
    b'{{"interaction":{"type":"script"}}}',
    b'{"interaction":{"type":"script"},}',
    b'\\x00{"interaction":{"type":"script"}}',
    b'{"interaction":{"type":"script"}}\x00{"interaction":{"type":"script"}}',
    b'{"interaction":{"type":"script"}}',
    # Aninhamento profundo: a guarda de profundidade tem que segurar.
    b'{"interaction":{"type":"script"},"x":' + b'[' * 200 + b']' * 200 + b'}',
    b'{"interaction":{"type":"script"},"x":' + b'{"a":' * 200 + b'1' + b'}' * 200 + b'}',
    # Bem maior que UF_CONFIG_MAX: recusado pelo cabeçalho, nunca lido pela
    # metade — mas o caminho é exercitado.
    b'{"interaction":{"type":"script"},"pad":"' + b'a' * 5000 + b'"}',
    b'{"interaction":{"type":"script"},"pad":"' + b'a' * 4090 + b'"}',
]

FRIDA_NAME_SEEDS = [
    b"meu_mod.js", b"meu_mod", b"foo.js.off", b".js", b"a/b.js", b"",
    b"u_frida.so", b"..js", b"x.jsx", b".hidden.js", b"a" * 4000 + b".js",
    b"com.dts.freefireth", b"com.foo_bar.Baz2", b"com.foo/../../etc",
    b"a/b", b"..", b"com..foo", b".foo", b"x; id", b"x$(id)", b"zygote64",
    b"zygote", b"a" * 200, b"a" * 127, b"a" * 128, b"a" * 129,
    b"/etc/passwd", b"com.foo\x01bar", b"com.foo bar",
]


def seed_frida():
    for i, s in enumerate(FRIDA_SEEDS):
        put("frida_config", "cfg_%02d" % i, s)
    for i, s in enumerate(FRIDA_NAME_SEEDS):
        put("frida_config", "name_%02d" % i, s)


# ------------------------------------------------------------------ selmix ---
SELMIX_SEEDS = [
    # .conf de mods real (o formato que o companion escreve e o Manager regrava).
    b"appInit=off\nappUpdateDraw=off\nappTouch=off\nappKey=off\nthrottle_every=30\nstream_source=companion\n",
    b"# comentario\nappInit=on\n\nthrottle_every=99999\n",
    b"throttle_every=abc\nthrottle_every=30xyz\nthrottle_every=-5\nthrottle_every=0\nthrottle_every=1\nthrottle_every=600\nthrottle_every=601\n",
    b"stream_source=android\nstream_source=game\nstream_source=companion\nstream_source=\n",
    b"chave_fantasma=on\nthrottle_every=10\nthrottle_every=20\n",
    b"appKey=verdade\nappKey=on\nappKey=off\nappKey=ON\nappKey=OFF\nappKey=\n",
    b"throttle_every=6",  # sem \n final (companion morreu no meio da escrita)
    b"=on\n=\n==\n",
    b"app" + b"I" * 200 + b"nit=on\n",
    b"a" * 4000 + b"=on\n",
    b"throttle_every=" + b"9" * 400 + b"\n",
    # allowlist de pacote (arquivo que o usuario dá push por adb).
    b"com.foo.bar\n# comentario\n\ncom.baz.qux \n  com.indentado\n",
    b"",
    b"\r\n\r\n",
    b"#",
    b"com.foo.bar",
    b"a" * 9000,
    b"com." + b"b" * 4000,
    # property persist.* (o valor vem do JNI, não do disco).
    b"1", b"7", b"1 sa2ammo", b"1 sa2ammo\n2 sa2ammo\n", b" 1 x", b"1 ", b" ",
    b"999999999999999999999999 sa2ammo", b"\x01 \x02",
    # Assinatura de prólogo real (appInit do libnative-lib.so, Caso 51) e o
    # segmento em volta dela, para o bc_pattern_scan_buffer ter o match.
    bytes([0xfd, 0x7b, 0xbd, 0xa9, 0xf6, 0x57, 0x01, 0xa9, 0xf4, 0x4f, 0x02, 0xa9,
           0xfd, 0x03, 0x00, 0x91, 0x16, 0x41, 0x00, 0xb0, 0xc8, 0xca, 0x40, 0xf9]),
    bytes([0xff, 0x43, 0x01, 0xd1, 0xfd, 0x7b, 0x02, 0xa9, 0xf6, 0x57, 0x03, 0xa9,
           0xf4, 0x4f, 0x04, 0xa9, 0xfd, 0x83, 0x00, 0x91, 0x54, 0xd0, 0x3b, 0xd5]),
    b"\x90" * 88 + bytes([0xfd, 0x7b, 0xbd, 0xa9, 0xf6, 0x57, 0x01, 0xa9, 0xf4, 0x4f,
                         0x02, 0xa9, 0xfd, 0x03, 0x00, 0x91, 0x16, 0x41, 0x00, 0xb0,
                         0xc8, 0xca, 0x40, 0xf9]) + b"\x90" * 200,
    # Regra C4 inteira (a chave de dedupe do u_patch é o texto da regra).
    b"regra|1|Foo|Bar|0|1|true",
    b"field|WeaponInfo|unlimitedAmmo|bool|true|SelectWeapon|1\n",
    b"return|ComplexCreature|HasAmmo|0|bool|true\n",
    b"a" * 4000,
]


def seed_selmix():
    for i, s in enumerate(SELMIX_SEEDS):
        put("selmix", "seed_%02d" % i, s)


if __name__ == "__main__":
    seed_c4()
    seed_elf()
    seed_frida()
    seed_selmix()
    # As sementes de .so real são opcional: só existem depois do ndk-build, e
    # servem às rodadas longas (as 10 min), não ao gate.
    if "--local" in sys.argv:
        seed_elf_local()
    for target in ("c4_line", "elf_preflight", "frida_config", "selmix"):
        n = len(os.listdir(os.path.join(CORPUS, target)))
        print("%-14s %3d seeds" % (target, n))
