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


def main():
    project_dir = sys.argv[1] if len(sys.argv) > 1 else "."
    src = f"{project_dir}/voss-haas.png"
    dst = f"{project_dir}/build/splash.dat"
    try:
        img = Image.open(src)
    except FileNotFoundError:
        print(f"  ! {src} not found — skipping splash conversion", file=sys.stderr)
        return

    img = img.resize((640, 400), Image.LANCZOS)

    if img.mode == "RGBA":
        # Blend alpha against black
        bg = Image.new("RGB", img.size, (0, 0, 0))
        bg.paste(img, mask=img.split()[3])
        img = bg
    elif img.mode != "RGB":
        img = img.convert("RGB")

    raw = bytearray(img.size[0] * img.size[1])
    i = 0
    for y in range(img.size[1]):
        for x in range(img.size[0]):
            r, g, b = img.getpixel((x, y))
            raw[i] = quantize_332(r, g, b)
            i += 1

    with open(dst, "wb") as f:
        f.write(raw)

    print(f"  + splash.dat ({len(raw)} bytes)")


if __name__ == "__main__":
    main()
