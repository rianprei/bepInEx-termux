#!/usr/bin/env python3
"""tabs_patch.py — patcher reproduzível do APK do TABS (tabs-notelemetry).

POR QUE PATCH DE APK E NÃO FRIDA. O usuário liberou mexer no APK deste jogo
(_prompts/freebuff_tabs_mobile.md, "MUDANÇA DO USUÁRIO"), e a medição mostra
que não precisa de hook: a chave que liga a telemetria está em um JSON de
asset, e o plugin de atribuição é ligado por um meta-data do manifest. Cortar
por config é o veículo mais simples que existe — sem binário de 25 MB na RAM,
sem risco do gadget crashar o jogo (o 17.19.0 já crashou no POCO C75), sem
processo extra, e o resultado é um APK que dá para diffar entrada por entrada.
A rota Frida (deny-list no Engine Bridge) fica de plano B e só entra se a
medição pós-patch mostrar que sobrou telemetria.

O QUE O PATCH FAZ (etapa `telemetry`, tudo medido):

  1. assets/XDConfig.json → tapsdk.db_config.enable = false
     O valor true está no asset e volta ecoado na resposta do bridge
     (XDGCoreService.initSDK → "dbConfig":{"channel":"Google","enableTapDB":true}),
     e o server_url do mesmo bloco (u0m5lrcp.cloud.ap-sg.tapapis.com) é o
     ÚNICO host de telemetria do uid 10310 medido (110/110 amostras de
     /proc/net/tcp).

  2. AndroidManifest.xml → dois meta-data de "loader" de true para false:
       com.tapsdk.tapdb.loader    (plugin de TapDB: analytics)
       com.xd.third.track.loader  (plugin de atribuição: Adjust)
     Os dois plugins estão no APK (com.tds.tapdb.*, com.xd.thrid.track.* com
     XDGThirdTrack, XDThirdTrackCore, AdjustLifecycleCallbacks). O manifest
     lista o carregador de cada plugin do TapSDK lado a lado com os de
     login, anti-addiction, momento, conta, pagamento, share, conquistas e
     anúncio — desligar dois desses flags é o corte mais estreito que existe
     neste APK, e não toca nenhum dos outros.

     Por que os dois e não só o JSON: o config é mesclado com uma resposta
     remota (XDConfigManager$2.onSuccess no logcat), então o servidor pode
     mandar o `enable` de volta para true; o meta-data do manifest é decisão
     local do app no boot, ninguém o sobrescreve por rede. O device mede qual
     dos dois bastou (o logcat mostra `initTapDB`/`register isc service
     XDThirdTrack`), e o relatório diz.

O QUE O PATCH NÃO FAZ. Não toca login, pagamento/IAP, anti-addiction,
conquistas, TapMoment (conteúdo) nem anúncios — anúncio é a etapa `noads`, com
o próprio interruptor (com.tapsdk.billboard.loader + o fetch do marquee), e
nada aqui pode virar desbloqueio de compra: o patch só escreve `false` em
dois interruptores que já existiam no APK original.

REPRODUTIBILIDADE. A entrada é conferida contra tools/tabs_apk.lock (sha256 do
base.apk original + sha256 de cada entrada tocada). Saída é função pura de
(entrada, etapas): as entradas não tocadas são copiadas byte a byte, sem
recomprimir. O APK não entra no git; só o lock, o patcher e o relatório de
mudanças.

USO
  python3 tools/tabs_patch.py --check --stages telemetry
  python3 tools/tabs_patch.py --apk <original> --out <patched> --stages telemetry
  python3 tools/tabs_patch.py --list-stages
"""
import argparse
import binascii
import hashlib
import json
import os
import struct
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOCK_PATH = os.path.join(ROOT, "tools", "tabs_apk.lock")

# ---------------------------------------------------------------- AXML ----
# Patch do manifest binário. Não é um editor de XML genérico: ele localiza o
# elemento <meta-data> cujo android:name é o nome pedido e troca o DWORD de
# dados do atributo android:value. Se não achar exatamente um, erro — um patch
# que não acha o alvo não pode passar em silêncio, porque o resultado é um
# APK que instala e finge funcionar.
AXML_HEADER = 0x00080003
CHUNK_STRING_POOL = 0x001C0001
CHUNK_START_ELEMENT = 0x00100102
CHUNK_END_ELEMENT = 0x00100103
TYPE_INT_BOOLEAN = 0x12
NO_ENTRY = 0xFFFFFFFF
ATTR_NAME = 0x01010003  # android:name
ATTR_VALUE = 0x01010024  # android:value


