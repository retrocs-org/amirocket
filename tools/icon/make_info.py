#!/usr/bin/env python3
"""
Builds AmiRocket.info, a Workbench tool icon, from two PNGs.

The file holds a classic DiskObject with two 4-colour planar images (shown
by any Workbench) followed by an OS 3.5+ ColorIcon ("FORM ICON") with the
full-colour artwork, which icon.library on AmigaOS 3.2 prefers.

  python3 make_info.py normal.png selected.png AmiRocket.info [--stack 65536]
  python3 make_info.py --dump AmiRocket.info        (decode and check a file)
"""
import struct
import sys

from PIL import Image

WB_PALETTE = [(170, 170, 170), (0, 0, 0), (255, 255, 255), (102, 136, 187)]


# ---------------------------------------------------------------- classic

def to_wb4(img):
    """Map an RGBA image to the 4 Workbench pens (0 = background)."""
    w, h = img.size
    px = img.load()
    out = []
    for y in range(h):
        row = []
        for x in range(w):
            r, g, b, a = px[x, y]
            if a < 128:
                row.append(0)
                continue
            lum = 0.299 * r + 0.587 * g + 0.114 * b
            sat = max(r, g, b) - min(r, g, b)
            if lum < 70:
                row.append(1)                      # outlines
            elif b > r + 30 and b > g:
                row.append(3)                      # blues
            elif sat > 90 and r > g and r > b and lum < 150:
                row.append(1)                      # strong reds read as dark
            elif lum > 175:
                row.append(2)                      # whites, yellows
            else:
                row.append(3 if (x + y) & 1 else 0)  # mid tones: dither
        out.append(row)
    return out


def planar(pixels, w, h, depth=2):
    words = (w + 15) // 16
    data = bytearray()
    for plane in range(depth):
        for y in range(h):
            row = bytearray(words * 2)
            for x in range(w):
                if pixels[y][x] >> plane & 1:
                    row[x >> 3] |= 0x80 >> (x & 7)
            data += row
    return bytes(data)


def image_struct(w, h, depth):
    # LeftEdge, TopEdge, Width, Height, Depth, ImageData, PlanePick, PlaneOnOff, NextImage
    return struct.pack(">hhhhhIBBI", 0, 0, w, h, depth, 1, (1 << depth) - 1, 0, 0)


# ---------------------------------------------------------------- ColorIcon

