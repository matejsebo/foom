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

"""Draws FOOM's own tool icons: the sketch layer and the GNSS commands.

Each icon is a list of simple shapes on a grid of 32 by 32 units. The script
writes the SVG beside itself (images/svg/<name>.svg) and the PNG of 32 by 32
pixels which the program uses (images/<name>.png). Needs Pillow:

    uv run --no-project --with pillow python images/svg/make_foom_icons.py

The sketch icons use the red of the first sketch pen; the GNSS icons use the
position marker of the GPS display, a red arrow on a grey disc, and the vertex
squares of the path tools. See the design notes.
"""

import math
from pathlib import Path

from PIL import Image, ImageDraw

SIZE = 32
SUPERSAMPLE = 8
HERE = Path(__file__).resolve().parent
IMAGES = HERE.parent

RED = "#e01010"
DARK = "#303030"
GREY = "#808080"
LIGHT = "#c8c8c8"
WHITE = "#ffffff"


# Shapes: ("poly", fill, outline or None, width, points)
#         ("line", colour, width, points)
#         ("circle", fill, outline or None, width, cx, cy, r)

def poly(fill, points, outline=None, width=1.0):
    return ("poly", fill, outline, width, points)


def line(colour, width, points):
    return ("line", colour, width, points)


def circle(fill, cx, cy, r, outline=None, width=1.0):
    return ("circle", fill, outline, width, cx, cy, r)


def wave(x0, y0, x1, y1, amplitude=2.5, waves=1.5, n=24):
    """A wavy stroke from (x0, y0) to (x1, y1), as a hand would draw it."""
    dx, dy = x1 - x0, y1 - y0
    length = math.hypot(dx, dy)
    nx, ny = -dy / length, dx / length
    points = []
    for i in range(n + 1):
        t = i / n
        offset = amplitude * math.sin(t * waves * 2 * math.pi)
        points.append((x0 + dx * t + nx * offset, y0 + dy * t + ny * offset))
    return points


def rotated_rect(cx, cy, w, h, angle_deg):
    a = math.radians(angle_deg)
    c, s = math.cos(a), math.sin(a)
    corners = [(-w / 2, -h / 2), (w / 2, -h / 2), (w / 2, h / 2), (-w / 2, h / 2)]
    return [(cx + x * c - y * s, cy + x * s + y * c) for x, y in corners]


def pencil(tail, tip, width=6.0):
    """A yellow pencil from its tail to its tip."""
    (x0, y0), (x1, y1) = tail, tip
    dx, dy = x1 - x0, y1 - y0
    length = math.hypot(dx, dy)
    ux, uy = dx / length, dy / length
    nx, ny = -uy, ux
    h = width / 2
    cone = 0.3 * length
    bx, by = x1 - ux * cone, y1 - uy * cone
    body = [(x0 + nx * h, y0 + ny * h), (bx + nx * h, by + ny * h), (bx - nx * h, by - ny * h), (x0 - nx * h, y0 - ny * h)]
    wood = [(bx + nx * h, by + ny * h), (x1, y1), (bx - nx * h, by - ny * h)]
    lx, ly = x1 - ux * cone * 0.35, y1 - uy * cone * 0.35
    lead = [(lx + nx * h * 0.35, ly + ny * h * 0.35), (x1, y1), (lx - nx * h * 0.35, ly - ny * h * 0.35)]
    ex, ey = x0 + ux * 2.5, y0 + uy * 2.5
    eraser = [(x0 + nx * h, y0 + ny * h), (ex + nx * h, ey + ny * h), (ex - nx * h, ey - ny * h), (x0 - nx * h, y0 - ny * h)]
    return [
        poly("#f2c12e", body, "#8a6508", 1.0),
        poly("#f1d9a8", wood, "#8a6508", 1.0),
        poly(DARK, lead),
        poly("#e88aa0", eraser, "#8a4458", 1.0),
    ]


def gps_marker(cx, cy, r, grey=False):
    """The position marker: a red arrow on a grey disc."""
    arrow = [(cx, cy - 0.72 * r), (cx + 0.5 * r, cy + 0.58 * r), (cx, cy + 0.28 * r), (cx - 0.5 * r, cy + 0.58 * r)]
    return [
        circle(LIGHT, cx, cy, r, "#606060", 1.0),
        poly(GREY if grey else RED, arrow, "#600000" if not grey else "#505050", 0.8),
    ]


def vertex(cx, cy, size=3.6):
    h = size / 2
    return poly(WHITE, [(cx - h, cy - h), (cx + h, cy - h), (cx + h, cy + h), (cx - h, cy + h)], DARK, 1.2)


def path_with_vertices(points):
    return [line(DARK, 1.8, points)] + [vertex(x, y) for x, y in points]


def arrow(x0, y0, x1, y1, colour=DARK, width=2.0, head=5.0):
    dx, dy = x1 - x0, y1 - y0
    length = math.hypot(dx, dy)
    ux, uy = dx / length, dy / length
    nx, ny = -uy, ux
    bx, by = x1 - ux * head, y1 - uy * head
    return [
        line(colour, width, [(x0, y0), (bx, by)]),
        poly(colour, [(x1, y1), (bx + nx * head * 0.55, by + ny * head * 0.55), (bx - nx * head * 0.55, by - ny * head * 0.55)]),
    ]


def crosshair(cx, cy, r, colour=RED):
    return [
        circle(None, cx, cy, r, colour, 1.6),
        line(colour, 1.4, [(cx - r - 2, cy), (cx - r + 2, cy)]),
        line(colour, 1.4, [(cx + r - 2, cy), (cx + r + 2, cy)]),
        line(colour, 1.4, [(cx, cy - r - 2), (cx, cy - r + 2)]),
        line(colour, 1.4, [(cx, cy + r - 2), (cx, cy + r + 2)]),
    ]


