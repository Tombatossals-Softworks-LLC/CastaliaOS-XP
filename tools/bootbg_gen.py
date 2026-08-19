#!/usr/bin/env python3
"""Castalia boot-menu background baker.

Renders the 640x480 boot-menu background (GRUB gfxmenu + isolinux vesamenu,
Bible §6.2, §14.1) as a PNG using only the Python standard library — a
deterministic build artifact derived from the boot-splash design
(branding/boot/splash.svg): the deep-sea radial field, a quiet wave band along
the bottom edge, and the Castalia mark in the lower right.

The mark is evaluated analytically from the same geometry as
branding/logo/castalia-mark.svg — the C ring as an annulus with a wedge
removed, the keep in its own transformed space, the hills from the very same
Bézier control points — and 2x2 super-sampled, because there is no vector
engine here and a hand-rolled circle otherwise looks like a staircase. The
one liberty taken: the window and doorway are rectangles rather than arches,
which at this size is a pixel or two.

Usage:
    PYTHONPATH=tools python3 tools/bootbg_gen.py            # -> iso/boot-bg/splash.png
    PYTHONPATH=tools python3 tools/bootbg_gen.py --out FILE
"""

from __future__ import annotations

import argparse
import bisect
import math
import struct
import sys
import zlib
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
W, H = 640, 480

# palette (from branding/boot/splash.svg)
GLOW_IN = (0x16, 0x34, 0x4E)     # radial centre
GLOW_OUT = (0x0A, 0x14, 0x1F)    # field edge
KEEP = (0x5A, 0x98, 0xD6)        # the keep's stone, read against the field
WINDOW = (0xF5, 0xD9, 0xA0)      # lit window
BAND = (0x0F, 0x22, 0x33)        # sea band
WAVE_LINE = (0x2C, 0x66, 0x99)   # azure crest
# the mark's own colours, muted for a dark boot field
RING = (0x33, 0x6E, 0xB2)        # the C
STONE_DARK = (0x38, 0x6E, 0xB0)  # the turrets, set back from the keep
ROOF = (0xC0, 0x33, 0x25)        # turret cones
DOOR = (0xE0, 0x87, 0x0E)        # the lit doorway
HILL_BACK = (0x5D, 0xA6, 0x34)
HILL_FRONT = (0x3D, 0x87, 0x24)
OUTLINE = (0x08, 0x18, 0x2C)     # the mark's navy edge

# The mark, on the 48-unit grid of branding/logo/castalia-mark.svg, placed in
# the lower right of the field.
MARK_CX, MARK_CY, MARK_S = 524.0, 352.0, 2.5
R_OUT, R_IN, GAP_DEG = 22.6, 13.4, 35.0
KEEP_DX, KEEP_DY, KEEP_S = 2.9, 1.7, 0.9      # the keep group's transform


def lerp(a: tuple, b: tuple, t: float) -> tuple:
    t = max(0.0, min(1.0, t))
    return tuple(round(ca + (cb - ca) * t) for ca, cb in zip(a, b))


def _bezier_profile(segments, steps: int = 240):
    """Sample chained quadratic Béziers into an (x, y) polyline.

    The hills in the mark are quadratic curves; the boot background has no
    vector engine, so we walk each curve once and then read heights off the
    samples. Chaining keeps the two segments of a hill continuous.
    """
    xs: list[float] = []
    ys: list[float] = []
    for (p0, c, p1) in segments:
        for i in range(steps + 1):
            t = i / steps
            u = 1 - t
            xs.append(u * u * p0[0] + 2 * u * t * c[0] + t * t * p1[0])
            ys.append(u * u * p0[1] + 2 * u * t * c[1] + t * t * p1[1])
    return xs, ys


# The two hills, exactly as branding/logo/castalia-mark.svg draws them.
HILL_BACK_CURVE = _bezier_profile([((2.58, 31.2), (13, 26.6), (24, 30.4)),
                                   ((24, 30.4), (35, 34.2), (45.79, 30))])
HILL_FRONT_CURVE = _bezier_profile([((4.15, 34.8), (14, 30.4), (26, 34.4)),
                                    ((26, 34.4), (38, 38.4), (44.17, 34.2))])


def _height_at(curve: tuple[list[float], list[float]], x: float):
    """The curve's y at x, or None where the curve does not span x.

    Both hills run strictly left to right, so a bisect over the sampled xs
    finds the bracketing pair in log time — this is called once per sub-sample
    of the badge, and a linear scan here would dominate the whole render.
    """
    xs, ys = curve
    if x < xs[0] or x > xs[-1]:
        return None
    i = bisect.bisect_left(xs, x)
    if i == 0:
        return ys[0]
    x0, x1 = xs[i - 1], xs[min(i, len(xs) - 1)]
    y0, y1 = ys[i - 1], ys[min(i, len(ys) - 1)]
    if x1 == x0:
        return y0
    t = (x - x0) / (x1 - x0)
    return y0 + (y1 - y0) * t


def _in_triangle(p, a, b, c) -> bool:
    def side(u, v, w):
        return ((v[0] - u[0]) * (w[1] - u[1])
                - (v[1] - u[1]) * (w[0] - u[0]))

    d1, d2, d3 = side(a, b, p), side(b, c, p), side(c, a, p)
    return not ((d1 < 0 or d2 < 0 or d3 < 0) and (d1 > 0 or d2 > 0 or d3 > 0))


