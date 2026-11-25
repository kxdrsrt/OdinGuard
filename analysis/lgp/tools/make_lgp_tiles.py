#!/usr/bin/env python3
"""Draw the OdinGuard LGP cell tiles (own work) and a contact sheet with a simulated in-game view.

Client facts used here (alpha.exe 2018-06-20, static analysis, see analysis/lgp/lgp-audit.md):
  * units 0xc8/0xc9 -> effects 671/672 -> flat quad helper 0x949b00(texture, mode 3):
    quad corners at 225/315/45/135 degrees (axis-aligned square), radius 8..10 world units (pulsing),
    vertex colour (0x37,0x37,0xff) = fixed blue tint, vertex alpha 250, additive blending.
  * palette units (only with the proposed OdinGuard client patch) use the same helper and geometry (mode 3);
    the patch sets the primitive's tint selector (+0x304) to 110.0: white tint, normal alpha blending.
  * one cell = 5 world units.
Every tile is an upright square of 45 px (0.8..1.0 cell while the quad pulses) in a transparent 128x128 texture.
Files: 128x128 TGA type 2, 32 bit, origin bottom-left, descriptor 0x08, TGA 2.0 footer (65,580 bytes) like
the client's hanmoon1.tga.

Usage: make_lgp_tiles.py <out data root (…/lgp)> <sheet.png> [symbols-sheet.png]
"""
import math
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

SIZE = 128
CELL = 5.0

# fill colours of the palette (name, RGB); hanmoon1/2 are the blue slots of the vanilla client
PALETTE = [
    ("blue", (40, 120, 255)),
    ("cyan", (120, 245, 255)),
    ("yellow", (255, 214, 0)),
    ("red", (255, 40, 24)),
    ("orange", (255, 128, 0)),
    ("green", (40, 220, 70)),
    ("purple", (170, 70, 255)),
    ("brown", (176, 108, 44)),
    ("pink", (255, 80, 190)),
    ("white", (245, 245, 245)),
    ("gold", (255, 190, 60)),
    ("grey", (125, 125, 135)),
    ("lightgrey", (200, 206, 218)),
    ("lime", (170, 235, 40)),
    ("lightgreen", (150, 255, 160)),   # Pneuma
    ("peach", (255, 170, 120)),        # dancer dances
    ("skyblue", (120, 200, 255)),      # ensemble songs
]

# symbols inside the tile (marker bits 16..23); the number is the index here + 1, same list in client/src/og_lgp.c
SYMBOLS = ["plus", "minus", "shield", "note", "dnote", "apple", "heart", "star", "cross", "zzz", "coin", "swirl", "drum",
           "ring", "noentry", "diamond", "bolt", "exclaim", "arrowdown", "moon", "sun", "flame", "wand", "web", "swordplus"]
BLUE = (110, 165, 255)


def tga_bytes(im):
    """Uncompressed TGA 32 bit, bottom-left origin, 8 alpha bits, TGA 2.0 footer (as the client's own files)."""
    w, h = im.size
    head = struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, w, h, 32, 0x08)
    px = im.transpose(Image.FLIP_TOP_BOTTOM).tobytes("raw", "BGRA")
    foot = struct.pack("<II", 0, 0) + b"TRUEVISION-XFILE.\0"
    return head + px + foot


def supersample(draw_fn, ss=4):
    big = Image.new("RGBA", (SIZE * ss, SIZE * ss), (0, 0, 0, 0))
    draw_fn(ImageDraw.Draw(big), ss)
    return big.resize((SIZE, SIZE), Image.LANCZOS)


def polygon(cx, cy, half, diamond, ss):
    c, h = cx * ss, half * ss
    if diamond:
        return [(c, c - h), (c + h, c), (c, c + h), (c - h, c)]
    return [(c - h, c - h), (c + h, c - h), (c + h, c + h), (c - h, c + h)]


def tri_polygon(cx, cy, half, ss):
    """Upright triangle in the same box as the square tile (hostile skills)."""
    c, h = cx * ss, half * ss
    return [(c, c - h), (c + h, c + h), (c - h, c + h)]