class AxmlError(Exception):
    pass


class Axml:
    """Leitor/escritor mínimo de AXML: só o que o patch precisa."""

    def __init__(self, data):
        self.data = bytearray(data)
        self.strings = []
        self._parse()

    def _parse(self):
        d = self.data
        if len(d) < 8 or struct.unpack_from("<I", d, 0)[0] != AXML_HEADER:
            raise AxmlError("não é AXML (magic ausente)")
        # O primeiro chunk do arquivo (0x00080003) é o cabeçalho do ARQUIVO
        # e o size dele é o tamanho inteiro do AXML, não o do chunk: o primeiro
        # sub-chunk (a pool de strings) começa em 8, não em <size>.
        off = 8
        end = len(d)
        while off < end - 8:
            ctype, csize = struct.unpack_from("<II", d, off)
            if ctype == CHUNK_STRING_POOL:
                self._parse_string_pool(off)
            elif ctype == CHUNK_START_ELEMENT:
                pass
            if csize <= 0:
                raise AxmlError("chunk com tamanho %d em %d" % (csize, off))
            off += csize
        if not self.strings:
            raise AxmlError(
                "pool de strings vazio (isso não é um AXML com strings, ou o "
                "formato mudou) — o patch para aqui em vez de escrever Bytes "
                "no lugar errado")

    def _parse_string_pool(self, off):
        d = self.data
        (count, style_count, flags, str_off, style_off) = struct.unpack_from("<IIIII", d, off + 8)
        is_utf8 = bool(flags & (1 << 8))
        offsets = struct.unpack_from("<%dI" % count, d, off + 28)
        # str_off é relativo ao INÍCIO do chunk da pool (28 + 4*count +
        # 4*style_count no caso comum) — usar o valor do header em vez de
        # recalcular é o que sobrevive a pool com estilos.
        base = off + str_off
        for i in range(count):
            p = base + offsets[i]
            if is_utf8:
                # utf16_size (1-2 bytes) + utf8_size (1-2 bytes)
                n1 = d[p]
                p += 2 if n1 & 0x80 else 1
                n2 = d[p]
                p += 2 if n2 & 0x80 else 1
                raw = bytes(d[p:p + n2])
                self.strings.append(raw.decode("utf-8", "replace"))
            else:
                n = struct.unpack_from("<H", d, p)[0]
                p += 2
                if n & 0x8000:
                    n2 = struct.unpack_from("<H", d, p)[0]
                    p += 2
                    n = ((n & 0x7FFF) << 16) | n2
                raw = bytes(d[p:p + 2 * n])
                self.strings.append(raw.decode("utf-16-le", "replace"))
        del style_count, style_off

    def _string(self, idx):
        if idx == NO_ENTRY or idx >= len(self.strings):
            return None
        return self.strings[idx]

    def meta_data_value_offset(self, meta_name):
        """DWORD (file offset) do dado de android:value do meta-data pedido."""
        d = self.data
        off = 8  # depois do cabeçalho do arquivo AXML (mesmo motivo de _parse)
        end = len(d)
        found = None
        while off < end - 8:
            ctype, csize = struct.unpack_from("<II", d, off)
            if ctype == CHUNK_START_ELEMENT:
                # ResXMLTree_node = header(8) + lineNumber(4) + comment(4) = 16,
                # e ResXMLTree_attrExt começa aí: ns(16) name(20)
                # attributeStart(24) attributeSize(26) attributeCount(28).
                name_idx = struct.unpack_from("<I", d, off + 20)[0]
                attr_start, attr_size, attr_count = struct.unpack_from("<HHH", d, off + 24)
                if self._string(name_idx) == "meta-data":
                    a = off + 16 + attr_start
                    for _ in range(attr_count):
                        # ResXMLTree_attribute.name é ÍNDICE DA POOL DE STRINGS
                        # (o 0x01010003 que o aapt2 imprime é o resource id
                        # resolvido pelo resource-map chunk, não o que está no
                        # arquivo) — por isso a comparação é por string.
                        a_ns = struct.unpack_from("<I", d, a)[0]
                        a_name = struct.unpack_from("<I", d, a + 4)[0]
                        a_raw = struct.unpack_from("<I", d, a + 8)[0]
                        a_size, a_res, a_type = struct.unpack_from("<HBB", d, a + 12)
                        a_data = struct.unpack_from("<I", d, a + 16)[0]
                        del a_ns, a_size, a_res, a_data
                        if self._string(a_name) == "name" and self._string(a_raw) == meta_name:
                            if found is not None:
                                raise AxmlError("meta-data %s aparece mais de uma vez" % meta_name)
                            for k in range(attr_count):
                                aa = a + k * attr_size
                                n_name = struct.unpack_from("<I", d, aa + 4)[0]
                                if self._string(n_name) == "value":
                                    found = aa + 16
                                    break
                            if found is None:
                                raise AxmlError("meta-data %s sem atributo value" % meta_name)
                        a += attr_size
            if csize <= 0:
                break
            off += csize
        if found is None:
            raise AxmlError("meta-data %s não encontrado no manifest" % meta_name)
        return found

    def read_meta_data_value(self, meta_name):
        off = self.meta_data_value_offset(meta_name)
        a_size, a_res, a_type = struct.unpack_from("<HBB", self.data, off - 4)
        return a_type, struct.unpack_from("<I", self.data, off)[0]

    def set_attr_enum(self, element, attr_name, value, expected=None):
        """Troca um atributo enum (android:installLocation e afins).

        No manifest do TABS o installLocation NÃO é string: é um enum
        (tipo 0x10, data=2 = preferExternal). Trocar por string exigiria mexer
        na pool de strings por um valor que não existe lá; trocar o DWORD é o
        patch de 4 bytes certo. MEDIDO: sem esta opção o installd falha com
        "Failed to override installation location" num aparelho com /data
        apertado (2026-09-28, POCO C75), e com ela o APK de 1,4 GB instala.
        """
        d = self.data
        off = 8
        end = len(d)
        while off < end - 8:
            ctype, csize = struct.unpack_from("<II", d, off)
            if ctype == CHUNK_START_ELEMENT:
                name_idx = struct.unpack_from("<I", d, off + 20)[0]
                attr_start, attr_size, attr_count = struct.unpack_from("<HHH", d, off + 24)
                if self._string(name_idx) == element:
                    a = off + 16 + attr_start
                    for _ in range(attr_count):
                        a_name = struct.unpack_from("<I", d, a + 4)[0]
                        a_size, a_res, a_type = struct.unpack_from("<HBB", d, a + 12)
                        if self._string(a_name) == attr_name:
                            if a_type != 0x10:
                                raise AxmlError("%s/%s não é enum (tipo 0x%02x)"
                                                % (element, attr_name, a_type))
                            old = struct.unpack_from("<I", d, a + 16)[0]
                            if expected is not None and old != expected:
                                raise AxmlError("%s/%s é %d, esperava %d: o APK de "
                                                "entrada não é o esperado"
                                                % (element, attr_name, old, expected))
                            struct.pack_into("<I", d, a + 16, value)
                            back = struct.unpack_from("<I", d, a + 16)[0]
                            if back != value:
                                raise AxmlError("escrita de %s/%s não confirmada"
                                                % (element, attr_name))
                            return old
                        a += attr_size
            if csize <= 0:
                break
            off += csize
        raise AxmlError("atributo %s de <%s> não encontrado" % (attr_name, element))

    def read_attr_enum(self, element, attr_name):
        d = self.data
        off = 8
        while off < len(d) - 8:
            ctype, csize = struct.unpack_from("<II", d, off)
            if ctype == CHUNK_START_ELEMENT:
                name_idx = struct.unpack_from("<I", d, off + 20)[0]
                attr_start, attr_size, attr_count = struct.unpack_from("<HHH", d, off + 24)
                if self._string(name_idx) == element:
                    a = off + 16 + attr_start
                    for _ in range(attr_count):
                        a_name = struct.unpack_from("<I", d, a + 4)[0]
                        if self._string(a_name) == attr_name:
                            return struct.unpack_from("<I", d, a + 16)[0]
                        a += attr_size
            if csize <= 0:
                break
            off += csize
        raise AxmlError("atributo %s de <%s> não encontrado" % (attr_name, element))

    def set_meta_data_boolean(self, meta_name, value):
        """true→false (0xFFFFFFFF→0) ou o inverso. Devolve o valor antigo."""
        off = self.meta_data_value_offset(meta_name)
        a_type = struct.unpack_from("<B", self.data, off - 1)[0]
        if a_type != TYPE_INT_BOOLEAN:
            raise AxmlError("android:value de %s não é booleano (tipo 0x%02x): "
                            "o patch só troca booleano, e mexer em string aqui "
                            "exigiria editar a pool de strings" % (meta_name, a_type))
        old = struct.unpack_from("<I", self.data, off)[0]
        new = 0xFFFFFFFF if value else 0
        struct.pack_into("<I", self.data, off, new)
        back_type, back = self.read_meta_data_value(meta_name)
        if back_type != TYPE_INT_BOOLEAN or back != new:
            raise AxmlError("escrita de %s não confirmada na releitura" % meta_name)
        return bool(old)