def arc(cx, cy, r, from_deg, to_deg, n=16):
    """Points on a circle; angles clockwise from north."""
    pts = []
    for i in range(n + 1):
        a = math.radians(from_deg + (to_deg - from_deg) * i / n)
        pts.append((cx + r * math.sin(a), cy - r * math.cos(a)))
    return pts


ICONS = {
    # The sketch layer
    "sketch-pen": [line(RED, 2.4, wave(3, 27, 17, 21, 2.2, 1.5))] + pencil((29, 3), (17, 20)),
    "sketch-eraser": [
        line(RED, 2.4, [(2, 26), (10, 26)]),
        line(RED, 2.4, [(22, 26), (30, 26)]),
        poly("#f4a6b8", rotated_rect(16, 14, 9, 18, 40), "#8a4458", 1.2),
        poly("#5b7fd0", rotated_rect(19.9, 9.4, 9, 7, 40), "#2c4580", 1.2),
    ],
    "sketch-note": [
        circle(RED, 8, 16, 4.2, "#800000", 1.0),
        line(RED, 2.4, [(15, 11), (28, 11)]),
        line(RED, 2.4, [(15, 16), (26, 16)]),
        line(RED, 2.4, [(15, 21), (23, 21)]),
    ],
    "sketch-manage": [
        line(RED, 2.2, wave(3, 7, 11, 7, 1.6, 1.0, 12)),
        line(GREY, 2.4, [(15, 7), (29, 7)]),
        circle(RED, 7, 16, 2.6),
        line(GREY, 2.4, [(15, 16), (27, 16)]),
        line(RED, 2.2, wave(3, 25, 11, 25, 1.6, 1.0, 12)),
        line(GREY, 2.4, [(15, 25), (29, 25)]),
    ],
    "sketch-flatten": arrow(16, 1.5, 16, 13, DARK, 2.2, 5) + [
        poly("#dfe9f7", [(4, 15), (28, 15), (28, 30), (4, 30)], "#45608f", 1.4),
        line(RED, 2.2, wave(7, 24, 25, 21, 2.0, 1.5)),
    ],
    "sketch-behind": [
        line(RED, 3.0, wave(2, 21, 30, 11, 2.0, 1.5)),
        poly("#7cc47c", [(9, 6), (23, 6), (23, 26), (9, 26)], "#2e7d32", 1.6),
    ],
}


def write_svg(path, name, shapes):
    out = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        f'<!-- FOOM icon {name}. Copyright 2026 Matej Šebo. Licensed under the GNU GPL, version 3 or later. -->',
        '<!-- Written by make_foom_icons.py; change the script, not this file. -->',
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {SIZE} {SIZE}" width="{SIZE}" height="{SIZE}"'
        ' stroke-linejoin="round" stroke-linecap="round">',
    ]
    for shape in shapes:
        kind = shape[0]
        if kind == "poly":
            _, fill, outline, width, points = shape
            pts = " ".join(f"{x:.2f},{y:.2f}" for x, y in points)
            stroke = f' stroke="{outline}" stroke-width="{width:.2f}"' if outline else ""
            out.append(f'<polygon fill="{fill}"{stroke} points="{pts}"/>')
        elif kind == "line":
            _, colour, width, points = shape
            pts = " ".join(f"{x:.2f},{y:.2f}" for x, y in points)
            out.append(f'<polyline fill="none" stroke="{colour}" stroke-width="{width:.2f}" points="{pts}"/>')
        else:
            _, fill, outline, width, cx, cy, r = shape
            stroke = f' stroke="{outline}" stroke-width="{width:.2f}"' if outline else ""
            out.append(f'<circle fill="{fill or "none"}"{stroke} cx="{cx:.2f}" cy="{cy:.2f}" r="{r:.2f}"/>')
    out.append("</svg>")
    path.write_text("\n".join(out) + "\n", encoding="utf-8", newline="\n")


def stroke_line(draw, colour, width, points):
    draw.line(points, fill=colour, width=max(1, round(width)), joint="curve")
    for x, y in (points[0], points[-1]):
        draw.ellipse((x - width / 2, y - width / 2, x + width / 2, y + width / 2), fill=colour)


def render(shapes):
    k = SUPERSAMPLE
    big = SIZE * k
    image = Image.new("RGBA", (big, big), (255, 255, 255, 0))
    draw = ImageDraw.Draw(image)
    for shape in shapes:
        kind = shape[0]
        if kind == "poly":
            _, fill, outline, width, points = shape
            pts = [(x * k, y * k) for x, y in points]
            draw.polygon(pts, fill=fill)
            if outline:
                stroke_line(draw, outline, width * k, pts + [pts[0]])
        elif kind == "line":
            _, colour, width, points = shape
            stroke_line(draw, colour, width * k, [(x * k, y * k) for x, y in points])
        else:
            _, fill, outline, width, cx, cy, r = shape
            x, y, rr = cx * k, cy * k, r * k
            if fill:
                draw.ellipse((x - rr, y - rr, x + rr, y + rr), fill=fill)
            if outline:
                w = width * k
                draw.ellipse((x - rr - w / 2, y - rr - w / 2, x + rr + w / 2, y + rr + w / 2), outline=outline, width=max(1, round(w)))
    return image.convert("RGBa").resize((SIZE, SIZE), Image.LANCZOS).convert("RGBA")


def main():
    for name, shapes in ICONS.items():
        write_svg(HERE / f"{name}.svg", name, shapes)
        render(shapes).save(IMAGES / f"{name}.png", optimize=True)
        print(name)


if __name__ == "__main__":
    main()