def tri_tile(rgb, half, border, fill_alpha=170, border_scale=0.45, hollow=False):
    dark = tuple(int(v * border_scale) for v in rgb)

    def draw(d, ss):
        d.polygon(tri_polygon(SIZE / 2, SIZE / 2, half, ss), fill=dark + (255,))
        # the inner triangle keeps the border width perpendicular to the sides
        d.polygon(tri_polygon(SIZE / 2, SIZE / 2 + border * 0.55, half - border * 1.6, ss), fill=rgb + ((40 if hollow else fill_alpha),))
    return supersample(draw)


def _glyph(name, ss):
    """Mask (L, SIZE*ss) of one symbol in a unit box [-1, 1]^2 scaled by the caller (see place())."""
    S = 100 * ss
    m = Image.new("L", (S, S), 0)
    d = ImageDraw.Draw(m)
    X = lambda v: (v + 1) * S / 2
    P = lambda pts: [(X(x), X(y)) for x, y in pts]

    def rect(x0, y0, x1, y1, v=255):
        d.rectangle((X(x0), X(y0), X(x1), X(y1)), fill=v)

    def ell(cx, cy, rx, ry, v=255):
        d.ellipse((X(cx - rx), X(cy - ry), X(cx + rx), X(cy + ry)), fill=v)

    def line(pts, w):
        d.line(P(pts), fill=255, width=int(w * S / 2), joint="curve")
        for x, y in pts:
            ell(x, y, w / 2, w / 2)

    if name == "plus":
        rect(-0.2, -0.9, 0.2, 0.9); rect(-0.9, -0.2, 0.9, 0.2)
    elif name == "minus":
        rect(-0.9, -0.2, 0.9, 0.2)
    elif name == "shield":
        d.polygon(P([(-0.8, -0.9), (0.8, -0.9), (0.8, 0.0), (0.45, 0.6), (0, 0.95), (-0.45, 0.6), (-0.8, 0.0)]), fill=255)
        d.polygon(P([(-0.4, -0.55), (0.4, -0.55), (0.4, -0.05), (0, 0.5), (-0.4, -0.05)]), fill=0)
    elif name == "note":
        ell(-0.3, 0.6, 0.5, 0.34); rect(0.07, -0.9, 0.3, 0.6)
        d.polygon(P([(0.3, -0.9), (0.9, -0.35), (0.85, 0.15), (0.3, -0.3)]), fill=255)
    elif name == "dnote":
        ell(-0.55, 0.62, 0.38, 0.28); ell(0.5, 0.42, 0.38, 0.28)
        rect(-0.24, -0.7, -0.02, 0.62); rect(0.81, -0.9, 0.99, 0.42)
        d.polygon(P([(-0.24, -0.7), (0.99, -0.92), (0.99, -0.5), (-0.24, -0.28)]), fill=255)
    elif name == "apple":
        ell(-0.3, 0.2, 0.55, 0.7); ell(0.3, 0.2, 0.55, 0.7)
        ell(0, -0.5, 0.14, 0.2, 0)
        rect(-0.07, -0.85, 0.07, -0.35)
        d.polygon(P([(0.07, -0.55), (0.35, -1.0), (0.95, -0.85), (0.6, -0.4)]), fill=255)
    elif name == "heart":
        ell(-0.42, -0.3, 0.5, 0.5); ell(0.42, -0.3, 0.5, 0.5)
        d.polygon(P([(-0.88, -0.05), (0.88, -0.05), (0, 0.95)]), fill=255)
    elif name == "star":
        pts = []
        for i in range(10):
            r = 1.0 if i % 2 == 0 else 0.42
            a = -math.pi / 2 + i * math.pi / 5
            pts.append((r * math.cos(a), r * math.sin(a) + 0.05))
        d.polygon(P(pts), fill=255)
    elif name == "cross":
        line([(-0.75, -0.75), (0.75, 0.75)], 0.42); line([(0.75, -0.75), (-0.75, 0.75)], 0.42)
    elif name == "zzz":
        d.polygon(P([(-0.8, -0.85), (0.8, -0.85), (0.8, -0.55), (-0.25, 0.55), (0.8, 0.55), (0.8, 0.85), (-0.8, 0.85), (-0.8, 0.55), (0.25, -0.55), (-0.8, -0.55)]), fill=255)
    elif name == "coin":
        ell(0, 0, 0.9, 0.9)
    elif name == "swirl":
        pts = [(0.12 * t * math.cos(t * 0.9), 0.12 * t * math.sin(t * 0.9)) for t in [i * 0.25 for i in range(0, 31)]]
        line(pts, 0.3)
    elif name == "drum":
        rect(-0.8, -0.25, 0.8, 0.75); ell(0, 0.75, 0.8, 0.22); ell(0, -0.25, 0.8, 0.3)
        ell(0, -0.25, 0.55, 0.15, 0)
    elif name == "ring":
        ell(0, 0, 0.9, 0.9); ell(0, 0, 0.5, 0.5, 0)
    elif name == "noentry":
        ell(0, 0, 0.9, 0.9); ell(0, 0, 0.56, 0.56, 0)
        d.line(P([(-0.5, 0.5), (0.5, -0.5)]), fill=255, width=int(0.34 * S / 2))
    elif name == "diamond":
        d.polygon(P([(0, -0.95), (0.8, 0), (0, 0.95), (-0.8, 0)]), fill=255)
    elif name == "bolt":
        d.polygon(P([(0.2, -0.95), (-0.65, 0.12), (-0.05, 0.12), (-0.3, 0.95), (0.7, -0.18), (0.1, -0.18), (0.5, -0.95)]), fill=255)
    elif name == "exclaim":
        d.polygon(P([(-0.28, -0.95), (0.28, -0.95), (0.18, 0.3), (-0.18, 0.3)]), fill=255)
        ell(0, 0.72, 0.24, 0.24)
    elif name == "arrowdown":
        rect(-0.25, -0.9, 0.25, 0.1)
        d.polygon(P([(-0.85, 0.05), (0.85, 0.05), (0, 0.95)]), fill=255)
    elif name == "moon":
        ell(0, 0, 0.9, 0.9); ell(0.42, -0.2, 0.74, 0.74, 0)
    elif name == "sun":
        ell(0, 0, 0.48, 0.48)
        for i in range(8):
            a = i * math.pi / 4
            d.line(P([(0.66 * math.cos(a), 0.66 * math.sin(a)), (0.97 * math.cos(a), 0.97 * math.sin(a))]), fill=255, width=int(0.2 * S / 2))
    elif name == "flame":
        d.polygon(P([(0.05, -0.98), (0.55, -0.3), (0.75, 0.3), (0.45, 0.85), (0, 0.98), (-0.45, 0.85), (-0.75, 0.3), (-0.5, -0.2), (-0.25, 0.05), (-0.2, -0.45)]), fill=255)
    elif name == "wand":
        line([(-0.8, 0.8), (0.35, -0.35)], 0.26)
        pts = []
        for i in range(10):
            r = 0.55 if i % 2 == 0 else 0.24
            a = -math.pi / 2 + i * math.pi / 5
            pts.append((0.5 + r * math.cos(a), -0.5 + r * math.sin(a)))
        d.polygon(P(pts), fill=255)
    elif name == "web":
        for i in range(8):
            a = i * math.pi / 4
            line([(0, 0), (0.98 * math.cos(a), 0.98 * math.sin(a))], 0.17)
        for r in (0.55, 0.98):
            pts = [(r * math.cos(i * math.pi / 4), r * math.sin(i * math.pi / 4)) for i in range(9)]
            line(pts, 0.17)
    elif name == "swordplus":
        d.polygon(P([(-0.4, -0.98), (-0.2, -0.78), (-0.2, 0.3), (-0.6, 0.3), (-0.6, -0.78)]), fill=255)
        rect(-0.85, 0.28, 0.05, 0.46)
        rect(-0.52, 0.46, -0.28, 0.8)
        ell(-0.4, 0.88, 0.17, 0.13)
        rect(0.3, -0.5, 0.95, -0.28); rect(0.52, -0.72, 0.74, -0.06)
    return m