# ----------------------------------------------------------------- ZIP ----
LFH_SIG = 0x04034B50
CDH_SIG = 0x02014B50
EOCD_SIG = 0x06054B50


class ZipError(Exception):
    pass


def read_central_directory(f):
    """(entradas, offset do EOCD, comentário) — offsets absolutos no arquivo."""
    f.seek(0, os.SEEK_END)
    size = f.tell()
    tail_len = min(size, 66000)
    f.seek(size - tail_len)
    tail = f.read(tail_len)
    pos = tail.rfind(b"PK\x05\x06")
    if pos < 0:
        raise ZipError("EOCD não encontrado (zip64 ou arquivo truncado?)")
    eocd_abs = size - tail_len + pos
    # EOCD: assinatura, disco, disco do CD, entradas do disco, entradas
    # totais, tamanho do CD, offset do CD, comentário.
    (_sig, _disk, _cd_disk, _this_disk, total, cd_size, cd_off,
     comment_len) = struct.unpack_from("<IHHHHIIH", tail, pos)
    if cd_off == 0xFFFFFFFF or total == 0xFFFF:
        raise ZipError("zip64 não suportado: este patcher só faz o APK do TABS")
    f.seek(cd_off)
    cd = f.read(cd_size)
    entries = []
    p = 0
    while p < len(cd) and struct.unpack_from("<I", cd, p)[0] == CDH_SIG:
        (_sig, ver_made, ver_need, flags, method, mtime, mdate, crc, csize, usize,
         nlen, elen, clen, disk, iattr, eattr, lho) = struct.unpack_from("<IHHHHHHIIIHHHHHII", cd, p)
        name = cd[p + 46:p + 46 + nlen].decode("utf-8", "replace")
        entries.append({
            "name": name, "flags": flags, "method": method, "mtime": mtime,
            "mdate": mdate, "crc": crc, "csize": csize, "usize": usize,
            "extra": cd[p + 46 + nlen:p + 46 + nlen + elen],
            "comment": cd[p + 46 + nlen + elen:p + 46 + nlen + elen + clen],
            "ver_made": ver_made, "ver_need": ver_need,
            "iattr": iattr, "eattr": eattr, "lho": lho,
        })
        p += 46 + nlen + elen + clen
    if len(entries) != total:
        raise ZipError("central directory com %d entradas, EOCD diz %d" % (len(entries), total))
    return entries, eocd_abs, tail[pos + 22:pos + 22 + comment_len]


