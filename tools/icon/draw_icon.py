#!/usr/bin/env python3
"""
Draws the AmiRocket icon artwork.

An original design: a rocket climbing out of a speech bubble, its exhaust
drawn as a cone of horizontal "copper bars" (the raster-bar effect of Amiga
demos). Rendered at 8x and scaled down, so the result is a clean 46x46
image with transparency, plus a highlighted variant for the selected state.

  python3 draw_icon.py OUTDIR      -> OUTDIR/normal.png, selected.png, preview.png
"""
import math
import os
import sys

from PIL import Image, ImageDraw, ImageEnhance

SIZE = 46
S = 8                      # supersampling factor
W = SIZE * S

# copper bar colours, bottom of the cone to the top (classic raster rainbow)
COPPER = [(170, 30, 200), (40, 70, 230), (20, 170, 230), (30, 200, 90),
          (240, 220, 40), (250, 140, 20), (235, 50, 30)]


def shade(c, f):
    return tuple(max(0, min(255, int(v * f))) for v in c)


def copper_bar(img, y0, y1, x0, x1, colour):
    """One raster bar: dark edges, bright middle, like a copper gradient."""
    d = ImageDraw.Draw(img)
    h = y1 - y0
    for y in range(y0, y1):
        t = (y - y0) / max(1, h - 1)
        f = 0.45 + 0.75 * math.sin(math.pi * t)      # 0.45 .. 1.2
        d.line([(x0, y), (x1, y)], fill=shade(colour, f) + (255,))


