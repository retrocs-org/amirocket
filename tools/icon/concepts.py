#!/usr/bin/env python3
"""Three icon concepts for AmiRocket, drawn at 8x and scaled to 46x46."""
import sys
from PIL import Image, ImageDraw

SIZE, S = 46, 8
W = SIZE * S


def finish(img):
    small = img.resize((SIZE, SIZE), Image.LANCZOS)
    px = small.load()
    for y in range(SIZE):
        for x in range(SIZE):
            r, g, b, a = px[x, y]
            px[x, y] = (r, g, b, 255) if a >= 110 else (0, 0, 0, 0)
    return small


def rocket(d, cx, cy, scale, body, accent, window, outline, flame=True, stripes=None):
    """Upright rocket centred at (cx, cy); scale = body half width."""
    bw = scale
    bh = scale * 4.2
    top, bot = cy - bh / 2, cy + bh / 2
    o = S * 1.6
    if flame:
        d.polygon([(cx - bw * .55, bot), (cx + bw * .55, bot), (cx, bot + bw * 2.0)], fill=(255, 140, 20, 255))
        d.polygon([(cx - bw * .28, bot), (cx + bw * .28, bot), (cx, bot + bw * 1.2)], fill=(255, 235, 110, 255))
    for s in (-1, 1):
        d.polygon([(cx + s * bw * .7, bot - bw * 1.6), (cx + s * bw * 1.9, bot + bw * .3),
                   (cx + s * bw * 1.9, bot + bw * .7), (cx + s * bw * .7, bot)], fill=outline)
        d.polygon([(cx + s * bw * .8, bot - bw * 1.35), (cx + s * (bw * 1.9 - o), bot + bw * .35),
                   (cx + s * (bw * 1.9 - o), bot + bw * .55), (cx + s * bw * .8, bot - o)], fill=accent)
    d.rounded_rectangle([cx - bw, top + bw * .8, cx + bw, bot], radius=bw * .6, fill=outline)
    d.rounded_rectangle([cx - bw + o, top + bw * .8 + o, cx + bw - o, bot - o], radius=bw * .5, fill=body)
    if stripes:
        h = (bw * 1.2) / len(stripes)
        y0 = bot - bw * 1.7
        for i, c in enumerate(stripes):
            d.rectangle([cx - bw + o, y0 + i * h, cx + bw - o, y0 + (i + 1) * h], fill=c + (255,))
    d.polygon([(cx - bw, top + bw * 1.3), (cx + bw, top + bw * 1.3), (cx, top - bw * .9)], fill=outline)
    d.polygon([(cx - bw + o, top + bw * 1.3 - o / 2), (cx + bw - o, top + bw * 1.3 - o / 2),
               (cx, top - bw * .9 + o * 1.7)], fill=accent)
    pr, py = bw * .55, top + bw * 2.3
    d.ellipse([cx - pr, py - pr, cx + pr, py + pr], fill=outline)
    d.ellipse([cx - pr + o, py - pr + o, cx + pr - o, py + pr - o], fill=window)
    d.ellipse([cx - pr * .5, py - pr * .55, cx - pr * .05, py - pr * .1], fill=(235, 245, 255, 255))


def concept_magicwb():
    """A: MagicWB-style: muted grey-blue palette, bevelled, 90s Workbench look."""
    img = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    lay = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    d = ImageDraw.Draw(lay)
    # drop shadow + bevelled round badge
    d.ellipse([W * .08, W * .10, W * .96, W * .98], fill=(60, 60, 70, 140))
    d.ellipse([W * .04, W * .04, W * .92, W * .92], fill=(0, 0, 0, 255))
    d.ellipse([W * .055, W * .055, W * .905, W * .905], fill=(255, 255, 255, 255))
    d.ellipse([W * .08, W * .08, W * .92 - S, W * .92 - S], fill=(123, 123, 123, 255))
    d.ellipse([W * .09, W * .09, W * .885, W * .885], fill=(102, 136, 187, 255))
    # stars
    for x, y in [(.25, .3), (.7, .25), (.3, .7), (.78, .62), (.5, .18)]:
        r = S * 1.1
        d.ellipse([W * x - r, W * y - r, W * x + r, W * y + r], fill=(255, 255, 255, 255))
    img.alpha_composite(lay)
    rk = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    rocket(ImageDraw.Draw(rk), W * .5, W * .44, W * .115, (205, 205, 205, 255), (200, 100, 70, 255),
           (102, 136, 187, 255), (0, 0, 0, 255))
    rk = rk.rotate(-35, resample=Image.BICUBIC, center=(W * .5, W * .5))
    img.alpha_composite(rk)
    return finish(img)