def mark_colour(gx: float, gy: float):
    """The mark's colour at grid point (gx, gy), or None outside it.

    One sample of branding/logo/castalia-mark.svg, evaluated analytically:
    the C ring, then the keep in its own transformed space, then the hills.
    Painted back-to-front exactly like the SVG's element order.
    """
    colour = None
    dx, dy = gx - 24.0, gy - 24.0
    dist = math.hypot(dx, dy)

    # the C — an annulus with a 2*GAP_DEG bite out of its right side
    if R_IN <= dist <= R_OUT:
        angle = math.degrees(math.atan2(dy, dx))
        if abs(angle) > GAP_DEG:
            colour = OUTLINE if (dist > R_OUT - 0.8
                                 or dist < R_IN + 0.8) else RING

    # the keep (translate(2.9 1.7) scale(0.9) in the SVG → invert it here)
    kx, ky = (gx - KEEP_DX) / KEEP_S, (gy - KEEP_DY) / KEEP_S
    if _in_triangle((kx, ky), (13, 5.6), (17.8, 15.6), (8.2, 15.6)):
        colour = ROOF
    if _in_triangle((kx, ky), (29.1, 14), (33.4, 22.6), (24.8, 22.6)):
        colour = ROOF
    for x0, y0, x1, y1, tone in ((9.6, 15, 16.4, 36, STONE_DARK),
                                 (26.2, 22, 32.0, 36, STONE_DARK),
                                 (15.2, 10, 26.4, 36, KEEP),      # main body
                                 (15.2, 6.4, 18.2, 10.8, KEEP),   # merlons
                                 (19.3, 6.4, 22.3, 10.8, KEEP),
                                 (23.4, 6.4, 26.4, 10.8, KEEP)):
        if x0 <= kx <= x1 and y0 <= ky <= y1:
            colour = tone
    if 18.7 <= kx <= 22.9 and 14 <= ky <= 19.4:        # the lit window
        colour = WINDOW
    if 17.8 <= kx <= 23.8 and 27.4 <= ky <= 36:        # the doorway
        colour = DOOR

    # the hills, which close the badge along its own rim
    if dist <= R_OUT:
        back = _height_at(HILL_BACK_CURVE, gx)
        if back is not None and gy >= back:
            colour = HILL_BACK
        front = _height_at(HILL_FRONT_CURVE, gx)
        if front is not None and gy >= front:
            colour = HILL_FRONT
    return colour


def render() -> list[list[tuple]]:
    """Render the frame as rows of (r, g, b)."""
    cx, cy, radius = W / 2, H * 0.42, W * 0.62
    px = [[(0, 0, 0)] * W for _ in range(H)]

    # radial deep-sea field
    for y in range(H):
        for x in range(W):
            d = math.hypot(x - cx, y - cy) / radius
            px[y][x] = lerp(GLOW_IN, GLOW_OUT, d)

    # sea band with a gentle sine top edge + azure crest line
    for x in range(W):
        top = int(446 + 4 * math.sin(x / 26.0))
        for y in range(top, H):
            px[y][x] = BAND
        for y in range(top, min(top + 2, H)):
            px[y][x] = WAVE_LINE

    # The mark, lower right. Each pixel is 2x2 super-sampled against
    # mark_colour(); with no vector engine here that is what keeps the badge's
    # circle from looking like a staircase, and it costs one small region of
    # the frame rather than the whole thing.
    half = R_OUT * MARK_S + 2
    x0, x1 = int(MARK_CX - half), int(MARK_CX + half)
    y0, y1 = int(MARK_CY - half), int(MARK_CY + half)
    for y in range(max(0, y0), min(H, y1)):
        for x in range(max(0, x0), min(W, x1)):
            hit, acc = 0, [0, 0, 0]
            for sy in (0.25, 0.75):
                for sx in (0.25, 0.75):
                    colour = mark_colour((x + sx - MARK_CX) / MARK_S + 24.0,
                                         (y + sy - MARK_CY) / MARK_S + 24.0)
                    if colour is not None:
                        hit += 1
                        for i in range(3):
                            acc[i] += colour[i]
            if not hit:
                continue
            over = tuple(c // hit for c in acc)
            px[y][x] = over if hit == 4 else lerp(px[y][x], over, hit / 4)

    return px


def write_png(path: Path, px: list[list[tuple]]) -> None:
    raw = bytearray()
    for row in px:
        raw.append(0)                     # filter: none
        for r, g, b in row:
            raw += bytes((r, g, b))

    def chunk(tag: bytes, data: bytes) -> bytes:
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data)))

    ihdr = struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0)   # 8-bit RGB
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", ihdr)
           + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
           + chunk(b"IEND", b""))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(png)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path,
                        default=REPO / "iso" / "boot-bg" / "splash.png")
    args = parser.parse_args(argv[1:])
    write_png(args.out, render())
    rel = args.out.relative_to(REPO) if args.out.is_relative_to(REPO) else args.out
    print(f"bootbg-gen: wrote {rel} ({args.out.stat().st_size // 1024} KiB, "
          f"{W}x{H})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