def draw(selected=False):
    img = Image.new("RGBA", (W, W), (0, 0, 0, 0))

    # --- exhaust: cone of copper bars, narrow under the rocket, wide below
    cone = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    top, bottom = int(W * 0.50), int(W * 0.97)
    cx_top, cx_bot = int(W * 0.47), int(W * 0.30)
    bars = len(COPPER)
    bar_h = (bottom - top) / bars
    for i, col in enumerate(reversed(COPPER)):         # red at the top
        y0 = int(top + i * bar_h)
        y1 = int(top + (i + 1) * bar_h) - S // 2
        t = (i + 0.5) / bars
        cx = cx_top + (cx_bot - cx_top) * t
        half = W * (0.05 + 0.30 * t)
        copper_bar(cone, y0, y1, int(cx - half), int(cx + half), col)
    # round the cone's sides off with a soft mask
    mask = Image.new("L", (W, W), 0)
    md = ImageDraw.Draw(mask)
    md.polygon([(cx_top - W * 0.05, top), (cx_top + W * 0.05, top),
                (cx_bot + W * 0.36, bottom), (cx_bot - W * 0.36, bottom)], fill=255)
    img.paste(cone, (0, 0), Image.composite(cone, Image.new("RGBA", (W, W)), mask).split()[3])

    # --- speech bubble (top left) with three dots
    d = ImageDraw.Draw(img)
    bx0, by0, bx1, by1 = int(W * 0.03), int(W * 0.05), int(W * 0.47), int(W * 0.36)
    outline = (20, 20, 40, 255)
    d.rounded_rectangle([bx0, by0, bx1, by1], radius=int(W * 0.08), fill=outline)
    d.polygon([(bx0 + W * 0.08, by1 - S), (bx0 + W * 0.20, by1 - S), (bx0 + W * 0.05, by1 + W * 0.09)], fill=outline)
    i = S * 2
    d.rounded_rectangle([bx0 + i, by0 + i, bx1 - i, by1 - i], radius=int(W * 0.06), fill=(250, 250, 255, 255))
    d.polygon([(bx0 + W * 0.09, by1 - S - i), (bx0 + W * 0.17, by1 - S - i), (bx0 + W * 0.075, by1 + W * 0.05)],
              fill=(250, 250, 255, 255))
    # three dots in copper-bar colours, each with a small highlight
    r = W * 0.034
    for k, col in enumerate([(235, 50, 30), (240, 200, 30), (40, 90, 230)]):
        x = bx0 + (bx1 - bx0) * (0.28 + 0.22 * k)
        y = (by0 + by1) / 2
        d.ellipse([x - r, y - r, x + r, y + r], fill=col + (255,))
        d.ellipse([x - r * 0.55, y - r * 0.6, x - r * 0.05, y - r * 0.1], fill=(255, 255, 255, 255))

    # --- rocket, drawn upright on its own layer and tilted 30 degrees
    rk = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    rd = ImageDraw.Draw(rk)
    cx, cy = W / 2, W / 2
    bw, bh = W * 0.15, W * 0.46           # body half-width and height
    ytop, ybot = cy - bh * 0.55, cy + bh * 0.45
    red = (225, 40, 35, 255) if not selected else (255, 80, 60, 255)
    dark = (20, 20, 40, 255)
    body = (238, 238, 245, 255) if not selected else (255, 255, 255, 255)
    flame_o = (255, 150, 20, 255)
    flame_i = (255, 240, 120, 255)
    # flame
    rd.polygon([(cx - bw * 0.6, ybot), (cx + bw * 0.6, ybot), (cx, ybot + bh * (0.55 if selected else 0.40))], fill=flame_o)
    rd.polygon([(cx - bw * 0.3, ybot), (cx + bw * 0.3, ybot), (cx, ybot + bh * (0.36 if selected else 0.24))], fill=flame_i)
    # fins
    o = S * 1.5
    for sgn in (-1, 1):
        fin = [(cx + sgn * bw * 0.8, ybot - bh * 0.35), (cx + sgn * bw * 1.9, ybot + bh * 0.05),
               (cx + sgn * bw * 1.9, ybot + bh * 0.18), (cx + sgn * bw * 0.8, ybot)]
        rd.polygon([(x + sgn * 0, y) for x, y in fin], fill=dark)
        inner = [(cx + sgn * bw * 0.85, ybot - bh * 0.30), (cx + sgn * (bw * 1.9 - o), ybot + bh * 0.06),
                 (cx + sgn * (bw * 1.9 - o), ybot + bh * 0.13), (cx + sgn * bw * 0.85, ybot - o / 2)]
        rd.polygon(inner, fill=red)
    # body with outline
    rd.rounded_rectangle([cx - bw, ytop + bh * 0.15, cx + bw, ybot], radius=bw * 0.5, fill=dark)
    rd.rounded_rectangle([cx - bw + o, ytop + bh * 0.15 + o, cx + bw - o, ybot - o], radius=bw * 0.4, fill=body)
    # nose cone
    rd.polygon([(cx - bw, ytop + bh * 0.30), (cx + bw, ytop + bh * 0.30), (cx, ytop - bh * 0.18)], fill=dark)
    rd.polygon([(cx - bw + o, ytop + bh * 0.30 - o / 2), (cx + bw - o, ytop + bh * 0.30 - o / 2),
                (cx, ytop - bh * 0.18 + o * 1.6)], fill=red)
    # stripe and porthole
    rd.rectangle([cx - bw + o, ybot - bh * 0.22, cx + bw - o, ybot - bh * 0.14], fill=red)
    pr = bw * 0.62
    py = cy - bh * 0.08
    rd.ellipse([cx - pr, py - pr, cx + pr, py + pr], fill=dark)
    rd.ellipse([cx - pr + o, py - pr + o, cx + pr - o, py + pr - o], fill=(60, 150, 235, 255))
    rd.ellipse([cx - pr * 0.45, py - pr * 0.55, cx - pr * 0.05, py - pr * 0.15], fill=(210, 240, 255, 255))

    rk = rk.rotate(-30, resample=Image.BICUBIC, center=(cx, cy))
    img.alpha_composite(rk, (int(W * 0.10), int(-W * 0.06)))

    small = img.resize((SIZE, SIZE), Image.LANCZOS)
    # hard alpha: icons have one transparent colour, no partial alpha
    px = small.load()
    for y in range(SIZE):
        for x in range(SIZE):
            r_, g_, b_, a_ = px[x, y]
            px[x, y] = (r_, g_, b_, 255) if a_ >= 110 else (0, 0, 0, 0)
    if selected:
        small = ImageEnhance.Brightness(small).enhance(1.12)
    return small


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(out, exist_ok=True)
    n, s = draw(False), draw(True)
    n.save(os.path.join(out, "normal.png"))
    s.save(os.path.join(out, "selected.png"))
    # preview: both states, 6x, on Workbench grey
    pv = Image.new("RGB", (SIZE * 6 * 2 + 30, SIZE * 6 + 20), (170, 170, 170))
    pv.paste(n.resize((SIZE * 6, SIZE * 6), Image.NEAREST), (10, 10), n.resize((SIZE * 6, SIZE * 6), Image.NEAREST))
    pv.paste(s.resize((SIZE * 6, SIZE * 6), Image.NEAREST), (SIZE * 6 + 20, 10), s.resize((SIZE * 6, SIZE * 6), Image.NEAREST))
    pv.save(os.path.join(out, "preview.png"))


if __name__ == "__main__":
    main()