def place(im, name, cx, cy, half, ss=4):
    """White symbol with a dark outline, centred at (cx, cy) texture pixels, half = half size in pixels."""
    g = _glyph(name, ss)
    px = int(2 * half * ss)
    g = g.resize((px, px), Image.LANCZOS)
    pad = 3 * ss
    mask = Image.new("L", (SIZE * ss, SIZE * ss), 0)
    mask.paste(g, (int(cx * ss - px / 2), int(cy * ss - px / 2)))
    outline = mask.filter(ImageFilter.MaxFilter(2 * pad + 1))
    big = Image.new("RGBA", (SIZE * ss, SIZE * ss), (0, 0, 0, 0))
    big.paste(Image.new("RGBA", big.size, (18, 18, 18, 255)), (0, 0), outline)
    big.paste(Image.new("RGBA", big.size, (255, 255, 255, 255)), (0, 0), mask)
    big = big.resize((SIZE, SIZE), Image.LANCZOS)
    out = im.copy()
    out.alpha_composite(big)
    return out


def tile(rgb, diamond, half, border, fill_alpha=170, border_scale=0.45, hollow=False):
    """Cell tile: fill + thin darker border (in additive blending a darker border is a dimmer line)."""
    dark = tuple(int(v * border_scale) for v in rgb)

    def draw(d, ss):
        outer = polygon(SIZE / 2, SIZE / 2, half, diamond, ss)
        d.polygon(outer, fill=dark + (255,))
        inset = border * (math.sqrt(2) if diamond else 1.0)
        inner = polygon(SIZE / 2, SIZE / 2, half - inset, diamond, ss)
        d.polygon(inner, fill=rgb + ((40 if hollow else fill_alpha),))
    return supersample(draw)


