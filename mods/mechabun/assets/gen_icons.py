#!/usr/bin/env python3
"""Gera os 2 slots de icon estático do Mecha-Bun (cat_id 426, True Form 's'):

  uni426_s00.png  deploy icon   128x128  frame oficial tbcml uni_s.png
  udi426_s.png    upgrade icon  294x111  plate tbcml udi_s.png x3.5,
                                           crop exato do pipeline (13,1,307,112)

Fonte da geometria: tbcml game_data/cat_base/cats.py
  - get_deploy_icon_file_name :1243 / get_upgrade_icon_file_name :1240
  - janela do deploy (14,26)-(113,101) = import_enemy_deploy_icon :1371
  - plate 85x32 x3.5 + crop_upgrade_icon :1420/:1433 (crop_rect exclusivo)

Keying do fundo: flood-fill a partir das bordas (tolerante a gradiente
radial/spotlight — modelo plano falha nessa arte), estimativa local de bg
por blur mascarado, alpha smoothstep e decontaminacao de fringe.

Rodar com a venv que tem PIL:  ~/.venvs/battlecats-mod/bin/python gen_icons.py
Nao toca em nenhum arquivo do jogo — só escreve os PNGs desta pasta.
"""
import os
from collections import deque

from PIL import Image, ImageFilter

SRC = os.path.expanduser(
    "~/Downloads/fan-made-ultimate-mecha-bunbun-mkx-v0-no2s9wyw3tb41.png"
)
TPL = os.path.expanduser(
    "~/battlecats-mods/tbcml/src/tbcml/files/assets/"
)
OUT = os.path.dirname(os.path.abspath(__file__))

# janela do deploy icon (PIL crop box, right/bottom exclusivos)
WIN = (14, 26, 113, 101)  # 99 x 75
# upgrade: template 85x32 x3.5 -> colado em (13,1), crop (13,1,307,112)
UPLATE_SCALE = 3.5
UPLATE_POS = (13, 1)
UPLATE_CROP = (13, 1, 307, 112)  # crop_rect do tbcml (right/bottom exclusivos)


def dist(c1, c2):
    return sum((a - b) * (a - b) for a, b in zip(c1, c2)) ** 0.5


def bg_mask_flood(im, tol=28.0):
    """Mascara do fundo: flood-fill 4-vizinho a partir de toda a borda.
    Comparacao com o vizinho (nao com cor global) => aguenta gradiente
    radial de spotlight sem vazar pro outline escuro do sujeito."""
    w, h = im.size
    px = im.load()
    mask = bytearray(w * h)  # 1 = fundo
    q = deque()
    for x in range(w):
        for y in (0, h - 1):
            if not mask[y * w + x]:
                mask[y * w + x] = 1
                q.append((x, y))
    for y in range(h):
        for x in (0, w - 1):
            if not mask[y * w + x]:
                mask[y * w + x] = 1
                q.append((x, y))
    while q:
        x, y = q.popleft()
        c = px[x, y][:3]
        for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
            if 0 <= nx < w and 0 <= ny < h:
                i = ny * w + nx
                if not mask[i] and dist(px[nx, ny][:3], c) < tol:
                    mask[i] = 1
                    q.append((nx, ny))
    return bytes(mask), w, h


