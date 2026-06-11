#!/usr/bin/env python3
"""Convert voss-haas.png to 640x400 8-bit indexed raw pixel data.

The kernel uses a fixed 3-3-2 palette (RRRGGGBB):
  R = (index >> 5 & 7) * 9,  G = (index >> 2 & 7) * 9,  B = (index & 3) * 21

This script resizes the source PNG to 640x400 and quantises each pixel
to the nearest 3-3-2 colour index, writing the raw byte array that the
kernel INCBINs and blits to the VBE framebuffer at boot.
"""

import sys
from PIL import Image

def quantize_332(r, g, b):
    """Map RGB (0-255 each) to the nearest 3-3-2 palette index."""
    ri = min(round(r * 7 / 255), 7)
    gi = min(round(g * 7 / 255), 7)
    bi = min(round(b * 3 / 255), 3)
    return (ri << 5) | (gi << 2) | bi


WIDTH  = 320
HEIGHT = 200


def crop_content(img):
    """Crop away solid-black borders, returning the tight bounding box."""
    w, h = img.size
    px = img.load()

    def is_blank(rgb, threshold=10):
        return rgb[0] < threshold and rgb[1] < threshold and rgb[2] < threshold

    x0, y0, x1, y1 = 0, 0, w - 1, h - 1

    while x0 < w:
        if not all(is_blank(px[x0, y]) for y in range(h)):
            break
        x0 += 1
    while x1 >= 0:
        if not all(is_blank(px[x1, y]) for y in range(h)):
            break
        x1 -= 1
    while y0 < h:
        if not all(is_blank(px[x, y0]) for x in range(x0, x1 + 1)):
            break
        y0 += 1
    while y1 >= 0:
        if not all(is_blank(px[x, y1]) for x in range(x0, x1 + 1)):
            break
        y1 -= 1

    if x0 >= x1 or y0 >= y1:
        return img                       # entirely blank — leave as-is

    return img.crop((x0, y0, x1 + 1, y1 + 1))


def main():
    project_dir = sys.argv[1] if len(sys.argv) > 1 else "."
    src = f"{project_dir}/voss-haas.png"
    dst = f"{project_dir}/build/splash.dat"
    try:
        img = Image.open(src)
    except FileNotFoundError:
        print(f"  ! {src} not found — skipping splash conversion", file=sys.stderr)
        return

    if img.mode == "RGBA":
        bg = Image.new("RGB", img.size, (0, 0, 0))
        bg.paste(img, mask=img.split()[3])
        img = bg
    elif img.mode != "RGB":
        img = img.convert("RGB")

    # Crop black borders, then scale to fit WIDTH x HEIGHT preserving aspect
    img = crop_content(img)
    img.thumbnail((WIDTH, HEIGHT), Image.LANCZOS)

    # Centre on a black canvas
    canvas = Image.new("RGB", (WIDTH, HEIGHT), (0, 0, 0))
    ox = (WIDTH - img.width) // 2
    oy = (HEIGHT - img.height) // 2
    canvas.paste(img, (ox, oy))

    raw = bytearray(WIDTH * HEIGHT)
    i = 0
    for y in range(HEIGHT):
        for x in range(WIDTH):
            r, g, b = canvas.getpixel((x, y))
            raw[i] = quantize_332(r, g, b)
            i += 1

    with open(dst, "wb") as f:
        f.write(raw)

    print(f"  + splash.dat ({len(raw)} bytes) — cropped, scaled, centred")


if __name__ == "__main__":
    main()
