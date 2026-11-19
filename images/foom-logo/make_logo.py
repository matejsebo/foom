#!/usr/bin/env python3
#
#    Copyright 2026 Matej Šebo
#
#    This file is part of FOOM.
#
#    FOOM is free software: you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation, either version 3 of the License, or
#    (at your option) any later version.
#
#    FOOM is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License
#    along with FOOM.  If not, see <http://www.gnu.org/licenses/>.

"""Draws the FOOM logo and wordmark and writes every file made from them.

The logo is a rubber stamp in the purple of a course overprint, on white:
a round stamp with an F as the icon, a framed FOOM as the wordmark. The
wobble and the specks are seeded, so the output is the same on every run.
Needs Pillow:

    uv run --no-project --with pillow python images/foom-logo/make_logo.py

Writes the SVG and PNG files beside this script, and the program icons
(PNG sizes, .ico, .icns) into images/mapper-icon/ and images/mapper.png.
Icons of 48 pixels and less are drawn simpler: a heavier ring, a larger F,
no specks.
"""

import math
import random
from pathlib import Path

from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
IMAGES = HERE.parent

PURPLE = "#b3279f"   # screen approximation of PMS Purple, the overprint colour
WHITE = "#ffffff"

SUPERSAMPLE = 4

# Letters in a box 140 high. Each is a list of rings; rings after the first are holes.
LETTER_F = [[(0, 0), (102, 0), (98, 36), (40, 34), (40, 58), (84, 56), (82, 92), (40, 90), (38, 140), (0, 140)]]
LETTER_O = [
    [(24, 0), (86, 0), (110, 26), (110, 114), (88, 140), (22, 140), (0, 116), (0, 24)],
    [(42, 36), (68, 36), (74, 44), (74, 98), (68, 106), (42, 106), (36, 98), (36, 44)],
]
LETTER_M = [[(0, 140), (0, 0), (38, 0), (67, 60), (96, 0), (134, 0), (134, 140), (98, 140), (98, 66),
             (76, 110), (58, 110), (36, 66), (36, 140)]]