def hollow_tile(rgb, diamond, half, border):
    """Second style: bright frame, faint inside (readable as 'outline' cells next to filled cells)."""
    def draw(d, ss):
        d.polygon(polygon(SIZE / 2, SIZE / 2, half, diamond, ss), fill=rgb + (255,))
        inset = border * (math.sqrt(2) if diamond else 1.0)
        d.polygon(polygon(SIZE / 2, SIZE / 2, half - inset, diamond, ss), fill=rgb + (35,))
    return supersample(draw)


# geometry -> tile size in texture pixels
# mode 3 (hanmoon): axis-aligned, world half-side per uv = r/sqrt2, r = 8..10 -> 2.5 world at r = 10
H3 = 64 * (CELL / 2) / (10 / math.sqrt(2))          # 22.6 px -> 0.8..1.0 cell


def build(root):
    eff = Path(root) / "data" / "texture" / "effect"
    eff.mkdir(parents=True, exist_ok=True)
    out = {}
    out["hanmoon1.tga"] = (tile(BLUE, False, H3, 3, fill_alpha=150), "vanilla")
    # fallback without the client patch: 0xc8 = protective (square), 0xc9 = hostile (triangle), both tinted blue
    out["hanmoon2.tga"] = (tri_tile((170, 210, 255), H3, 3, fill_alpha=150), "vanilla")
    for name, rgb in PALETTE:
        out[f"odinlgp_{name}.tga"] = (tile(rgb, False, H3, 3, fill_alpha=185, border_scale=0.55), "patch")
        out[f"odinlgp_{name}_t.tga"] = (tri_tile(rgb, H3 * 1.12, 3, fill_alpha=190, border_scale=0.55), "patch")
        for sym in SYMBOLS:
            out[f"odinlgp_{name}_{sym}.tga"] = (place(out[f"odinlgp_{name}.tga"][0], sym, SIZE / 2, SIZE / 2, 11.5), "patch")
            out[f"odinlgp_{name}_t_{sym}.tga"] = (place(out[f"odinlgp_{name}_t.tga"][0], sym, SIZE / 2, SIZE / 2 + 7, 9.5), "patch")
    for name, (im, _) in out.items():
        (eff / name).write_bytes(tga_bytes(im))
    return out


