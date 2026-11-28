#!/usr/bin/env python3
"""Builds the app icon of OdinGuard Studio: the emblem on a rounded-square tile (macOS/iOS style, like the OpenThor
Patcher icon), from assets/logo/odinguard-emblem.png.

  assets/icon/odinguard-app-1024.png   the tile, 1024 px (source for `tauri icon` and the README)

Then:  cd studio/apps/studio && npx tauri icon ../../../assets/icon/odinguard-app-1024.png
"""
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SIZE = 1024
MARGIN = 14          # like the OpenThor icon: the tile nearly fills the canvas
RADIUS = 232         # about 22.7 % of the tile: the rounded square of macOS app icons
SS = 4               # supersampling for smooth corners


def tile_mask():
    big = Image.new("L", (SIZE * SS, SIZE * SS), 0)
    ImageDraw.Draw(big).rounded_rectangle(
        [MARGIN * SS, MARGIN * SS, (SIZE - MARGIN) * SS - 1, (SIZE - MARGIN) * SS - 1], radius=RADIUS * SS, fill=255)
    return big.resize((SIZE, SIZE), Image.LANCZOS)


def background():
    """Deep red glow behind the emblem fading into near-black at the corners (the emblem's own red disc and flames)."""
    y, x = np.mgrid[0:SIZE, 0:SIZE].astype(np.float32)
    d = np.sqrt((x - SIZE / 2) ** 2 + (y - SIZE * 0.52) ** 2) / (SIZE * 0.72)
    t = np.clip(1.0 - d, 0.0, 1.0) ** 1.6
    centre = np.array([108, 14, 14], np.float32)
    edge = np.array([14, 6, 8], np.float32)
    rgb = edge[None, None, :] + (centre - edge)[None, None, :] * t[..., None]
    return Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8), "RGB").convert("RGBA")


def main():
    emblem = Image.open(os.path.join(ROOT, "assets", "logo", "odinguard-emblem.png")).convert("RGBA")
    bbox = emblem.getchannel("A").getbbox()
    emblem = emblem.crop(bbox)
    width = int((SIZE - 2 * MARGIN) * 0.90)
    emblem = emblem.resize((width, int(emblem.height * width / emblem.width)), Image.LANCZOS)

    tile = background()
    # soft shadow under the emblem so it separates from the glow
    shadow = Image.new("RGBA", tile.size, (0, 0, 0, 0))
    pos = ((SIZE - emblem.width) // 2, int((SIZE - emblem.height) / 2 + 6))
    shadow.paste((0, 0, 0, 150), pos, emblem.getchannel("A"))
    tile = Image.alpha_composite(tile, shadow.filter(ImageFilter.GaussianBlur(14)))
    tile.alpha_composite(emblem, pos)

    out = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    out.paste(tile, (0, 0), tile_mask())
    path = os.path.join(ROOT, "assets", "icon", "odinguard-app-1024.png")
    out.save(path, optimize=True)
    print("wrote", path)


if __name__ == "__main__":
    main()