def roughen(points, rng, amp, step=24.0):
    """Cuts each edge of a closed ring into pieces and pushes the cuts about."""
    out = []
    n = len(points)
    for i in range(n):
        ax, ay = points[i]
        bx, by = points[(i + 1) % n]
        pieces = max(1, int(math.hypot(bx - ax, by - ay) // step))
        for k in range(pieces):
            t = k / pieces
            out.append((ax + (bx - ax) * t + rng.gauss(0, amp), ay + (by - ay) * t + rng.gauss(0, amp)))
    return out


def circle(cx, cy, radius, n=72):
    return [(cx + radius * math.cos(math.tau * i / n), cy + radius * math.sin(math.tau * i / n)) for i in range(n)]


# A drawing is a list of primitives:
#   ("poly", colour, rings)   ("line", colour, width, points)   ("dot", colour, x, y, radius)

def placed(drawing, dx=0.0, dy=0.0, angle=0.0, scale=1.0, about=(0.0, 0.0)):
    """The drawing scaled and turned about a point, then moved."""
    c, s = math.cos(math.radians(angle)), math.sin(math.radians(angle))
    ox, oy = about

    def pt(p):
        x, y = (p[0] - ox) * scale, (p[1] - oy) * scale
        return (ox + x * c - y * s + dx, oy + x * s + y * c + dy)

    out = []
    for prim in drawing:
        if prim[0] == "poly":
            out.append(("poly", prim[1], [[pt(p) for p in ring] for ring in prim[2]]))
        elif prim[0] == "line":
            out.append(("line", prim[1], prim[2] * scale, [pt(p) for p in prim[3]]))
        else:
            x, y = pt((prim[2], prim[3]))
            out.append(("dot", prim[1], x, y, prim[4] * scale))
    return out


def closed(points):
    return points + points[:2]


def wear(rng, inside, clamp, count, radii, scratches, reach, widths):
    """Specks and scratches where the stamp did not take the ink."""
    out = []
    for _ in range(count):
        x, y = inside(rng)
        out.append(("dot", WHITE, x, y, rng.choice(radii)))
    for _ in range(scratches):
        x, y = inside(rng)
        length, lean = rng.uniform(*reach), rng.uniform(-0.2, 0.2)
        line = [clamp(x + length * t + rng.gauss(0, 1.2), y + length * lean * t + rng.gauss(0, 1.2)) for t in (0, 0.33, 0.66, 1)]
        out.append(("line", WHITE, rng.uniform(*widths), line))
    return out


def icon_drawing(simple=False):
    """The program icon on a 512 canvas: a round stamp with an F."""
    rng = random.Random(15)
    radius, ring_width, f_scale = (226, 44, 2.35) if simple else (236, 24, 1.95)
    ring = roughen(circle(256, 256, radius), rng, 0.0 if simple else 1.6)
    drawing = [("poly", WHITE, [ring]), ("line", PURPLE, ring_width, closed(ring))]
    if not simple:
        drawing.append(("line", PURPLE, 7, closed(roughen(circle(256, 256, 199), rng, 1.2))))
    letter = [roughen(ring, rng, 0.0 if simple else 0.9) for ring in LETTER_F]
    drawing += placed([("poly", PURPLE, letter)], dx=262 - 51, dy=256 - 70, angle=-10, scale=f_scale, about=(51, 70))
    if not simple:
        def inside(r):
            a, d = r.uniform(0, math.tau), 240 * math.sqrt(r.random())
            return 256 + d * math.cos(a), 256 + d * math.sin(a)
        def clamp(x, y):
            d = math.hypot(x - 256, y - 256)
            f = min(1.0, 240 / d) if d else 1.0
            return 256 + (x - 256) * f, 256 + (y - 256) * f
        drawing += wear(rng, inside, clamp, 420, (1.5, 2.3, 3.3, 5.5), 6, (70, 190), (3, 7))
    return drawing, (512, 512)


def wordmark_drawing():
    """FOOM in a double frame, set askew like a stamp put down in a hurry."""
    rng = random.Random(15)
    w, h = 720, 212
    frame = roughen([(0, 0), (w, 0), (w, h), (0, h)], rng, 1.3, step=60)
    inner = roughen([(14, 14), (w - 14, 14), (w - 14, h - 14), (14, h - 14)], rng, 1.0, step=60)
    drawing = [("poly", WHITE, [frame]), ("line", PURPLE, 9, closed(frame)), ("line", PURPLE, 3, closed(inner))]
    for rings, x in ((LETTER_F, 52), (LETTER_O, 210), (LETTER_O, 370), (LETTER_M, 534)):
        letter = [roughen(ring, rng, 0.9) for ring in rings]
        drawing += placed([("poly", PURPLE, letter)], dx=x, dy=36 + rng.uniform(-3, 3), angle=rng.uniform(-1.5, 1.5), about=(55, 70))
    drawing += wear(rng, lambda r: (r.uniform(-2, w + 2), r.uniform(-2, h + 2)),
                    lambda x, y: (min(max(x, 0), w), min(max(y, 0), h)), 1300, (0.6, 0.9, 1.3, 2.2), 10, (30, 90), (1.5, 4))
    return placed(drawing, dx=16, dy=39, angle=-5, about=(w / 2, h / 2)), (752, 290)


def lockup_drawing():
    """Icon and wordmark side by side."""
    icon, _ = icon_drawing()
    word, (w, h) = wordmark_drawing()
    return placed(icon, scale=h / 512) + placed(word, dx=h + 16), (h + 16 + w, h)


def write_svg(path, drawing, size):
    w, h = size
    lines = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        '<!-- FOOM logo. Copyright 2026 Matej Šebo. Licensed under the GNU GPL, version 3 or later. -->',
        '<!-- Written by make_logo.py; change the script, not this file. -->',
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w} {h}" width="{w}" height="{h}"'
        ' stroke-linejoin="round" stroke-linecap="round">',
        '<title>FOOM</title>',
    ]
    for prim in drawing:
        if prim[0] == "poly":
            d = " ".join("M" + " L".join(f"{x:.1f},{y:.1f}" for x, y in ring) + " Z" for ring in prim[2])
            lines.append(f'<path fill="{prim[1]}" fill-rule="evenodd" d="{d}"/>')
        elif prim[0] == "line":
            pts = " ".join(f"{x:.1f},{y:.1f}" for x, y in prim[3])
            lines.append(f'<polyline fill="none" stroke="{prim[1]}" stroke-width="{prim[2]:.1f}" points="{pts}"/>')
        else:
            lines.append(f'<circle fill="{prim[1]}" cx="{prim[2]:.1f}" cy="{prim[3]:.1f}" r="{prim[4]:.1f}"/>')
    lines.append("</svg>")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def render(drawing, size, out_size):
    """Rasterises the drawing to out_size (width, height) on a transparent ground."""
    k = SUPERSAMPLE * out_size[0] / size[0]
    big = (out_size[0] * SUPERSAMPLE, out_size[1] * SUPERSAMPLE)
    image = Image.new("RGBA", big, (255, 255, 255, 0))
    draw = ImageDraw.Draw(image)
    for prim in drawing:
        if prim[0] == "poly":
            mask = Image.new("L", big, 0)
            mask_draw = ImageDraw.Draw(mask)
            for i, ring in enumerate(prim[2]):
                mask_draw.polygon([(x * k, y * k) for x, y in ring], fill=255 if i == 0 else 0)
            image.paste(Image.new("RGBA", big, prim[1]), (0, 0), mask)
            draw = ImageDraw.Draw(image)
        elif prim[0] == "line":
            pts = [(x * k, y * k) for x, y in prim[3]]
            width = max(1, round(prim[2] * k))
            draw.line(pts, fill=prim[1], width=width, joint="curve")
            for x, y in (pts[0], pts[-1]):
                draw.ellipse((x - width / 2, y - width / 2, x + width / 2, y + width / 2), fill=prim[1])
        else:
            x, y, r = prim[2] * k, prim[3] * k, prim[4] * k
            draw.ellipse((x - r, y - r, x + r, y + r), fill=prim[1])
    return image.resize(out_size, Image.LANCZOS)


