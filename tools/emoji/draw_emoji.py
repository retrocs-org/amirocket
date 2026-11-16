#!/usr/bin/env python3
"""
Draws AmiRocket's pixel-art emoji (an original set) and writes them as a C
source with one palette-indexed bitmap per emoji and size.

  python3 draw_emoji.py src/amiga/emoji_data.c preview.png
"""
import math
import os
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(__file__))
from emoji_list import EMOJI

SIZES = [8, 10, 12, 14, 16]
C = 128                       # drawing canvas
PAL = [None,                  # 0 = transparent
       (90, 55, 10),          # 1 outline
       (252, 200, 50),        # 2 yellow
       (255, 255, 255),       # 3 white
       (225, 45, 45),         # 4 red
       (60, 130, 230),        # 5 blue
       (245, 140, 30),        # 6 orange
       (60, 170, 70),         # 7 green
       (110, 110, 120),       # 8 grey
       (245, 120, 150),       # 9 pink
       (130, 200, 255),       # 10 light blue
       (140, 85, 40),         # 11 brown
       (30, 30, 40)]          # 12 dark

O, Y, W, R, B, OR, G, GR, PK, LB, BR, DK = [PAL[i] for i in range(1, 13)]
LW = 7                        # outline width on the canvas


def canvas():
    im = Image.new("RGBA", (C, C), (0, 0, 0, 0))
    return im, ImageDraw.Draw(im)


def face(d, color=Y):
    d.ellipse([6, 6, C - 7, C - 7], fill=O)
    d.ellipse([6 + LW, 6 + LW, C - 7 - LW, C - 7 - LW], fill=color)


def dot_eyes(d, y=50, r=8, dx=22):
    for x in (64 - dx, 64 + dx):
        d.ellipse([x - r, y - r * 1.3, x + r, y + r * 1.3], fill=O)


def happy_eyes(d, y=52, dx=22):
    for x in (64 - dx, 64 + dx):
        d.arc([x - 13, y - 10, x + 13, y + 14], 200, 340, fill=O, width=LW)


def smile(d, top=62, w=34, h=28):
    d.arc([64 - w, top - h, 64 + w, top + h], 25, 155, fill=O, width=LW)


def grin(d, top=70, w=34, h=26, teeth=True):
    d.chord([64 - w, top - h, 64 + w, top + h], 0, 180, fill=O)
    if teeth:
        d.chord([64 - w + 7, top - h + 7, 64 + w - 7, top + h - 9], 0, 180, fill=W)


def frown(d, y=94, w=26):
    d.arc([64 - w, y - 4, 64 + w, y + 40], 205, 335, fill=O, width=LW)


def flat(d, y=88, w=22):
    d.line([64 - w, y, 64 + w, y], fill=O, width=LW)


def tear(d, x, y, big=False):
    s = 12 if big else 8
    d.polygon([(x, y - s * 1.6), (x - s, y + s * .4), (x + s, y + s * .4)], fill=LB)
    d.ellipse([x - s, y - s * .4, x + s, y + s * 1.6], fill=LB)


def heart(d, cx, cy, s, color=R):
    d.ellipse([cx - s, cy - s * .8, cx, cy + s * .2], fill=color)
    d.ellipse([cx, cy - s * .8, cx + s, cy + s * .2], fill=color)
    d.polygon([(cx - s * .98, cy - s * .2), (cx + s * .98, cy - s * .2), (cx, cy + s)], fill=color)


def hand(d, x0, y0, x1, y1):
    d.rounded_rectangle([x0, y0, x1, y1], radius=14, fill=O)
    d.rounded_rectangle([x0 + LW, y0 + LW, x1 - LW, y1 - LW], radius=10, fill=Y)