def simulate(im, kind, floor, r_frac=0.5):
    """Top-down view of 3x3 cells with the client's geometry (mode 3: upright square, half-diagonal 8..10)."""
    scale = 8  # sheet pixels per world unit
    W = int(CELL * 3 * scale) + 40
    r = 8 + 2 * r_frac
    side = 2 * (r / math.sqrt(2)) * scale
    q = np.array(im.resize((max(1, int(side)), max(1, int(side))), Image.BILINEAR)).astype(np.float32)
    arr = np.array(Image.new("RGB", (W, W), floor)).astype(np.float32)
    a = q[..., 3:4] / 255.0 * (250 / 255)
    for cy in range(3):
        for cx in range(3):
            centre = (20 + (cx + 0.5) * CELL * scale, 20 + (cy + 0.5) * CELL * scale)
            x0, y0 = int(centre[0] - q.shape[1] / 2), int(centre[1] - q.shape[0] / 2)
            xs, ys, xe, ye = max(0, x0), max(0, y0), min(W, x0 + q.shape[1]), min(W, y0 + q.shape[0])
            src, al = q[ys - y0:ye - y0, xs - x0:xe - x0, :3], a[ys - y0:ye - y0, xs - x0:xe - x0]
            if kind == "vanilla":   # additive (SRCALPHA, ONE), vertex colour 0x3737ff
                arr[ys:ye, xs:xe] += src * al * np.array((0x37 / 255, 0x37 / 255, 1.0), dtype=np.float32)
            else:                   # patch: alpha blending (SRCALPHA, INVSRCALPHA), white
                arr[ys:ye, xs:xe] = src * al + arr[ys:ye, xs:xe] * (1 - al)
    out = Image.fromarray(arr.clip(0, 255).astype(np.uint8))
    d = ImageDraw.Draw(out)
    for i in range(4):  # cell grid
        p = 20 + i * CELL * scale
        d.line((20, p, 20 + 3 * CELL * scale, p), fill=(255, 255, 255), width=1)
        d.line((p, 20, p, 20 + 3 * CELL * scale), fill=(255, 255, 255), width=1)
    c = 20 + 1.5 * CELL * scale  # a character in the middle cell
    d.ellipse((c - 6, c - 14, c + 6, c + 2), outline=(0, 0, 0), fill=(230, 200, 170))
    return out


def checker(size):
    im = Image.new("RGB", (size, size), (200, 200, 200))
    d = ImageDraw.Draw(im)
    for y in range(0, size, 8):
        for x in range(0, size, 8):
            if (x // 8 + y // 8) % 2:
                d.rectangle((x, y, x + 7, y + 7), fill=(150, 150, 150))
    return im


def sheet(tiles, path):
    names = list(tiles)
    colw = 170
    S = Image.new("RGB", (colw * len(names), 22 + SIZE + 8 + 3 * 165 + 10), (24, 24, 24))
    d = ImageDraw.Draw(S)
    for i, n in enumerate(names):
        im, kind = tiles[n]
        x = i * colw
        bg = checker(SIZE)
        bg.paste(im, (0, 0), im)
        S.paste(bg, (x + 20, 22))
        d.text((x + 4, 4), n, fill=(255, 230, 150))
        y = 22 + SIZE + 8
        rows = [("patch", (22, 58, 26), "patch, darkworld floor"), ("patch", (92, 86, 70), "patch, vanilla floor")]
        if kind == "vanilla":
            rows.append(("vanilla", (22, 58, 26), "no patch: additive+blue"))
        for k, floor, label in rows:
            sim = simulate(im, k, floor)
            sim.thumbnail((150, 150))
            S.paste(sim, (x + 10, y))
            d.text((x + 10, y + 150), label, fill=(200, 200, 200))
            y += 165
    S.save(path)


def symbol_grid(tiles, path, scale=2):
    """All symbols on a mid-tone floor, simulated 'patch' blending at 3 cells, square above triangle."""
    cols = ["blue", "gold", "red", "lightgrey"]
    cw = 96
    S = Image.new("RGB", (cw * len(SYMBOLS), cw * 2 * len(cols) + 20), (92, 86, 70))
    d = ImageDraw.Draw(S)
    for ci, col in enumerate(cols):
        for si, sym in enumerate(SYMBOLS):
            for ti, suffix in enumerate(("", "_t")):
                im = tiles[f"odinlgp_{col}{suffix}_{sym}.tga"][0]
                small = im.resize((cw, cw), Image.LANCZOS)
                S.paste(small, (si * cw, 20 + (ci * 2 + ti) * cw), small)
            if ci == 0:
                d.text((si * cw + 4, 4), sym, fill=(255, 230, 150))
    S.save(path)


if __name__ == "__main__":
    t = build(sys.argv[1])
    plain = {n: v for n, v in t.items() if n.count("_") <= 2 and not any(n.endswith(f"_{x}.tga") for x in SYMBOLS)}
    sheet(plain, sys.argv[2])
    symbol_grid(t, sys.argv[3] if len(sys.argv) > 3 else str(Path(sys.argv[2]).with_name("lgp-symbols-sheet.png")))
    print(len(t), "tiles written")
