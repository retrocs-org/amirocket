#!/usr/bin/env python3
"""
Draws the AmiRocket icon artwork as crisp 46x46 pixel art.

Design: a white rocket with a rainbow stripe band climbing through a dark
starry disc. The rocket is drawn at 8x, reduced to icon size and snapped to
a small fixed palette (no blended edge colours). Outlines, the disc and the
stars are then drawn directly at 1x, so every edge is a hard pixel edge.

  python3 draw_icon.py OUTDIR   -> OUTDIR/normal.png, selected.png, preview.png
"""
import os
import sys

from PIL import Image, ImageDraw

SIZE = 46
S = 8
W = SIZE * S

OUTLINE = (12, 12, 28)
SKY = (26, 32, 86)
SKY_SEL = (38, 48, 120)
SKY_RIM = (52, 66, 150)
STAR = (255, 252, 210)
WHITE = (250, 250, 255)
SHADE = (178, 186, 208)
RED = (226, 44, 38)
RED_DARK = (150, 22, 26)
WINDOW = (64, 156, 238)
WINDOW_HI = (214, 238, 255)
FLAME_O = (255, 140, 24)
FLAME_Y = (255, 232, 100)
STRIPES = [(232, 52, 34), (250, 144, 24), (242, 222, 44), (36, 184, 84), (44, 96, 232)]

PALETTE = [OUTLINE, SKY, SKY_SEL, SKY_RIM, STAR, WHITE, SHADE, RED, RED_DARK, WINDOW, WINDOW_HI,
           FLAME_O, FLAME_Y] + STRIPES


def nearest(c):
    r, g, b = c[:3]
    return min(PALETTE, key=lambda p: (p[0] - r) ** 2 * 3 + (p[1] - g) ** 2 * 4 + (p[2] - b) ** 2 * 2)


def rocket_layer(selected):
    """Rocket without outline, upright at 8x, then tilted 35 degrees."""
    img = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    cx, cy = W * .50, W * .44
    bw = W * .118
    bh = bw * 5.0
    top, bot = cy - bh / 2, cy + bh / 2

    # flame
    fl = 2.6 if selected else 1.9
    d.polygon([(cx - bw * .62, bot - S), (cx + bw * .62, bot - S), (cx, bot + bw * fl)], fill=FLAME_O)
    d.polygon([(cx - bw * .32, bot - S), (cx + bw * .32, bot - S), (cx, bot + bw * fl * .62)], fill=FLAME_Y)
    # fins
    for s in (-1, 1):
        d.polygon([(cx + s * bw * .75, bot - bw * 1.55), (cx + s * bw * 1.95, bot + bw * .25),
                   (cx + s * bw * 1.95, bot + bw * .62), (cx + s * bw * .75, bot - bw * .05)],
                  fill=RED if s < 0 else RED_DARK)
    # body with a shaded right half
    d.rounded_rectangle([cx - bw, top + bw * .9, cx + bw, bot], radius=bw * .55, fill=WHITE)
    d.rounded_rectangle([cx + bw * .35, top + bw * .9, cx + bw, bot], radius=bw * .5, fill=SHADE)
    d.rectangle([cx + bw * .35, top + bw * .9, cx + bw * .5, bot], fill=SHADE)
    # rainbow band
    band_top = bot - bw * 1.45
    h = (bw * 0.95) / len(STRIPES)
    for i, c in enumerate(STRIPES):
        d.rectangle([cx - bw, band_top + i * h, cx + bw, band_top + (i + 1) * h], fill=c)
    # nose cone, split light/dark
    d.polygon([(cx - bw, top + bw * 1.35), (cx, top - bw * .95), (cx, top + bw * 1.35)], fill=RED)
    d.polygon([(cx, top - bw * .95), (cx + bw, top + bw * 1.35), (cx, top + bw * 1.35)], fill=RED_DARK)
    # porthole
    pr, py = bw * .62, top + bw * 2.25
    d.ellipse([cx - pr, py - pr, cx + pr, py + pr], fill=OUTLINE)
    d.ellipse([cx - pr * .62, py - pr * .62, cx + pr * .62, py + pr * .62], fill=WINDOW)
    d.ellipse([cx - pr * .45, py - pr * .45, cx - pr * .02, py - pr * .02], fill=WINDOW_HI)
    return img.rotate(-35, resample=Image.NEAREST, center=(cx, W * .5))


def reduce(layer):
    """8x layer -> 46x46: majority alpha, colour snapped to the palette."""
    small = layer.resize((SIZE, SIZE), Image.BOX)
    out = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    sp, op = small.load(), out.load()
    for y in range(SIZE):
        for x in range(SIZE):
            r, g, b, a = sp[x, y]
            if a >= 128:
                # un-premultiply the averaged edge colour before snapping
                op[x, y] = nearest((r, g, b)) + (255,)
    return out


def outline(img, colour):
    """1-pixel hard outline around every opaque pixel (8-neighbour)."""
    px = img.load()
    add = []
    for y in range(SIZE):
        for x in range(SIZE):
            if px[x, y][3]:
                continue
            for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1), (-1, -1), (1, -1), (-1, 1), (1, 1)):
                nx, ny = x + dx, y + dy
                if 0 <= nx < SIZE and 0 <= ny < SIZE and px[nx, ny][3]:
                    add.append((x, y))
                    break
    for x, y in add:
        px[x, y] = colour + (255,)


def draw(selected=False):
    icon = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    d = ImageDraw.Draw(icon)
    # sky disc: outline, rim highlight, fill (all drawn at 1x: hard edges)
    d.ellipse([1, 1, SIZE - 2, SIZE - 2], fill=OUTLINE + (255,))
    d.ellipse([2, 2, SIZE - 3, SIZE - 3], fill=SKY_RIM + (255,))
    d.ellipse([3, 3, SIZE - 4, SIZE - 4], fill=(SKY_SEL if selected else SKY) + (255,))
    # stars: single pixels and two small crosses
    for x, y in [(9, 13), (35, 9), (12, 33), (38, 30), (23, 6), (7, 23), (30, 40), (40, 20)]:
        icon.putpixel((x, y), STAR + (255,))
    for x, y in [(14, 9), (36, 36)]:
        for dx, dy in ((0, 0), (1, 0), (-1, 0), (0, 1), (0, -1)):
            icon.putpixel((x + dx, y + dy), STAR + (255,))

    rk = reduce(rocket_layer(selected))
    outline(rk, OUTLINE)
    icon.alpha_composite(rk)
    return icon


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(out, exist_ok=True)
    n, s = draw(False), draw(True)
    n.save(os.path.join(out, "normal.png"))
    s.save(os.path.join(out, "selected.png"))
    k = 6
    pv = Image.new("RGB", (SIZE * k * 2 + SIZE * 2 + 70, SIZE * k + 20), (170, 170, 170))
    for i, im in enumerate((n, s)):
        big = im.resize((SIZE * k, SIZE * k), Image.NEAREST)
        pv.paste(big, (10 + i * (SIZE * k + 20), 10), big)
    for i, im in enumerate((n, s)):
        pv.paste(im, (SIZE * k * 2 + 50 + i * (SIZE + 10), SIZE * k - SIZE), im)
    pv.save(os.path.join(out, "preview.png"))


if __name__ == "__main__":
    main()
