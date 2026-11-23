#!/usr/bin/env python3
"""Builds the OdinGuard branding from assets/src/emblem-generated.png and the fonts in assets/fonts:

  assets/logo/odinguard-emblem.png      emblem, transparent, 1024 px
  assets/logo/odinguard-logo.png        emblem + wordmark (website, docs)
  assets/icon/odinguard.ico             icon 16..256 px (DLL resource, installer)
  assets/splash/splash.png              splash screen preview
  client/res/splash.bmp                 splash for the DLL (32 bit, premultiplied alpha, bottom-up)
  client/res/odinguard.ico              copy of the icon for the resource compiler

Fonts: Cinzel Decorative (title) and Philosopher (text), both SIL Open Font License.
"""
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
A = os.path.join(ROOT, "assets")
VERSION = sys.argv[1] if len(sys.argv) > 1 else "0.3"
TITLE_FONT = os.path.join(A, "fonts", "CinzelDecorative-Bold.ttf")
TEXT_FONT = os.path.join(A, "fonts", "Philosopher-Bold.ttf")

GOLD = (245, 200, 92)
GOLD_DARK = (150, 96, 24)
CREAM = (255, 240, 205)


def cut_out(img):
    """Black background -> transparent. Only near-black pixels connected to the border are removed,
    so the dark feathers of the ravens stay opaque."""
    rgb = np.asarray(img.convert("RGB")).astype(np.int16)
    h, w, _ = rgb.shape
    lum = rgb.max(axis=2)
    dark = lum < 10
    bg = np.zeros((h, w), bool)
    stack = [(y, x) for x in range(w) for y in (0, h - 1)] + [(y, x) for y in range(h) for x in (0, w - 1)]
    while stack:
        y, x = stack.pop()
        if bg[y, x] or not dark[y, x]:
            continue
        bg[y, x] = True
        if y > 0: stack.append((y - 1, x))
        if y < h - 1: stack.append((y + 1, x))
        if x > 0: stack.append((y, x - 1))
        if x < w - 1: stack.append((y, x + 1))
    alpha = np.where(bg, 0, 255).astype(np.uint8)
    a = Image.fromarray(alpha).filter(ImageFilter.GaussianBlur(1.2))
    # soft edge: semi-dark pixels next to the background get partial alpha
    edge = np.clip((lum - 4) * 255 // 30, 0, 255).astype(np.uint8)
    a = Image.fromarray(np.maximum(np.asarray(a), np.where(bg, 0, edge)).astype(np.uint8))
    out = img.convert("RGBA")
    out.putalpha(a)
    return out.crop(out.getbbox())


def gradient_text(size, text, font, top, bottom, stroke=0, stroke_fill=(40, 10, 5)):
    """Text with a vertical gold gradient and a dark outline, returned as RGBA image."""
    tmp = Image.new("L", (1, 1))
    box = ImageDraw.Draw(tmp).textbbox((0, 0), text, font=font, stroke_width=stroke)
    w, h = box[2] - box[0] + 4, box[3] - box[1] + 4
    mask = Image.new("L", (w, h))
    ImageDraw.Draw(mask).text((-box[0] + 2, -box[1] + 2), text, font=font, fill=255)
    outline = Image.new("L", (w, h))
    ImageDraw.Draw(outline).text((-box[0] + 2, -box[1] + 2), text, font=font, fill=255, stroke_width=stroke, stroke_fill=255)
    grad = Image.new("RGB", (w, h))
    gd = ImageDraw.Draw(grad)
    for y in range(h):
        t = y / max(1, h - 1)
        gd.line([(0, y), (w, y)], fill=tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3)))
    img = Image.new("RGBA", (w, h), stroke_fill + (0,))
    img.putalpha(outline)
    img.paste(grad, (0, 0), mask)
    return img