def local_data_span(f, e):
    """(início do header local, início do dado, tamanho total do registro)."""
    f.seek(e["lho"])
    lh = f.read(30)
    if struct.unpack_from("<I", lh, 0)[0] != LFH_SIG:
        raise ZipError("local header inválido em %s" % e["name"])
    nlen, elen = struct.unpack_from("<HH", lh, 26)
    data_start = e["lho"] + 30 + nlen + elen
    if e["flags"] & 0x08:
        raise ZipError("entrada %s usa data descriptor: não suportado" % e["name"])
    return e["lho"], data_start, e["csize"]


def rewrite_zip(src_path, dst_path, replacements):
    """Copia o zip byte a byte, trocando só as entradas de `replacements`.

    O bloco de assinatura do APK (v2/v3) fica entre a última entrada e o
    diretório central: ele é descartado aqui, e o apksigner reassina depois.
    Reescrever tudo com zipfile recomprimiria 1,4 GB; aqui só os bytes das
    entradas tocadas são novos.
    """
    with open(src_path, "rb") as fin, open(dst_path, "wb") as fout:
        entries, _eocd_abs, comment = read_central_directory(fin)
        names = {e["name"] for e in entries}
        for name in replacements:
            if name not in names:
                raise ZipError("entrada %s não existe no APK de entrada" % name)
        out_entries = []
        pos = 0
        for e in entries:
            lh_start, data_start, csize = local_data_span(fin, e)
            if lh_start != pos:
                raise ZipError("entrada %s começa em %d, esperava %d (tem lixo entre)"
                               % (e["name"], lh_start, pos))
            new = replacements.get(e["name"])
            # O offset do header local NOVA é a posição antes de escrever:
            # o central directory aponta para ele, e é o que o zipalign
            # depois realinha.
            new_lho = fout.tell()
            if new is None:
                # Copia o registro INTEIRO: header local + nome + extra +
                # dados. Copiar só os dados (o erro óbvio, e o que este
                # código fazia antes) produz um zip que o zipfile lista e não
                # consegue ler — o sintoma aparece só na leitura, longe do
                # ponto do erro.
                fin.seek(lh_start)
                remaining = (data_start - lh_start) + csize
                while remaining > 0:
                    chunk = fin.read(min(1 << 22, remaining))
                    if not chunk:
                        raise ZipError("entrada %s truncada" % e["name"])
                    fout.write(chunk)
                    remaining -= len(chunk)
                rec = dict(e)
            else:
                comp = zlib.compressobj(9, zlib.DEFLATED, -15) if e["method"] == 8 else None
                payload = comp.compress(new) + comp.flush() if comp else new
                crc = binascii.crc32(new) & 0xFFFFFFFF
                name_bytes = e["name"].encode("utf-8")
                lh = struct.pack("<IHHHHHIIIHH", LFH_SIG, e["ver_need"], e["flags"],
                                 e["method"], e["mtime"], e["mdate"], crc,
                                 len(payload), len(new), len(name_bytes), 0)
                fout.write(lh)
                fout.write(name_bytes)
                fout.write(payload)
                rec = dict(e)
                rec.update({"crc": crc, "csize": len(payload), "usize": len(new)})
            rec["new_lho"] = new_lho
            out_entries.append(rec)
            pos = data_start + csize
        cd_off = fout.tell()
        for rec in out_entries:
            nlen = len(rec["name"].encode("utf-8"))
            elen = len(rec["extra"])
            clen = len(rec["comment"])
            fout.write(struct.pack("<IHHHHHHIIIHHHHHII", CDH_SIG, rec["ver_made"],
                                   rec["ver_need"], rec["flags"], rec["method"],
                                   rec["mtime"], rec["mdate"], rec["crc"], rec["csize"],
                                   rec["usize"], nlen, elen, clen, 0, rec["iattr"],
                                   rec["eattr"], rec["new_lho"]))
            fout.write(rec["name"].encode("utf-8"))
            fout.write(rec["extra"])
            fout.write(rec["comment"])
        cd_size = fout.tell() - cd_off
        fout.write(struct.pack("<IHHHHIIH", EOCD_SIG, 0, 0, len(out_entries),
                               len(out_entries), cd_size, cd_off, len(comment)))
        fout.write(comment)
    return {"entries": len(out_entries), "cd_off": cd_off, "cd_size": cd_size}