def quantize(img, max_colours=64):
    """Palette image where index 0 is the transparent colour."""
    w, h = img.size
    rgb = Image.new("RGB", img.size, (0, 0, 0))
    rgb.paste(img, mask=img.split()[3])
    q = rgb.quantize(colors=max_colours, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    pal = q.getpalette()[: 3 * max_colours]
    used = sorted(set(q.get_flattened_data() if hasattr(q, "get_flattened_data") else q.getdata()))
    remap = {old: i + 1 for i, old in enumerate(used)}
    alpha = img.split()[3].load()
    qp = q.load()
    idx = [[0 if alpha[x, y] < 128 else remap[qp[x, y]] for x in range(w)] for y in range(h)]
    palette = [(0, 0, 0)] + [tuple(pal[3 * o:3 * o + 3]) for o in used]
    return idx, palette


class BitWriter:
    def __init__(self):
        self.out = bytearray()
        self.acc = 0
        self.n = 0

    def put(self, value, bits):
        for i in range(bits - 1, -1, -1):
            self.acc = (self.acc << 1) | ((value >> i) & 1)
            self.n += 1
            if self.n == 8:
                self.out.append(self.acc)
                self.acc = self.n = 0

    def flush(self):
        if self.n:
            self.out.append(self.acc << (8 - self.n))
            self.acc = self.n = 0
        return bytes(self.out)


def rle_encode(values, depth):
    """ColorIcon RLE: 8-bit control (ByteRun1 style) then depth-bit values."""
    bw = BitWriter()
    i, n = 0, len(values)
    while i < n:
        run = 1
        while i + run < n and run < 128 and values[i + run] == values[i]:
            run += 1
        if run >= 3:
            bw.put((257 - run) & 0xFF, 8)          # -(run-1)
            bw.put(values[i], depth)
            i += run
            continue
        j = i
        lit = []
        while j < n and len(lit) < 128:
            r = 1
            while j + r < n and r < 3 and values[j + r] == values[j]:
                r += 1
            if r >= 3:
                break
            lit.append(values[j])
            j += 1
        bw.put(len(lit) - 1, 8)
        for v in lit:
            bw.put(v, depth)
        i = j
    return bw.flush()


def rle_decode(data, depth, count):
    bits = "".join(f"{b:08b}" for b in data)
    pos = 0

    def take(k):
        nonlocal pos
        v = int(bits[pos:pos + k], 2)
        pos += k
        return v

    out = []
    while len(out) < count:
        c = take(8)
        if c < 128:
            out += [take(depth) for _ in range(c + 1)]
        elif c > 128:
            out += [take(depth)] * (257 - c)
    return out[:count]


def imag_chunk(idx, palette, with_palette=True):
    flat = [v for row in idx for v in row]
    ncol = len(palette)
    depth = max(1, (ncol - 1).bit_length())
    img_data = rle_encode(flat, depth)
    pal_data = bytes(c for rgb in palette for c in rgb) if with_palette else b""
    flags = 1 | (2 if with_palette else 0)            # transparent colour present, palette attached
    hdr = struct.pack(">BBBBBBHH", 0, ncol - 1, flags, 1, 0, depth,
                      len(img_data) - 1, max(0, len(pal_data) - 1))
    body = hdr + img_data + pal_data
    return chunk(b"IMAG", body), depth


def chunk(cid, body):
    c = cid + struct.pack(">I", len(body)) + body
    if len(body) & 1:
        c += b"\0"
    return c


def color_icon(n_img, s_img):
    w, h = n_img.size
    n_idx, n_pal = quantize(n_img)
    s_idx, s_pal = quantize(s_img)
    maxpal = max(len(n_pal), len(s_pal)) * 3
    face = chunk(b"FACE", struct.pack(">BBBBH", w - 1, h - 1, 0, 0x11, maxpal - 1))
    imag1, _ = imag_chunk(n_idx, n_pal)
    imag2, _ = imag_chunk(s_idx, s_pal)
    body = b"ICON" + face + imag1 + imag2
    return b"FORM" + struct.pack(">I", len(body)) + body


# ---------------------------------------------------------------- DiskObject

def build(normal_png, selected_png, out, stack=65536):
    n_img = Image.open(normal_png).convert("RGBA")
    s_img = Image.open(selected_png).convert("RGBA")
    w, h = n_img.size
    depth = 2

    gadget = struct.pack(">IhhhhHHHIIIIIHI",
                         0,                 # NextGadget
                         0, 0, w, h,        # Left, Top, Width, Height
                         0x0006,            # GFLG_GADGIMAGE | GFLG_GADGHIMAGE
                         0x0003,            # GACT_RELVERIFY | GACT_IMMEDIATE
                         0x0001,            # GTYP_BOOLGADGET
                         1, 1,              # GadgetRender, SelectRender (present)
                         0, 0, 0,           # GadgetText, MutualExclude, SpecialInfo
                         0,                 # GadgetID
                         1)                 # UserData: WB_DISKREVISION
    assert len(gadget) == 44
    diskobj = struct.pack(">HH", 0xE310, 1) + gadget + struct.pack(
        ">BBIIiiIIi",
        3, 0,                               # WBTOOL, pad
        0, 0,                               # DefaultTool, ToolTypes
        -0x80000000, -0x80000000,           # NO_ICON_POSITION
        0, 0,                               # DrawerData, ToolWindow
        stack)
    assert len(diskobj) == 78

    data = bytearray(diskobj)
    for img in (n_img, s_img):
        px = to_wb4(img)
        data += image_struct(w, h, depth) + planar(px, w, h, depth)
    data += color_icon(n_img, s_img)
    with open(out, "wb") as f:
        f.write(data)
    return len(data)


def dump(path):
    d = open(path, "rb").read()
    magic, ver = struct.unpack(">HH", d[:4])
    assert magic == 0xE310 and ver == 1, "not a DiskObject"
    w, h = struct.unpack(">hh", d[12:16])
    dtype = d[48]
    stack = struct.unpack(">i", d[74:78])[0]
    pos = 78
    print(f"DiskObject: type={dtype} gadget={w}x{h} stack={stack}")
    for k in range(2):
        iw, ih, idp = struct.unpack(">hhh", d[pos + 4:pos + 10])
        size = ((iw + 15) // 16) * 2 * ih * idp
        print(f"  classic image {k}: {iw}x{ih}x{idp} ({size} bytes)")
        pos += 20 + size
    assert d[pos:pos + 4] == b"FORM" and d[pos + 8:pos + 12] == b"ICON", "no ColorIcon"
    end = pos + 8 + struct.unpack(">I", d[pos + 4:pos + 8])[0]
    p = pos + 12
    fw = fh = 0
    imgs = []
    while p < end:
        cid = d[p:p + 4]
        ln = struct.unpack(">I", d[p + 4:p + 8])[0]
        body = d[p + 8:p + 8 + ln]
        if cid == b"FACE":
            fw, fh = body[0] + 1, body[1] + 1
            print(f"  FACE {fw}x{fh} maxpal={struct.unpack('>H', body[4:6])[0] + 1}")
        elif cid == b"IMAG":
            tr, nc, fl, ifmt, pfmt, dep, isz, psz = struct.unpack(">BBBBBBHH", body[:10])
            idata = body[10:10 + isz + 1]
            pdata = body[10 + isz + 1:10 + isz + 1 + psz + 1]
            pix = rle_decode(idata, dep, fw * fh) if ifmt == 1 else list(idata)
            assert len(pix) == fw * fh and max(pix) <= nc
            assert len(pdata) == (nc + 1) * 3
            print(f"  IMAG colours={nc + 1} depth={dep} rle={ifmt} image={isz + 1}B palette={psz + 1}B ok")
            imgs.append((pix, pdata))
        p += 8 + ln + (ln & 1)
    return fw, fh, imgs


if __name__ == "__main__":
    if sys.argv[1] == "--dump":
        fw, fh, imgs = dump(sys.argv[2])
        if len(sys.argv) > 3:   # write the decoded ColorIcon back to PNG to compare
            pix, pal = imgs[0]
            im = Image.new("RGBA", (fw, fh))
            im.putdata([(0, 0, 0, 0) if v == 0 else (pal[3 * v], pal[3 * v + 1], pal[3 * v + 2], 255) for v in pix])
            im.save(sys.argv[3])
    else:
        stack = 65536
        if "--stack" in sys.argv:
            stack = int(sys.argv[sys.argv.index("--stack") + 1])
        n = build(sys.argv[1], sys.argv[2], sys.argv[3], stack)
        print(f"{sys.argv[3]}: {n} bytes")
