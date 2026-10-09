#!/usr/bin/env python3
"""Makes the macOS app's icon, port/macos/AppIcon.icns:

    python3 tools/macos_icon.py

The icon is drawn here, from nothing but shapes: a dark blue rounded square
with a lit orbit and a small sphere. It uses no artwork, logo or lettering of
the game, whose rights belong to their holders. Writes the iconset's PNGs (16
to 1024 pixels) and turns them into the .icns with macOS's iconutil; run it
again when the drawing changes. Needs Pillow.
"""

import math
import shutil
import subprocess
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parent.parent
OUTPUT = ROOT / "port/macos/AppIcon.icns"
SIZE = 1024
# the iconset's files: (points, scale)
ICONSET = [(16, 1), (16, 2), (32, 1), (32, 2), (128, 1), (128, 2), (256, 1), (256, 2), (512, 1), (512, 2)]


def rounded_square_mask(size: int) -> Image.Image:
    """macOS's icon shape: a rounded square inset to its grid (824 of 1024)"""
    mask = Image.new("L", (size, size), 0)
    inset = size * 100 // 1024
    radius = size * 185 // 1024
    ImageDraw.Draw(mask).rounded_rectangle((inset, inset, size - inset, size - inset), radius=radius, fill=255)
    return mask


def draw() -> Image.Image:
    size = SIZE
    background = Image.new("RGB", (size, size))
    pixels = background.load()
    # a vertical gradient, night blue to deep teal
    top, bottom = (14, 22, 44), (10, 58, 74)
    for y in range(size):
        t = y / (size - 1)
        colour = tuple(round(top[i] + (bottom[i] - top[i]) * t) for i in range(3))
        for x in range(size):
            pixels[x, y] = colour

    glow = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    centre = (size * 0.5, size * 0.54)
    # the orbit: a tilted ellipse, drawn as a thick line of points
    orbit = ImageDraw.Draw(glow)
    a, b, tilt = size * 0.33, size * 0.12, math.radians(-18)
    points = []
    for step in range(721):
        angle = 2 * math.pi * step / 720
        x, y = a * math.cos(angle), b * math.sin(angle)
        points.append((centre[0] + x * math.cos(tilt) - y * math.sin(tilt),
                       centre[1] + x * math.sin(tilt) + y * math.cos(tilt)))
    orbit.line(points, fill=(120, 230, 210, 255), width=size // 40, joint="curve")
    blurred = glow.filter(ImageFilter.GaussianBlur(size / 60))

    icon = background.convert("RGBA")
    icon = Image.alpha_composite(icon, blurred)
    icon = Image.alpha_composite(icon, glow)

    # the sphere, lit from the top left
    sphere = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    sphere_pixels = sphere.load()
    radius = size * 0.16
    cx, cy = centre[0], centre[1] - size * 0.04
    for y in range(int(cy - radius) - 1, int(cy + radius) + 2):
        for x in range(int(cx - radius) - 1, int(cx + radius) + 2):
            dx, dy = (x - cx) / radius, (y - cy) / radius
            distance = dx * dx + dy * dy
            if distance > 1.0:
                continue
            dz = math.sqrt(1.0 - distance)
            light = max(0.0, (-0.5 * dx - 0.6 * dy + 0.62 * dz))
            shade = 0.25 + 0.75 * light
            colour = (round(70 + 150 * shade), round(110 + 130 * shade), round(150 + 100 * shade))
            edge = min(1.0, (1.0 - math.sqrt(distance)) * radius / 2.0)
            sphere_pixels[x, y] = colour + (round(255 * edge),)
    icon = Image.alpha_composite(icon, sphere)
    # the near half of the orbit passes in front of the sphere
    front = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    ImageDraw.Draw(front).line(points[:361], fill=(140, 240, 220, 255), width=size // 40, joint="curve")
    icon = Image.alpha_composite(icon, front)

    shaped = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    shaped.paste(icon, (0, 0), rounded_square_mask(size))
    return shaped


def main() -> None:
    if not shutil.which("iconutil"):
        raise SystemExit("iconutil (macOS) is needed to make the .icns")
    artwork = draw()
    with tempfile.TemporaryDirectory() as temporary:
        iconset = Path(temporary) / "AppIcon.iconset"
        iconset.mkdir()
        for points, scale in ICONSET:
            pixels = points * scale
            name = f"icon_{points}x{points}{'@2x' if scale == 2 else ''}.png"
            artwork.resize((pixels, pixels), Image.LANCZOS).save(iconset / name)
        subprocess.run(["iconutil", "-c", "icns", str(iconset), "-o", str(OUTPUT)], check=True)
    print(f"wrote {OUTPUT.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