def key_subject(im):
    """Alpha suave por distancia a estimativa local de bg (blur mascarado),
    decontaminacao do fringe e crop pro bbox opaco."""
    w, h = im.size
    mask, _, _ = bg_mask_flood(im)

    mimg = Image.frombytes("L", (w, h), bytes(m * 255 for m in mask))
    # dilata 2px pra estimativa cobrir o AA da borda
    mimg = mimg.filter(ImageFilter.MaxFilter(5))
    mdil = mimg.tobytes()

    # blur PREMULTIPLICADO do bg: cor*mask e mask em canais separados,
    # normaliza depois — evita o halo escuro do blur RGBA direto sobre
    # regiao transparente (bug v2: estimativa escurecia nas bordas)
    rgb_pm = Image.new("RGB", (w, h), (0, 0, 0))
    a_pm = Image.new("L", (w, h), 0)
    spx = im.load()
    rpx, apx = rgb_pm.load(), a_pm.load()
    for y in range(h):
        for x in range(w):
            if mdil[y * w + x]:
                r, g, b, _ = spx[x, y]
                rpx[x, y] = (r, g, b)
                apx[x, y] = 255
    pr, pg, pb = rgb_pm.filter(ImageFilter.GaussianBlur(9)).split()
    pa = a_pm.filter(ImageFilter.GaussianBlur(9))
    prp, pgp, pbp, pap = pr.load(), pg.load(), pb.load(), pa.load()

    def est_at(x, y):
        """cor de bg estimada (nao-premultiplicada); (0,0,0) se sem amostra"""
        a = pap[x, y]
        if a == 0:
            return (0, 0, 0)
        return (prp[x, y] * 255 // a,
                pgp[x, y] * 255 // a,
                pbp[x, y] * 255 // a)

    # sujeito estrito (nao-flood) dilatado: regiao de vizinhanca onde o
    # resgate cinza vale (pernas/sombras conectadas) — mata ruido
    # dessaturado do vinheta longe do sujeito
    strict = Image.frombytes("L", (w, h), bytes(0 if m else 255 for m in mask))
    near = strict.filter(ImageFilter.MaxFilter(31)).tobytes()

    # zonas autoritativas: sujeito (nao-flood) = opaco; bg a mais de 3px
    # do sujeito = transparente; so a banda de transicao decide por cor
    simg = Image.frombytes("L", (w, h), bytes(0 if m else 255 for m in mask))
    band = simg.filter(ImageFilter.MaxFilter(7)).tobytes()

    alpha = bytearray(w * h)
    lo, hi = 20.0, 50.0
    for y in range(h):
        for x in range(w):
            i = y * w + x
            if not mask[i]:
                alpha[i] = 255          # sujeito: opaco autoritativo
                continue
            if not band[i]:
                # bg profundo: transparente, exceto cinza dessaturado PERTO
                # do sujeito (perna/sombra que o flood engoliu pelo
                # gradiente suave) — aí decide por cor vs estimativa
                r, g, b, _ = spx[x, y]
                if (near[i] and max(r, g, b) - min(r, g, b) < 30
                        and max(r, g, b) < 190):
                    pass  # cai no smoothstep abaixo
                else:
                    alpha[i] = 0
                    continue
            d = dist(spx[x, y][:3], est_at(x, y))
            t = (d - lo) / (hi - lo)
            t = 0.0 if t < 0 else (1.0 if t > 1 else t)
            t = t * t * (3.0 - 2.0 * t)
            alpha[i] = int(t * 255)

    amask = Image.frombytes("L", (w, h), bytes(alpha))
    amask = amask.filter(ImageFilter.MedianFilter(3))
    a2 = amask.tobytes()

    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    opx = out.load()
    for y in range(h):
        for x in range(w):
            a = a2[y * w + x]
            if a == 0:
                continue
            r, g, b, _ = spx[x, y]
            if a < 255:  # tira a mistura de bg que veio no fringe
                er, eg, eb = est_at(x, y)
                af = a / 255.0
                r = max(0, min(255, int((r - (1 - af) * er) / af)))
                g = max(0, min(255, int((g - (1 - af) * eg) / af)))
                b = max(0, min(255, int((b - (1 - af) * eb) / af)))
            opx[x, y] = (r, g, b, a)

    xs, ys = [], []
    for y in range(h):
        for x in range(w):
            if a2[y * w + x] > 10:
                xs.append(x)
                ys.append(y)
    bbox = (min(xs), min(ys), max(xs) + 1, max(ys) + 1)
    return out.crop(bbox), sum(mask)


def contain(cutout, max_w, max_h):
    w, h = cutout.size
    s = min(max_w / w, max_h / h)
    return cutout.resize((max(1, round(w * s)), max(1, round(h * s))),
                         Image.LANCZOS)


def make_deploy(cutout):
    tpl = Image.open(TPL + "uni_s.png").convert("RGBA")
    win_w = WIN[2] - WIN[0]
    win_h = WIN[3] - WIN[1]
    subj = contain(cutout, win_w - 4, win_h - 4)
    x = WIN[0] + (win_w - subj.size[0]) // 2
    y = WIN[3] - 2 - subj.size[1]  # bottom-align, margem 2px
    tpl.alpha_composite(subj, (x, y))
    return tpl, (x, y)


def make_upgrade(cutout):
    canvas = Image.new("RGBA", (512, 128), (0, 0, 0, 0))
    tw, th = round(85 * UPLATE_SCALE), round(32 * UPLATE_SCALE)
    plate = Image.open(TPL + "udi_s.png").convert("RGBA").resize(
        (tw, th), Image.LANCZOS)
    canvas.alpha_composite(plate, UPLATE_POS)
    subj = contain(cutout, 96, th - 16)
    cx = UPLATE_POS[0] + tw // 2
    cy = UPLATE_POS[1] + th // 2
    canvas.alpha_composite(subj, (cx - subj.size[0] // 2,
                                  cy - subj.size[1] // 2))
    # crop exato do pipeline tbcml (crop_rect -> right/bottom exclusivos)
    return canvas.crop(UPLATE_CROP)


def ascii_preview(img, box=None, step=3):
    w, h = img.size
    x0, y0, x1, y1 = box or (0, 0, w, h)
    px = img.load()
    rows = []
    for y in range(y0, y1, step * 2):
        row = ""
        for x in range(x0, x1, step):
            a = px[x, y][3]
            row += "#" if a > 128 else ("+" if a > 30 else ".")
        rows.append(row)
    return "\n".join(rows)


def main():
    os.makedirs(OUT, exist_ok=True)
    src = Image.open(SRC).convert("RGBA")
    print(f"fonte: {SRC} ({src.size[0]}x{src.size[1]})")
    cutout, nbg = key_subject(src)
    print(f"sujeito keyado: {cutout.size} (fundo flood: {nbg} px)")

    deploy, pos = make_deploy(cutout)
    p1 = os.path.join(OUT, "uni426_s00.png")
    deploy.save(p1)
    print(f"deploy  -> {p1} ({deploy.size[0]}x{deploy.size[1]}) "
          f"sujeito@{pos}")

    upgrade = make_upgrade(cutout)
    p2 = os.path.join(OUT, "udi426_s.png")
    upgrade.save(p2)
    print(f"upgrade -> {p2} ({upgrade.size[0]}x{upgrade.size[1]})")

    print("\npreview deploy (janela, alpha):")
    print(ascii_preview(deploy, (WIN[0], WIN[1], WIN[2], WIN[3])))
    print("\npreview upgrade (alpha):")
    print(ascii_preview(upgrade, step=6))


if __name__ == "__main__":
    main()