# ------------------------------------------------------------- etapas -----
XD_CONFIG = "assets/XDConfig.json"
MANIFEST = "AndroidManifest.xml"
# Carregadores de plugin do TapSDK/XDSDK que são telemetria pura. A lista vem
# do manifest do próprio APK (aapt2 dump xmltree): ao lado destes estão
# login, anti-addiction, momento, conta, pagamento, share, conquistas e
# anúncio, e nenhum deles entra aqui.
TELEMETRY_LOADERS = [
    "com.tapsdk.tapdb.loader",   # TapDB: analytics
    "com.xd.third.track.loader",  # Adjust/AppsFlyer: atribuição
]
# NUNCA tocar: o que quebraria o jogo ou a conta se sumisse.
LOADERS_PARA_NUNCA_TOCAR = [
    "com.tapsdk.antiaddiction.loader",
    "com.xd.third.login.loader",
    "com.xd.intl.payment.loader",
    "com.tapsdk.moment.loader",
    "com.xd.intl.account.loader",
    "com.tapsdk.achievement.loader",
    "com.tapsdk.billboard.loader",  # anúncio: isso é a etapa noads, não esta
    "com.xd.share.loader",
]


def _set_tapsdk_db_enable(text, enable):
    """db_config.enable -> enable, sem reescrever o resto do JSON.

    Trocar de texto (buscar o valor) em vez de json.loads+dumps de propósito:
    o arquivo original tem indentação e ordem próprias, e um round-trip de
    JSON mudaria o arquivo inteiro por causa de uma chave — o diff do APK
    tem que mostrar uma linha, não o arquivo reformatado.
    """
    lines = text.split("\n")
    hits = 0
    out = []
    in_db = False
    depth = 0
    for line in lines:
        stripped = line.strip()
        if '"db_config"' in stripped:
            in_db = True
            depth = 0
        if in_db and '"enable"' in stripped:
            indent = line[:len(line) - len(line.lstrip())]
            tail = "true" if enable else "false"
            if "true" in stripped:
                line = indent + '"enable": %s,' % tail
                hits += 1
            elif "false" in stripped:
                line = indent + '"enable": %s,' % tail
                hits += 1
        if in_db and stripped in ("}", "},", "}"):
            in_db = False
        out.append(line)
    if hits != 1:
        raise ValueError("XDConfig.json: esperado exatamente 1 chave enable em "
                         "db_config, achei %d" % hits)
    return "\n".join(out)


