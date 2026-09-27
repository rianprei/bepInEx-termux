#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Gera o corpus deterministico de test/fixtures/modtypes/.

Toda saida e derivada de contantes, sem tempo, sem random, sem rede:
rodar o script duas vezes produz byte a byte o mesmo arquivo (determinismo
checado pelo ModTypeMatrixTest, que reprova corpus que muda).

Corpus (o que o detector tem que saber dizer a verdade sobre):
  ELF arm64/arm32/x86_64, frida-gadget sintetico (ELF arm64 + SONAME +
  marca no conteudo), .dll .NET Mono/IL2CPP, .dll PE nativo, .exe,
  .bpatch C4 valido e quebrado, .patch C4 valido com a extensao ANTIGA,
  .js Frida, .lua GameGuardian,
  .bmod valido, .bmod zip-slip, .zip BepInEx de PC, .apk/.obb/.xapk (zip),
  .pak Unreal, .bundle UnityFS, .json de save, .txt, vazio, 0B .so,
  ELF chamado .png e zip chamado .so.

Uso: python3 generate.py   (a partir de qualquer diretorio)
"""

import os
import struct
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))

# ---------------------------------------------------------------- ELF helpers


def ehdr(class_, data, etype, machine, phoff, phnum):
    """Cabecalho ELF de 64 bytes (ELF64)."""
    e = bytearray(64)
    e[0:4] = b"\x7fELF"
    e[4] = class_
    e[5] = data
    e[6] = 1                      # e_version = EV_CURRENT
    struct.pack_into("<HH", e, 16, etype, machine)
    struct.pack_into("<IQQ", e, 20, 1, 0, phoff)   # e_version, e_entry, e_phoff
    struct.pack_into("<HHHHHH", e, 52, 64, 56, phnum, 64, 0, 0)
    return e


def elf64_arm64(phoff=64, phnum=1, pad=0x2000):
    """ELF64 LE arm64 ET_DYN coerente: 1 PT_LOAD de 256B em 0x1000."""
    e = ehdr(2, 1, 3, 183, phoff, phnum)
    out = bytearray(pad)
    out[0:len(e)] = e
    # 1 programa: PT_LOAD R+X, offset/vaddr 0x1000, filesz/memsz 256
    struct.pack_into("<IIQQQQQQ", out, phoff, 1, 5, 0x1000, 0x1000, 0x1000,
                     256, 256, 0x1000)
    return bytes(out)


def elf64(machine, pad=0x2000):
    """ELF64 LE generico (x86_64 = 62, armhf...), mesma forma fisica do arm64."""
    e = ehdr(2, 1, 3, machine, 64, 1)
    out = bytearray(pad)
    out[0:len(e)] = e
    struct.pack_into("<IIQQQQQQ", out, 64, 1, 5, 0x1000, 0x1000, 0x1000,
                     256, 256, 0x1000)
    return bytes(out)


def elf32(machine, pad=2048):
    """ELF32 LE com uma entrada PT_LOAD valida."""
    e = bytearray(52)
    e[0:4] = b"\x7fELF"
    e[4] = 1                      # ELFCLASS32
    e[5] = 1                      # ELFDATA2LSB
    e[6] = 1
    struct.pack_into("<HH", e, 16, 3, machine)   # ET_DYN
    struct.pack_into("<I", e, 20, 1)
    struct.pack_into("<I", e, 28, 52)            # e_phoff
    struct.pack_into("<HHH", e, 40, 52, 32, 1)  # header + 1 program header
    out = bytearray(pad)
    out[0:len(e)] = e
    struct.pack_into("<IIIIIIII", out, 52, 1, 0, 0, 0, pad, pad, 5, 4096)
    return bytes(out)


# ---------------------------------------------------------------- zip helpers

def store_zip(path, entries):
    """Zip deterministico: STORAGE (sem deflate), timestamp fixo."""
    with zipfile.ZipFile(path, "w", zipfile.ZIP_STORED) as z:
        for name, data in entries:
            zi = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
            zi.external_attr = 0o600 << 16
            z.writestr(zi, data)


BMOD_MANIFEST = (
    b'{"format":1,"id":"corpus-bmod","name":"Mod do corpus","version":"1.0",'
    b'"author":"qa","description":"fixture","game":"*","engine":"unity-il2cpp",'
    b'"type":"native","options":[]}'
)

DOTNET_MONO_DLL = (
    b"MZ" + bytes(60) +
    b"Texto real de um assembly .NET: os nomes mscorlib, _CorDllMain e "
    b"mscoree aparecem em todo assembly .NET (BepInEx 5 / MelonLoader Mono)."
)

DOTNET_IL2CPP_DLL = (
    b"MZ" + bytes(60) +
    b"Texto real de um assembly .NET IL2CPP: mscorlib, mscoree e as refs do "
    b"interop Il2CppInterop, Il2CppDomain e UnhollowerBaseLib."
)

PE_NATIVE_DLL = (
    b"MZ" + bytes(60) +
    b"Este e um .dll nativo de Windows (PE de maquina de verdade), sem "
    b"runtime gerenciado: export table e import table de DLL C/C++ pura."
)

JS_FRIDA = (
    b"// script Frida: hook no metodo Update do jogo\n"
    b"Java.perform(function () {\n"
    b"  var m = Process.getModuleByName('libunity.so');\n"
    b"  Interceptor.attach(Module.findExportByName('libunity.so', 'Update'), {\n"
    b"    onEnter: function (args) { Memory.writeU32(args[0], 9999); }\n"
    b"  });\n"
    b"});\n"
)

LUA_GG = (
    b"-- script GameGuardian: nasce num menu, roda dentro do gg\n"
    b"function menu()\n"
    b"  gg.clearResults()\n"
    b"  gg.searchNumber('9999', gg.TYPE_DWORD)\n"
    b"end\n"
)

PATCH_OK = (
    b"# regras C4 do corpus (o mesmo formato que o Mod Maker escreve)\n"
    b"return ComplexCreature HasAmmo 0 bool true\n"
    b"mul WeaponDamage DamageFactor 0 float 3.5\n"
    b"field WeaponInfo unlimitedAmmo bool true Update 1\n"
)

# .bpatch quebrado: nenhuma linha da gramatica C4 (tipos inventados, nargs
# negativo, campo sobrando) -> PatchGenerator.parse devolve lista vazia.
PATCH_BROKEN = (
    b"# parece regra, mas o C4 nao aceita nenhuma\n"
    b"return Foo bar -1 bool true\n"
    b"mul Foo bar 0 string abc\n"
    b"field Foo bar bool true Metodo 1 extra\n"
)

SAVE_JSON = (
    b'{"save_version":3,"player":{"gold":12345,"xp":99.5,"pos":[1.0,2.0]},'
    b'"inventory":[{"item":"sword","count":2}],"timestamp":1769904000}'
)

# SONAME e marca reais dentro de um ELF sintetico: o gadget e barrado por
# nome OU por conteudo; aqui os dois caminhos tem prova.
GADGET_NAME = b"libfrida-gadget-raw.so"
GADGET_ELF = elf64_arm64()[:0x1000] + GADGET_NAME + bytes(
    0x1000 - len(GADGET_NAME)) + b"frida-agent: gumboca gum-js-loop\0"


def write(name, data):
    with open(os.path.join(HERE, name), "wb") as f:
        f.write(data)
    return name


def main():
    made = []
    # --- ELF ---
    made.append(write("mod_arm64.so", elf64_arm64()))
    # mentiroso: ELF arm64 com nome de imagem (o CONTEUDO manda: instala)
    made.append(write("imagem_falsa.png", elf64_arm64()))
    # mentiroso: zip com nome de .so (o CONTEUDO manda: recusa como zip)
    store_zip(os.path.join(HERE, "compactado_mentiroso.so"),
              [("leia_me.txt", b"Isto e um zip comum com nome de .so.")])
    made.append("compactado_mentiroso.so")
    made.append(write("mod_arm32.so", elf32(40)))
    made.append(write("mod_x86_64.so", elf64(62)))
    # --- frida-gadget sintetico (SONAME libfrida-gadget-raw.so no conteudo) ---
    made.append(write("frida-gadget-raw.so", GADGET_ELF))
    # --- PE / .NET ---
    made.append(write("mod_pcinho.dll", DOTNET_MONO_DLL))
    made.append(write("mod_il2cpp.dll", DOTNET_IL2CPP_DLL))
    made.append(write("dll_nativo.dll", PE_NATIVE_DLL))
    made.append(write("hackeador.exe", PE_NATIVE_DLL))
    # --- texto ---
    made.append(write("regras_boas.bpatch", PATCH_OK))
    made.append(write("regras_quebradas.bpatch", PATCH_BROKEN))
    # O MESMO conteudo valido, com a extensao ANTIGA (.patch) e SEM extensao
    # nenhuma. E o que segura o item 3 do rename: o detector decide pelo
    # CONTEUDO, nao pelo nome, entao um arquivo C4 valido que o usuario trouxe
    # de onde veio tem que ser reconhecido e instalado como <id>.bpatch. Se
    # alguem "simplificar" o detector para olhar so a extensao, estes dois
    # param de instalar e o usuario fica sem mod e sem explicacao.
    made.append(write("regras_ext_antiga.patch", PATCH_OK))
    made.append(write("regras_sem_extensao", PATCH_OK))
    made.append(write("script_frida.js", JS_FRIDA))
    made.append(write("script_gg.lua", LUA_GG))
    made.append(write("save_do_jogo.json", SAVE_JSON))
    made.append(write("leia_me.txt", b"Aviso do criador do mod: instale com o Manager.\n"))
    made.append(write("vazio_sem_extensao", b""))
    made.append(write("so_zero.so", b""))
    # --- zips ---
    store_zip(os.path.join(HERE, "pacote_ok.bmod"),
              [("manifest.json", BMOD_MANIFEST), ("mod.so", elf64_arm64()[:1024])])
    made.append("pacote_ok.bmod")
    store_zip(os.path.join(HERE, "zip_slip.bmod"),
              [("manifest.json", BMOD_MANIFEST), ("../evil.so", elf64_arm64()[:1024])])
    made.append("zip_slip.bmod")
    store_zip(os.path.join(HERE, "bepinex_pc.zip"),
              [("BepInEx/plugins/mod_pcinho.dll", DOTNET_MONO_DLL),
               ("patcher/MeuPatcher.dll", DOTNET_MONO_DLL)])
    made.append("bepinex_pc.zip")
    store_zip(os.path.join(HERE, "jogo.apk"),
              [("AndroidManifest.xml", b"\x03\x00\x08\x00binario binario AXML"),
               ("classes.dex", b"dex\n035\x00" + bytes(512))])
    made.append("jogo.apk")
    store_zip(os.path.join(HERE, "expansao.obb"), [("jogo/assets/main", bytes(1024))])
    made.append("expansao.obb")
    store_zip(os.path.join(HERE, "pacote.xapk"),
              [("manifest.json", b'{"xapk_version":2,"name":"Jogo"}'),
               ("com.foo.apk", b"PK\x03\x04falso")])
    made.append("pacote.xapk")
    made.append(write("dados.pak", b"UE4" + struct.pack("<I", 17) + bytes(2048)))
    made.append(write("asset.bundle", b"UnityFS\x00" + struct.pack("<HI", 2021, 7)
                      + bytes(2048)))
    print("corpus gerado em %s: %d arquivos" % (HERE, len(made)))


if __name__ == "__main__":
    sys.exit(main())