def main():
    icon, icon_size = icon_drawing()
    small, _ = icon_drawing(simple=True)
    word, word_size = wordmark_drawing()
    lockup, lockup_size = lockup_drawing()

    write_svg(HERE / "foom-icon.svg", icon, icon_size)
    write_svg(HERE / "foom-icon-small.svg", small, icon_size)
    write_svg(HERE / "foom-wordmark.svg", word, word_size)
    write_svg(HERE / "foom-logo.svg", lockup, lockup_size)
    render(word, word_size, word_size).save(HERE / "foom-wordmark.png", optimize=True)
    render(lockup, lockup_size, lockup_size).save(HERE / "foom-logo.png", optimize=True)

    def icon_at(size):
        return render(small if size <= 48 else icon, icon_size, (size, size))

    icons = IMAGES / "mapper-icon"
    for size in (16, 24, 32, 36, 48, 72, 96, 128, 256, 512):
        icon_at(size).save(icons / f"Mapper-{size}.png", optimize=True)
    icon_at(128).save(IMAGES / "mapper.png", optimize=True)
    ico_sizes = (16, 24, 32, 48, 64, 128, 256)
    icon_at(256).save(icons / "Mapper.ico", sizes=[(s, s) for s in ico_sizes],
                      append_images=[icon_at(s) for s in ico_sizes[:-1]])
    icon_at(1024).save(icons / "Mapper.icns")


if __name__ == "__main__":
    main()