def stage_telemetry_config(read_entry):
    """Etapa 1a — só o config: tapsdk.db_config.enable -> false.

    Isolada da etapa 1b porque as DUAS precisam ser testadas sozinhas: no
    device, a union delas quebrou o login (o TapDB fornece o did que a conta
    usa) e só o device diz qual das duas é a culpada. Etapa independente,
    ligável e desligável, como as regras pedem.
    """
    raw = read_entry(XD_CONFIG)
    new_text = _set_tapsdk_db_enable(raw.decode("utf-8"), False)
    return [(XD_CONFIG, new_text.encode("utf-8"),
             "tapsdk.db_config.enable true->false (TapDB/analytics)")]


def stage_telemetry_plugins(read_entry):
    """Etapa 1b — só o manifest: os dois carregadores de telemetria -> false."""
    ax = Axml(read_entry(MANIFEST))
    for loader in TELEMETRY_LOADERS:
        if loader in LOADERS_PARA_NUNCA_TOCAR:
            raise ValueError("%s está na lista de nunca tocar: a etapa "
                             "telemetria e a etapa noads precisam ser "
                             "independentes" % loader)
        was = ax.set_meta_data_boolean(loader, False)
        if was is not True:
            raise ValueError("meta-data %s já não era true: o APK de entrada "
                             "não é o esperado (o lock confere o sha256, mas "
                             "confira a versão)" % loader)
    return [(MANIFEST, bytes(ax.data),
             "meta-data %s true->false (TapDB e atribuição desligados)"
             % ", ".join(TELEMETRY_LOADERS))]


def stage_telemetry_track_plugin(read_entry):
    """Etapa 1c — SÓ o plugin de atribuição (Adjust/AppsFlyer) desligado.

    É a etapa que sobrou depois de medir: `telemetry-config` e
    `telemetry-plugins` quebram o login (o TapDB gera o did que a conta usa,
    e as duas mexem nele). Esta só desliga com.xd.third.track.loader, que é
    o carregador do com.xd.thrid.track.* (XDGThirdTrack, XDThirdTrackCore,
    AdjustLifecycleCallbacks) e não tem relação com o did. O device é quem
    confirma; até lá o status é "candidato medido só por código".
    """
    ax = Axml(read_entry(MANIFEST))
    loader = "com.xd.third.track.loader"
    if ax.set_meta_data_boolean(loader, False) is not True:
        raise ValueError("meta-data %s já não era true: o APK de entrada não "
                         "é o esperado" % loader)
    return [(MANIFEST, bytes(ax.data),
             "meta-data %s true->false (plugin de atribuição)" % loader)]