def concept_monitor():
    """B: retro CRT monitor with chat on screen, rocket launching out of it."""
    img = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    # monitor body
    d.rounded_rectangle([W * .02, W * .30, W * .78, W * .90], radius=W * .06, fill=(20, 20, 30, 255))
    d.rounded_rectangle([W * .02 + S * 1.5, W * .30 + S * 1.5, W * .78 - S * 1.5, W * .90 - S * 1.5],
                        radius=W * .05, fill=(214, 206, 186, 255))
    d.rectangle([W * .25, W * .90, W * .55, W * .97], fill=(20, 20, 30, 255))
    d.rectangle([W * .27, W * .90, W * .53, W * .955], fill=(190, 182, 160, 255))
    # screen
    d.rounded_rectangle([W * .09, W * .37, W * .71, W * .79], radius=W * .03, fill=(20, 20, 30, 255))
    d.rounded_rectangle([W * .11, W * .39, W * .69, W * .77], radius=W * .025, fill=(0, 85, 170, 255))
    # chat lines on screen
    for i, (x0, x1, col) in enumerate([(.15, .50, (255, 255, 255)), (.30, .65, (255, 136, 0)), (.15, .45, (255, 255, 255))]):
        y = W * (.45 + i * .09)
        d.rounded_rectangle([W * x0, y, W * x1, y + W * .05], radius=W * .02, fill=col + (255,))
    # power led
    d.ellipse([W * .66, W * .82, W * .70, W * .86], fill=(40, 220, 60, 255))
    rk = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    rocket(ImageDraw.Draw(rk), W * .5, W * .5, W * .075, (245, 245, 250, 255), (225, 40, 35, 255),
           (60, 150, 235, 255), (20, 20, 30, 255))
    rk = rk.rotate(-40, resample=Image.BICUBIC, center=(W * .5, W * .5))
    img.alpha_composite(rk, (int(W * .24), int(-W * .26)))
    return finish(img)


def concept_stripes():
    """C: bold rocket with an 80s rainbow livery against a night-sky disc."""
    img = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.ellipse([W * .03, W * .03, W * .97, W * .97], fill=(15, 15, 35, 255))
    d.ellipse([W * .06, W * .06, W * .94, W * .94], fill=(25, 30, 80, 255))
    for x, y, r in [(.22, .28, 1.2), (.75, .22, 1.0), (.28, .75, 0.9), (.8, .7, 1.2), (.55, .12, .8), (.15, .5, .8)]:
        rr = S * r
        d.ellipse([W * x - rr, W * y - rr, W * x + rr, W * y + rr], fill=(255, 255, 220, 255))
    rk = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    rocket(ImageDraw.Draw(rk), W * .5, W * .43, W * .12, (245, 245, 250, 255), (225, 40, 35, 255),
           (60, 150, 235, 255), (10, 10, 20, 255),
           stripes=[(235, 50, 30), (250, 140, 20), (240, 220, 40), (30, 180, 80), (40, 90, 230)])
    rk = rk.rotate(-35, resample=Image.BICUBIC, center=(W * .5, W * .5))
    img.alpha_composite(rk)
    return finish(img)


def main():
    out = sys.argv[1]
    imgs = [("A", concept_magicwb()), ("B", concept_monitor()), ("C", concept_stripes())]
    k = 5
    sheet = Image.new("RGB", (3 * (SIZE * k + 20) + 20 + 3 * (SIZE + 20), SIZE * k + 70), (170, 170, 170))
    d = ImageDraw.Draw(sheet)
    x = 20
    for name, im in imgs:
        big = im.resize((SIZE * k, SIZE * k), Image.NEAREST)
        sheet.paste(big, (x, 40), big)
        sheet.paste(im, (x + SIZE * k + 6, 40 + SIZE * k - SIZE), im)   # actual size
        d.text((x, 12), name, fill=(0, 0, 0))
        x += SIZE * k + SIZE + 40
        im.save(f"{out}/concept_{name}.png")
    sheet.save(f"{out}/concepts.png")


if __name__ == "__main__":
    main()