def draw(name):
    im, d = canvas()
    if name in ("smile", "grinning", "smiley", "grin", "joy", "laughing", "sweat_smile", "rofl",
                "slight_smile", "blush", "wink", "innocent", "heart_eyes", "kissing_heart",
                "stuck_out_tongue", "stuck_out_tongue_winking_eye", "sunglasses", "thinking",
                "neutral_face", "confused", "upside_down", "rolling_eyes", "slight_frown", "cry",
                "sob", "rage", "angry", "open_mouth", "scream", "partying_face"):
        face(d, R if name == "rage" else Y)
    if name in ("smile", "laughing", "joy", "rofl", "sweat_smile"):
        happy_eyes(d)
        grin(d)
        if name == "joy" or name == "rofl":
            tear(d, 30, 62, True)
            tear(d, 98, 62, True)
        if name == "sweat_smile":
            tear(d, 100, 34)
    elif name in ("grinning", "smiley", "grin"):
        dot_eyes(d)
        grin(d)
    elif name in ("slight_smile", "blush"):
        dot_eyes(d)
        smile(d)
        if name == "blush":
            d.ellipse([22, 70, 42, 82], fill=PK)
            d.ellipse([86, 70, 106, 82], fill=PK)
    elif name == "wink":
        d.ellipse([34, 40, 50, 60], fill=O)
        d.arc([74, 42, 102, 64], 200, 340, fill=O, width=LW)
        smile(d)
    elif name == "innocent":
        dot_eyes(d)
        smile(d)
        d.ellipse([28, -4, 100, 18], outline=LB, width=8)
    elif name == "heart_eyes":
        heart(d, 42, 50, 16)
        heart(d, 86, 50, 16)
        grin(d, teeth=False)
    elif name == "kissing_heart":
        dot_eyes(d)
        d.ellipse([56, 80, 72, 96], outline=O, width=LW)
        heart(d, 100, 90, 14)
    elif name in ("stuck_out_tongue", "stuck_out_tongue_winking_eye"):
        if name == "stuck_out_tongue":
            dot_eyes(d)
        else:
            d.ellipse([34, 40, 50, 60], fill=O)
            d.line([76, 42, 98, 52, 76, 62], fill=O, width=LW)
        d.line([40, 80, 88, 80], fill=O, width=LW)
        d.rounded_rectangle([50, 80, 78, 108], radius=12, fill=PK)
    elif name == "sunglasses":
        d.rounded_rectangle([20, 38, 58, 62], radius=8, fill=DK)
        d.rounded_rectangle([70, 38, 108, 62], radius=8, fill=DK)
        d.line([56, 46, 72, 46], fill=DK, width=6)
        smile(d)
    elif name == "thinking":
        dot_eyes(d)
        flat(d, 86, 16)
        hand(d, 60, 88, 104, 120)
    elif name == "neutral_face":
        dot_eyes(d)
        flat(d)
    elif name == "confused":
        dot_eyes(d)
        d.line([44, 92, 84, 82], fill=O, width=LW)
    elif name == "upside_down":
        dot_eyes(d, y=78)
        d.arc([30, 18, 98, 74], 205, 335, fill=O, width=LW)
    elif name == "rolling_eyes":
        for x in (42, 86):
            d.ellipse([x - 15, 34, x + 15, 66], fill=W, outline=O, width=4)
            d.ellipse([x - 7, 36, x + 7, 50], fill=O)
        flat(d, 90, 14)
    elif name == "slight_frown":
        dot_eyes(d)
        frown(d)
    elif name in ("cry", "sob"):
        dot_eyes(d) if name == "cry" else happy_eyes(d)
        frown(d)
        tear(d, 40, 76, name == "sob")
        if name == "sob":
            tear(d, 88, 76, True)
    elif name in ("rage", "angry"):
        d.line([28, 34, 56, 46], fill=O, width=LW)
        d.line([100, 34, 72, 46], fill=O, width=LW)
        dot_eyes(d, y=56)
        frown(d, 96)
    elif name in ("open_mouth", "scream"):
        dot_eyes(d, y=46)
        d.ellipse([48, 70, 80, 110], fill=O)
        if name == "scream":
            d.ellipse([24, 24, 104, 40], fill=LB)
            d.ellipse([18, 70, 30, 100], fill=W)
            d.ellipse([98, 70, 110, 100], fill=W)
    elif name == "partying_face":
        happy_eyes(d)
        smile(d)
        d.polygon([(64, -4), (40, 30), (88, 30)], fill=B)
        d.polygon([(64, -4), (52, 14), (76, 14)], fill=OR)
        d.line([90, 92, 120, 80], fill=OR, width=8)
        for x, y, c in [(14, 30, R), (112, 26, G), (20, 104, B)]:
            d.ellipse([x - 5, y - 5, x + 5, y + 5], fill=c)
    elif name == "heart":
        heart(d, 64, 60, 56)
        heart(d, 64, 60, 50, R)
        d.ellipse([28, 30, 42, 44], fill=(255, 150, 150))
    elif name in ("thumbsup", "thumbsdown"):
        hand(d, 26, 54, 102, 120)
        hand(d, 44, 6, 72, 66)
        for y in (72, 88, 104):
            d.line([60, y, 98, y], fill=O, width=4)
        if name == "thumbsdown":
            im = im.rotate(180)
    elif name == "ok_hand":
        hand(d, 46, 50, 110, 122)
        d.ellipse([10, 30, 62, 82], outline=O, width=12)
        d.ellipse([16, 36, 56, 76], outline=Y, width=4)
        for x in (62, 80, 98):
            hand(d, x - 4, 6, x + 14, 60)
    elif name == "clap":
        hand(d, 14, 20, 70, 112)
        hand(d, 54, 12, 112, 104)
        for a in (-30, 0, 30):
            x = 64 + 54 * math.sin(math.radians(a))
            d.line([x, 0, x, 8], fill=OR, width=5)
    elif name == "pray":
        d.polygon([(64, 4), (24, 110), (104, 110)], fill=O)
        d.polygon([(64, 18), (34, 104), (94, 104)], fill=Y)
        d.line([64, 18, 64, 104], fill=O, width=5)
    elif name == "wave":
        hand(d, 28, 50, 100, 122)
        for x in (32, 50, 68, 86):
            hand(d, x, 8, x + 20, 70)
        d.arc([100, 10, 126, 50], 280, 80, fill=B, width=5)
    elif name == "muscle":
        d.polygon([(10, 118), (40, 60), (70, 40), (92, 8), (116, 22), (96, 60), (110, 100), (60, 120)], fill=O)
        d.polygon([(22, 112), (46, 64), (74, 48), (94, 20), (106, 26), (86, 62), (100, 98), (60, 112)], fill=Y)
    elif name == "fire":
        d.polygon([(64, 2), (100, 56), (110, 92), (90, 122), (38, 122), (18, 92), (30, 56), (48, 70)], fill=R)
        d.polygon([(64, 34), (90, 76), (92, 104), (78, 118), (50, 118), (36, 104), (40, 76), (54, 86)], fill=OR)
        d.polygon([(64, 70), (80, 100), (72, 116), (56, 116), (48, 100)], fill=Y)
    elif name == "tada":
        d.polygon([(10, 120), (40, 40), (90, 90)], fill=O)
        d.polygon([(20, 110), (42, 50), (80, 88)], fill=Y)
        d.line([30, 82, 60, 96], fill=R, width=6)
        for x, y, c in [(70, 20, R), (100, 30, B), (90, 60, G), (118, 70, OR), (60, 6, PK), (112, 100, Y)]:
            d.ellipse([x - 7, y - 7, x + 7, y + 7], fill=c)
    elif name == "rocket":
        rk = Image.new("RGBA", (C, C), (0, 0, 0, 0))
        r = ImageDraw.Draw(rk)
        r.polygon([(52, 100), (76, 100), (64, 126)], fill=OR)
        r.polygon([(44, 80), (30, 106), (52, 100)], fill=R)
        r.polygon([(84, 80), (98, 106), (76, 100)], fill=R)
        r.rounded_rectangle([44, 30, 84, 102], radius=18, fill=O)
        r.rounded_rectangle([50, 36, 78, 96], radius=14, fill=W)
        r.polygon([(44, 40), (84, 40), (64, 2)], fill=R)
        r.ellipse([54, 50, 74, 70], fill=O)
        r.ellipse([57, 53, 71, 67], fill=B)
        im.alpha_composite(rk.rotate(-40, resample=Image.BICUBIC, center=(64, 64)))
    elif name == "eyes":
        for x in (38, 90):
            d.ellipse([x - 26, 18, x + 26, 110], fill=O)
            d.ellipse([x - 20, 24, x + 20, 104], fill=W)
            d.ellipse([x - 8, 40, x + 14, 70], fill=DK)
    elif name == "100":
        d.line([16, 30, 16, 92], fill=R, width=12)
        for x in (52, 94):
            d.ellipse([x - 18, 28, x + 18, 94], outline=R, width=11)
        d.line([10, 106, 118, 102], fill=R, width=8)
        d.line([14, 120, 114, 116], fill=R, width=8)
    elif name == "star":
        pts = []
        for k in range(10):
            a = math.radians(-90 + 36 * k)
            rr = 62 if k % 2 == 0 else 26
            pts.append((64 + rr * math.cos(a), 68 + rr * math.sin(a)))
        d.polygon(pts, fill=O)
        pts2 = [(64 + (x - 64) * .84, 68 + (y - 68) * .84) for x, y in pts]
        d.polygon(pts2, fill=Y)
    elif name == "white_check_mark":
        d.rounded_rectangle([6, 6, 122, 122], radius=22, fill=G)
        d.line([30, 66, 54, 92, 100, 34], fill=W, width=16)
    elif name == "x":
        d.line([20, 20, 108, 108], fill=R, width=24)
        d.line([108, 20, 20, 108], fill=R, width=24)
    elif name == "beers":
        for x0 in (6, 62):
            d.rectangle([x0, 40, x0 + 46, 116], fill=O)
            d.rectangle([x0 + 6, 46, x0 + 40, 110], fill=OR)
            d.ellipse([x0 - 4, 24, x0 + 56, 54], fill=W)
            d.arc([x0 + 36, 56, x0 + 62, 96], 270, 90, fill=O, width=7)
    elif name == "champagne_glass":
        for x0, a in ((40, 18), (88, -18)):
            g = Image.new("RGBA", (C, C), (0, 0, 0, 0))
            gd = ImageDraw.Draw(g)
            gd.polygon([(48, 10), (80, 10), (72, 70), (56, 70)], fill=O)
            gd.polygon([(53, 30), (75, 30), (69, 64), (59, 64)], fill=Y)
            gd.line([64, 70, 64, 108], fill=O, width=6)
            gd.line([48, 112, 80, 112], fill=O, width=6)
            g = g.rotate(a, resample=Image.BICUBIC, center=(64, 64))
            im.alpha_composite(g, (int(x0 - 64), 0))
        d.line([60, 6, 68, 0], fill=OR, width=4)
    elif name == "coffee":
        d.rounded_rectangle([18, 50, 96, 118], radius=16, fill=O)
        d.rounded_rectangle([25, 57, 89, 111], radius=12, fill=W)
        d.ellipse([25, 50, 89, 70], fill=BR)
        d.arc([84, 62, 120, 100], 270, 90, fill=O, width=9)
        for x in (40, 62):
            d.arc([x - 8, 4, x + 8, 44], 90, 270, fill=GR, width=5)
    elif name == "poop":
        for (x0, y0, x1, y1) in [(14, 80, 114, 122), (26, 50, 102, 92), (40, 22, 88, 62)]:
            d.ellipse([x0, y0, x1, y1], fill=O)
            d.ellipse([x0 + 6, y0 + 6, x1 - 6, y1 - 6], fill=BR)
        d.polygon([(64, 2), (52, 28), (76, 28)], fill=BR)
        for x in (48, 80):
            d.ellipse([x - 9, 72, x + 9, 92], fill=W)
            d.ellipse([x - 4, 78, x + 4, 88], fill=DK)
    return im