def stage_telemetry(read_entry):
    """Etapa 1 — as duas metades juntas.

    MEDIDO NO DEVICE (2026-09-28, POCO C75): este conjunto QUEBRA O LOGIN. O
    TapDB é quem gera o did, e o did é a identidade que a conta TDS usa — com
    as duas metades ligadas o jogo fica na splash com
    "fetch tap session token result error: did: did can not be empty" e
    HTTP 400 em toda requisição da conta. Está aqui porque é a união
    documentada e é o que o teste de host confere; para uso real, veja
    qual metade o device aceitou (README e relatório). A trava de "uma etapa
    por arquivo" impede de pedir `telemetry` junto de `telemetry-config`.
    """
    return stage_telemetry_config(read_entry) + stage_telemetry_plugins(read_entry)


STAGES = {
    "telemetry-config": {
        "fn": stage_telemetry_config,
        "doc": "SÓ assets/XDConfig.json: tapsdk.db_config.enable true->false. "
               "Etapa isolada porque ela mexe no did da conta: no device, "
               "together com a de plugins, o login parou (medido no device).",
    },
    "telemetry-plugins": {
        "fn": stage_telemetry_plugins,
        "doc": "SÓ AndroidManifest.xml: com.tapsdk.tapdb.loader e "
               "com.xd.third.track.loader true->false (não carrega os plugins "
               "de analytics nem de atribuição).",
    },
    "telemetry-track-plugin": {
        "fn": stage_telemetry_track_plugin,
        "doc": "SÓ o meta-data com.xd.third.track.loader true->false: não "
               "carrega o plugin de atribuição (Adjust/AppsFlyer) e não toca o "
               "TapDB, que é quem dá o did da conta. Candidato a telemetria "
               "com login intacto — pendente de prova no device.",
    },
    "telemetry": {
        "fn": stage_telemetry,
        "doc": "UNião das duas metades (config + plugins). MEDIDO: juntas "
               "quebram o login (did vazio), porque o TapDB gera a identidade "
               "que a conta usa. Para uso real, use telemetry-config ou "
               "telemetry-plugins — o device diz qual delas o jogo aceita.",
    },
    "noads": {
        "fn": None,
        "doc": "PENDENTE (D3): desligar com.tapsdk.billboard.loader + o fetch do "
               "marquee, sem conceder recompensa sem anúncio. Etapa própria, "
               "testada sozinha.",
    },
    "perf": {
        "fn": None,
        "doc": "PENDENTE (D4): render/escala/sombras/qualidade, sem tocar simulação.",
    },
}