def make_icon(emblem):
    sizes = [256, 128, 64, 48, 32, 24, 16]
    sq = max(emblem.size)
    base = Image.new("RGBA", (sq, sq), (0, 0, 0, 0))
    base.paste(emblem, ((sq - emblem.width) // 2, (sq - emblem.height) // 2), emblem)
    imgs = []
    for s in sizes:
        im = base.resize((s, s), Image.LANCZOS)
        if s <= 32:  # small sizes: a little sharper
            im = im.filter(ImageFilter.UnsharpMask(radius=1, percent=80, threshold=2))
        imgs.append(im)
    path = os.path.join(A, "icon", "odinguard.ico")
    imgs[0].save(path, sizes=[(s, s) for s in sizes], append_images=imgs[1:])
    for s, im in zip(sizes, imgs):
        im.save(os.path.join(A, "icon", f"odinguard-{s}.png"))
    return path


def make_logo(emblem):
    em = emblem.resize((int(emblem.width * 420 / emblem.height), 420), Image.LANCZOS)
    title = gradient_text(0, "OdinGuard", ImageFont.truetype(TITLE_FONT, 170), (255, 236, 160), GOLD_DARK, stroke=6)
    sub = gradient_text(0, "Client Protection  ·  BurningRO", ImageFont.truetype(TEXT_FONT, 44), CREAM, (230, 190, 120), stroke=3)
    w = em.width + 40 + max(title.width, sub.width) + 40
    h = max(em.height, title.height + sub.height + 20) + 40
    logo = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    logo.paste(em, (20, (h - em.height) // 2), em)
    x = em.width + 50
    y = (h - title.height - sub.height - 10) // 2
    logo.alpha_composite(title, (x, y))
    logo.alpha_composite(sub, (x + 8, y + title.height + 10))
    logo.save(os.path.join(A, "logo", "odinguard-logo.png"))


def make_splash(emblem):
    W, H = 600, 300
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    # card: dark crimson radial gradient, gold double border, rounded corners
    yy, xx = np.mgrid[0:H, 0:W]
    d = np.sqrt(((xx - W * 0.30) / W) ** 2 + ((yy - H * 0.45) / H) ** 2)
    t = np.clip(d / 0.75, 0, 1)[..., None]
    inner, outer = np.array([120, 18, 12]), np.array([22, 4, 4])
    card = (inner * (1 - t) + outer * t).astype(np.uint8)
    card_img = Image.fromarray(card).convert("RGBA")
    mask = Image.new("L", (W, H))
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, W - 1, H - 1], radius=18, fill=255)
    img.paste(card_img, (0, 0), mask)
    dr = ImageDraw.Draw(img)
    dr.rounded_rectangle([3, 3, W - 4, H - 4], radius=16, outline=GOLD + (255,), width=3)
    dr.rounded_rectangle([10, 10, W - 11, H - 11], radius=12, outline=GOLD_DARK + (200,), width=1)
    # faint rune ring behind the emblem
    rune_font = ImageFont.truetype(TEXT_FONT, 15)
    runes = "ᚠᚢᚦᚨᚱᚲᚷᚹᚺᚾᛁᛃᛇᛈᛉᛊᛏᛒᛖᛗᛚᛜᛞᛟ"
    ring = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    em = emblem.resize((int(emblem.width * 200 / emblem.height), 200), Image.LANCZOS)
    cx, cy = 30 + em.width // 2, H // 2
    glow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    ImageDraw.Draw(glow).ellipse([cx - 120, cy - 120, cx + 120, cy + 120], fill=(255, 120, 40, 70))
    img.alpha_composite(glow.filter(ImageFilter.GaussianBlur(28)))
    img.alpha_composite(ring)
    img.alpha_composite(em, (30, (H - em.height) // 2))
    # wordmark
    title = gradient_text(0, "OdinGuard", ImageFont.truetype(TITLE_FONT, 40), (255, 236, 160), GOLD_DARK, stroke=3)
    x = 30 + em.width + 14
    img.alpha_composite(title, (x, 102))
    f = ImageFont.truetype(TEXT_FONT, 17)
    dr = ImageDraw.Draw(img)
    dr.text((x + 4, 102 + title.height + 8), "Client protection for BurningRO", font=f, fill=CREAM + (255,))
    dr.line([(x + 4, 102 + title.height + 36), (W - 34, 102 + title.height + 36)], fill=GOLD_DARK + (255,), width=1)
    small = ImageFont.truetype(TEXT_FONT, 13)
    dr.text((x + 4, 102 + title.height + 44), f"Version {VERSION}  ·  Ragnarok Online", font=small, fill=(230, 190, 130, 255))
    del runes, rune_font
    img.save(os.path.join(A, "splash", "splash.png"))
    return img


def write_bmp32(img, path):
    """32-bit BI_RGB bottom-up BMP with premultiplied alpha (what UpdateLayeredWindow expects)."""
    a = np.asarray(img).astype(np.uint16)
    rgb = a[..., :3] * a[..., 3:4] // 255
    bgra = np.dstack([rgb[..., 2], rgb[..., 1], rgb[..., 0], a[..., 3]]).astype(np.uint8)[::-1]
    h, w = bgra.shape[:2]
    data = bgra.tobytes()
    import struct
    hdr = struct.pack("<2sIHHI", b"BM", 14 + 40 + len(data), 0, 0, 54)
    info = struct.pack("<IiiHHIIiiII", 40, w, h, 1, 32, 0, len(data), 2835, 2835, 0, 0)
    with open(path, "wb") as f:
        f.write(hdr + info + data)


def main():
    emblem = cut_out(Image.open(os.path.join(A, "src", "emblem-generated.png")))
    emblem.save(os.path.join(A, "logo", "odinguard-emblem.png"))
    ico = make_icon(emblem)
    make_logo(emblem)
    splash = make_splash(emblem)
    res = os.path.join(ROOT, "client", "res")
    os.makedirs(res, exist_ok=True)
    write_bmp32(splash, os.path.join(res, "splash.bmp"))
    with open(ico, "rb") as s, open(os.path.join(res, "odinguard.ico"), "wb") as d:
        d.write(s.read())
    print("branding written")


if __name__ == "__main__":
    main()