def nearest(rgb):
    r, g, b = rgb
    best, bi = None, 1
    for i in range(1, len(PAL)):
        pr, pg, pb = PAL[i]
        dd = (pr - r) ** 2 * 3 + (pg - g) ** 2 * 4 + (pb - b) ** 2 * 2
        if best is None or dd < best:
            best, bi = dd, i
    return bi


def reduce(im, size):
    sm = im.resize((size, size), Image.LANCZOS)
    px = sm.load()
    out = []
    for y in range(size):
        row = []
        for x in range(size):
            r, g, b, a = px[x, y]
            if a < 120:
                row.append(0)
            else:
                f = 255.0 / a if a < 255 else 1.0   # undo premultiplied edges
                row.append(nearest((min(255, int(r * f)), min(255, int(g * f)), min(255, int(b * f)))))
        out.append(row)
    return out


def main():
    out_c = sys.argv[1]
    preview = sys.argv[2] if len(sys.argv) > 2 else None
    arts = [draw(code) for code, _, _, _ in EMOJI]
    lines = ["/* Generated by tools/emoji/draw_emoji.py - do not edit.",
             " * Original pixel-art emoji for AmiRocket, palette-indexed (0 = transparent). */",
             '#include "emoji_data.h"', ""]
    lines.append("const unsigned long emoji_palette[EMOJI_COLOURS] = {")
    lines.append("    0x000000UL, " + ", ".join("0x%02X%02X%02XUL" % p for p in PAL[1:]))
    lines.append("};")
    lines.append(f"const int emoji_sizes[EMOJI_NSIZES] = {{ {', '.join(map(str, SIZES))} }};")
    sheet = Image.new("RGB", (sum(SIZES) * 4 + 20 * len(SIZES), len(EMOJI) * 18 * 4 // 4 + 20), (170, 170, 170))
    for si, sz in enumerate(SIZES):
        lines.append(f"static const unsigned char e{sz}[{len(EMOJI)}][{sz * sz}] = {{")
        for ei, art in enumerate(arts):
            px = reduce(art, sz)
            flat = [v for row in px for v in row]
            lines.append("    { " + ",".join(map(str, flat)) + " },")
            if preview:
                for y in range(sz):
                    for x in range(sz):
                        v = px[y][x]
                        if v:
                            for k in range(4):
                                for m in range(4):
                                    xx = 10 + sum(SIZES[:si]) * 4 + si * 20 + x * 4 + m
                                    yy = 10 + ei * 18 * 4 // 4 + y * 1  # compact rows
                                    _ = (xx, yy)
        lines.append("};")
    lines.append("const unsigned char *emoji_bitmap(int idx, int size_index)")
    lines.append("{")
    lines.append(f"    if (idx < 0 || idx >= {len(EMOJI)})")
    lines.append("        return 0;")
    lines.append("    switch (size_index) {")
    for si, sz in enumerate(SIZES):
        lines.append(f"    case {si}: return e{sz}[idx];")
    lines.append("    }")
    lines.append("    return 0;")
    lines.append("}")
    open(out_c, "w").write("\n".join(lines) + "\n")

    if preview:
        cols = 10
        k = 3
        cell = (16 + 4) * k
        pv = Image.new("RGB", (cols * cell + 10, ((len(EMOJI) + cols - 1) // cols) * cell * 2 + 20), (170, 170, 170))
        for ei, art in enumerate(arts):
            for row_i, sz in enumerate((16, 10)):
                px = reduce(art, sz)
                ox = 10 + (ei % cols) * cell
                oy = 10 + (ei // cols) * cell * 2 + row_i * cell
                for y in range(sz):
                    for x in range(sz):
                        v = px[y][x]
                        if v:
                            for a in range(k):
                                for b in range(k):
                                    pv.putpixel((ox + x * k + a, oy + y * k + b), PAL[v])
        pv.save(preview)


if __name__ == "__main__":
    main()