# --------------------------------------------------------------- lock -----
def load_lock():
    if not os.path.exists(LOCK_PATH):
        raise SystemExit("ERRO: %s ausente" % LOCK_PATH)
    lock = {}
    with open(LOCK_PATH, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            k, _, v = line.partition("=")
            lock[k.strip()] = v.strip()
    return lock


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 22), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser(description="patcher do APK do TABS")
    ap.add_argument("--apk", help="base.apk original (fora do git)")
    ap.add_argument("--out", help="APK de saída")
    ap.add_argument("--stages", default="telemetry",
                    help="etapas separadas por vírgula (padrão: telemetry)")
    ap.add_argument("--check", action="store_true",
                    help="só confere o lock e mostra o que seria mudado")
    ap.add_argument("--list-stages", action="store_true")
    ap.add_argument("--report", help="grava o relatório de mudanças em JSON")
    ap.add_argument("--install-location", choices=("keep", "internal"),
                    default="keep",
                    help="mantém o installLocation do APK (keep) ou força "
                         "internal. NÃO é mod: é opção de instalação local. O "
                         "original pede preferExternal e, num aparelho com "
                         "/data apertado, o installd falha com 'Failed to "
                         "override installation location'.")
    ap.add_argument("--ignore-lock", action="store_true",
                    help="confere mesmo assim (só para APK de teste)")
    args = ap.parse_args()

    if args.list_stages:
        for name, st in STAGES.items():
            print("%-10s %s %s" % (name, "[pronto]" if st["fn"] else "[pendente]", st["doc"]))
        return 0

    names = [s.strip() for s in args.stages.split(",") if s.strip()]
    if not names:
        print("ERRO: --stages vazio", file=sys.stderr)
        return 2
    for n in names:
        if n not in STAGES:
            print("ERRO: etapa desconhecida: %s (use --list-stages)" % n, file=sys.stderr)
            return 2
        if STAGES[n]["fn"] is None:
            print("ERRO: etapa %s ainda não implementada: %s" % (n, STAGES[n]["doc"]),
                  file=sys.stderr)
            return 2

    lock = load_lock()
    if not args.check and not args.apk:
        print("ERRO: --apk é obrigatório (use --check para só conferir)", file=sys.stderr)
        return 2

    report = {"stages": names, "apk": None, "apk_sha256": None,
              "lock_sha256": lock.get("apk-sha256"), "changes": []}

    if args.apk:
        got = sha256_file(args.apk)
        report["apk"] = os.path.abspath(args.apk)
        report["apk_sha256"] = got
        want = lock.get("apk-sha256")
        if got != want and not args.ignore_lock:
            print("ERRO: sha256 do APK de entrada não bate com tools/tabs_apk.lock\n"
                  "  entrada: %s\n  lock:    %s\n"
                  "Se o APK mudou de verdade, atualize o lock com o sha novo e "
                  "justifique no relatório — não é para pular a conferência."
                  % (got, want), file=sys.stderr)
            return 1
        if got != want:
            report["lock_mismatch_ignored"] = True

    if args.check:
        print("etapas: %s" % ", ".join(names))
        for n in names:
            print("  %s: %s" % (n, STAGES[n]["doc"]))
        print("apk: %s" % (args.apk or "(não informado)"))
        print("lock: %s" % lock.get("apk-sha256"))
        return 0

    import zipfile
    replacements = {}
    zf = zipfile.ZipFile(args.apk)

    def read_entry(name):
        return zf.read(name)

    for n in names:
        try:
            stage_changes = STAGES[n]["fn"](read_entry)
        except (ValueError, AxmlError, KeyError, zipfile.BadZipFile) as e:
            # Mensagem limpa, não traceback: quem roda isto é uma pessoa com
            # um APK de 1,4 GB na mão, e "FileNotFoundError" numa linha de
            # walk de zip não diz nada sobre o que ela fez de errado.
            print("ERRO na etapa %s: %s" % (n, e), file=sys.stderr)
            return 1
        for entry, data, why in stage_changes:
            # Trava de estágio: duas etapas no mesmo arquivo, cada uma tocando
            # sua parte, é erro de projeto (uma depende da outra) — não uma
            # merger de patch.
            if entry in replacements:
                print("ERRO: duas etapas mexem em %s; stages têm que ser "
                      "independentes para poderem ser testadas sozinhas" % entry,
                      file=sys.stderr)
                return 1
            replacements[entry] = data
            report["changes"].append({
                "stage": n, "entry": entry,
                "sha256_before": hashlib.sha256(read_entry(entry)).hexdigest(),
                "sha256_after": hashlib.sha256(data).hexdigest(),
                "bytes": len(data), "why": why,
            })
    if args.install_location == "internal":
        cur = replacements.get(MANIFEST)
        ax = Axml(cur if cur is not None else zf.read(MANIFEST))
        old = ax.set_attr_enum("manifest", "installLocation", 1, expected=2)
        replacements[MANIFEST] = bytes(ax.data)
        report["install_location"] = {"from": old, "to": 1}
        print("opção de instalação: android:installLocation %d -> 1 (internal)" % old)
    zf.close()

    stats = rewrite_zip(args.apk, args.out, replacements)
    report["output"] = os.path.abspath(args.out)
    report["output_sha256"] = sha256_file(args.out)
    report["zip"] = stats
    if args.report:
        with open(args.report, "w", encoding="utf-8") as f:
            json.dump(report, f, indent=2, ensure_ascii=False)
    for c in report["changes"]:
        print("%-10s %s (%d bytes) — %s" % (c["stage"], c["entry"], c["bytes"], c["why"]))
    print("saida: %s (%d entradas, sha256 %s)" % (args.out, stats["entries"],
                                                  report["output_sha256"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
