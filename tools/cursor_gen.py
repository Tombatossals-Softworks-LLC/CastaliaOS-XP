"""Generate the Castalia Xcursor theme from the original SVG cursor sources.

Builds a real X11 cursor theme — `themes/cursors/src/*.svg` become genuine
Xcursor binary files (multi-size, with hotspots), an `index.theme`, and the
symlink web of standard cursor names X11/GTK/Qt applications actually ask for.

The Xcursor container is written here in pure Python (`pack_xcursor`), and so
is the PNG decode (`decode_png_rgba`) — the build needs neither `xcursorgen`
nor Pillow. Only the SVG rasterisation shells out, preferring `rsvg-convert`
(librsvg) and falling back to ImageMagick. That split also lets the format
writer and the decoder be unit tested with no image tooling present at all
(see tools/tests/test_cursor_gen.py).

File format (all little-endian), per the Xcursor specification:

    header:  magic "Xcur" | header size (16) | version | ntoc
    toc[n]:  type | subtype | byte position
    image:   header (36) | type | subtype (nominal size) | version
             | width | height | xhot | yhot | delay | w*h ARGB pixels

Pixels are premultiplied ARGB32, which is what the X Render extension expects
(and what xcursorgen produces).

Usage:
    python3 tools/cursor_gen.py [--out build/out/cursors] [--sizes 24,32,48]
"""

import argparse
import os
import shutil
import struct
import subprocess
import sys
import zlib
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
SRC = REPO / "themes" / "cursors" / "src"

THEME_ID = "Castalia-Human"
THEME_NAME = "Castalia Human"
THEME_COMMENT = "Castalia OS — Human pointers (original artwork)"

CHUNK_IMAGE = 0xFFFD0002
FONT_RE = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
DEFAULT_SIZES = (24, 32, 48)

# Hotspot for each shape, in the 24x24 source grid; scaled with the size.
HOTSPOTS = {
    "left_ptr": (1, 1),
    "question_arrow": (1, 1),
    "hand2": (8, 2),
    "xterm": (12, 12),
    "watch": (12, 12),
    "fleur": (12, 12),
    "sb_h_double_arrow": (12, 12),
    "sb_v_double_arrow": (12, 12),
    "crosshair": (12, 12),
}

# The standard names applications ask for, mapped to the shape that serves
# them. Includes the well-known hashed names some toolkits still request.
ALIASES = {
    "left_ptr": [
        "default", "arrow", "top_left_arrow", "left_arrow",
    ],
    "xterm": ["text", "ibeam"],
    "watch": [
        "wait", "left_ptr_watch", "progress",
        "00000000000000020006000e7e9ffc3f",
        "08e8e1c95fe2fc01f976f1e063a24ccd",
        "3ecb610c1bf2410f44200f48c40d3599",
    ],
    "hand2": [
        "hand1", "hand", "pointer", "pointing_hand",
        "9d800788f1b08800ae810202380a0822",
        "e29285e634086352946a0e7090d73106",
    ],
    "fleur": ["move", "size_all", "all-scroll"],
    "sb_h_double_arrow": [
        "ew-resize", "h_double_arrow", "col-resize", "size_hor",
        "028006030e0e7ebffc7f7070c0600140",
    ],
    "sb_v_double_arrow": [
        "ns-resize", "v_double_arrow", "row-resize", "size_ver",
        "00008160000006810000408080010102",
    ],
    "crosshair": ["cross", "tcross", "cell"],
    "question_arrow": [
        "help", "whats_this", "left_ptr_help", "dnd-ask",
        "d9ce0ab605698f320427677b458ad60b",
    ],
}


def pack_xcursor(images):
    """Pack images into an Xcursor file.

    `images` is a sequence of dicts with keys: size, width, height, xhot,
    yhot, delay, pixels — where `pixels` is a bytes-like of width*height
    premultiplied ARGB values already packed little-endian (4 bytes each).
    Returns the complete file as bytes.
    """
    images = list(images)
    ntoc = len(images)
    header = struct.pack("<4sIII", b"Xcur", 16, 0x00010000, ntoc)
    toc_size = 12 * ntoc
    pos = len(header) + toc_size

    toc = b""
    chunks = b""
    for im in images:
        toc += struct.pack("<III", CHUNK_IMAGE, im["size"], pos)
        chunk = struct.pack(
            "<IIIIIIIII", 36, CHUNK_IMAGE, im["size"], 1,
            im["width"], im["height"], im["xhot"], im["yhot"], im["delay"],
        ) + bytes(im["pixels"])
        chunks += chunk
        pos += len(chunk)
    return header + toc + chunks


def premultiplied_argb(rgba: bytes) -> bytes:
    """Convert raw RGBA bytes to packed, premultiplied ARGB (little-endian)."""
    out = bytearray(len(rgba))
    for i in range(0, len(rgba), 4):
        r, g, b, a = rgba[i], rgba[i + 1], rgba[i + 2], rgba[i + 3]
        if a != 255:
            r = (r * a + 127) // 255
            g = (g * a + 127) // 255
            b = (b * a + 127) // 255
        # little-endian 0xAARRGGBB → B, G, R, A on disk
        out[i] = b
        out[i + 1] = g
        out[i + 2] = r
        out[i + 3] = a
    return bytes(out)


def decode_png_rgba(data: bytes):
    """Decode a PNG into (width, height, RGBA bytes) — pure Python.

    Supports the subset rasterisers emit here: 8-bit truecolour with or
    without alpha, non-interlaced. Keeping this in-tree means the build does
    not need Pillow, and it lets the decoder be unit tested anywhere.
    """
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    pos = 8
    width = height = depth = colour = interlace = None
    idat = bytearray()
    while pos < len(data):
        (length,) = struct.unpack_from(">I", data, pos)
        ctype = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length  # length + type + data + crc
        if ctype == b"IHDR":
            (width, height, depth, colour, _comp, _filt,
             interlace) = struct.unpack(">IIBBBBB", body)
        elif ctype == b"IDAT":
            idat += body
        elif ctype == b"IEND":
            break
    if depth != 8 or colour not in (2, 6) or interlace != 0:
        raise ValueError(
            f"unsupported PNG (depth={depth} colour={colour} "
            f"interlace={interlace})")

    channels = 4 if colour == 6 else 3
    raw = zlib.decompress(bytes(idat))
    stride = width * channels
    out = bytearray(width * height * 4)
    prev = bytearray(stride)
    ipos = 0
    for y in range(height):
        ftype = raw[ipos]
        ipos += 1
        line = bytearray(raw[ipos:ipos + stride])
        ipos += stride
        # Undo the per-scanline filter (PNG spec §9.2).
        if ftype == 1:      # Sub
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 0xFF
        elif ftype == 2:    # Up
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif ftype == 3:    # Average
            for i in range(stride):
                left = line[i - channels] if i >= channels else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif ftype == 4:    # Paeth
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                b = prev[i]
                c = prev[i - channels] if i >= channels else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pred) & 0xFF
        elif ftype != 0:
            raise ValueError(f"bad PNG filter type {ftype}")
        prev = line

        for x in range(width):
            s = x * channels
            d = (y * width + x) * 4
            out[d] = line[s]
            out[d + 1] = line[s + 1]
            out[d + 2] = line[s + 2]
            out[d + 3] = line[s + 3] if channels == 4 else 255
    return width, height, bytes(out)


# Rasterisers we know how to drive, best first. rsvg-convert is librsvg — the
# renderer that actually understands SVG. ImageMagick is only a fallback: its
# SVG support depends on a delegate that plain `imagemagick` installs lack, so
# relying on it alone makes the build fragile (it broke CI once already).
RASTERISERS = (
    ("rsvg-convert", lambda svg, size: [
        "rsvg-convert", "-w", str(size), "-h", str(size),
        "-f", "png", str(svg)]),
    ("magick", lambda svg, size: [
        "magick", "-background", "none", "-density", str(size * 16),
        str(svg), "-resize", f"{size}x{size}", "png:-"]),
    ("convert", lambda svg, size: [
        "convert", "-background", "none", "-density", str(size * 16),
        str(svg), "-resize", f"{size}x{size}", "png:-"]),
)


def find_rasteriser():
    """The first available rasteriser, as (name, argv builder)."""
    for name, argv in RASTERISERS:
        if shutil.which(name):
            return name, argv
    raise RuntimeError(
        "no SVG rasteriser found — install librsvg2-bin (rsvg-convert) "
        "or imagemagick")


def rasterise(svg: Path, size: int, rasteriser=None) -> bytes:
    """Rasterise an SVG to raw RGBA bytes at size x size."""
    name, argv = rasteriser or find_rasteriser()
    proc = subprocess.run(argv(svg, size), capture_output=True)
    if proc.returncode != 0:
        err = proc.stderr.decode("utf-8", "replace").strip()
        raise RuntimeError(f"{name} failed on {svg.name}@{size}: {err}")
    w, h, rgba = decode_png_rgba(proc.stdout)
    if (w, h) != (size, size):
        raise RuntimeError(f"{svg.name}: got {w}x{h}, expected {size}x{size}")
    return rgba


def build_cursor(name: str, sizes, rasteriser=None) -> bytes:
    svg = SRC / f"{name}.svg"
    hx, hy = HOTSPOTS[name]
    images = []
    for size in sizes:
        rgba = rasterise(svg, size, rasteriser)
        images.append({
            "size": size,
            "width": size,
            "height": size,
            "xhot": round(hx * size / 24),
            "yhot": round(hy * size / 24),
            "delay": 0,
            "pixels": premultiplied_argb(rgba),
        })
    return pack_xcursor(images)


def write_preview(path: Path, shapes) -> None:
    """Render a contact sheet of the pointer set (press kit / docs)."""
    path.parent.mkdir(parents=True, exist_ok=True)
    tiles = [str(SRC / f"{n}.svg") for n in shapes]
    if not shutil.which("montage"):
        print("cursor_gen: no ImageMagick montage — skipping the preview")
        return
    subprocess.run(
        ["montage", *tiles, "-tile", f"{len(tiles)}x", "-background", "#EFEBE7",
         "-fill", "#8A7A67", "-font", FONT_RE, "-pointsize", "13",
         "-label", "%t", "-geometry", "96x96+10+8", str(path)],
        check=True,
    )
    print(f"cursor_gen: preview -> {path}")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(REPO / "build" / "out" / "cursors"),
                    help="output directory (the theme dir is created inside)")
    ap.add_argument("--sizes", default=",".join(str(s) for s in DEFAULT_SIZES),
                    help="comma-separated nominal sizes to render")
    ap.add_argument("--preview", metavar="PNG",
                    help="also write a contact sheet of the pointer set")
    args = ap.parse_args()
    sizes = [int(s) for s in args.sizes.split(",") if s.strip()]

    theme_dir = Path(args.out) / THEME_ID
    cur_dir = theme_dir / "cursors"
    cur_dir.mkdir(parents=True, exist_ok=True)

    shapes = sorted(p.stem for p in SRC.glob("*.svg"))
    if not shapes:
        print("cursor_gen: no cursor sources found", file=sys.stderr)
        return 1
    missing = [s for s in shapes if s not in HOTSPOTS]
    if missing:
        print(f"cursor_gen: no hotspot defined for: {', '.join(missing)}",
              file=sys.stderr)
        return 1

    rasteriser = find_rasteriser()
    print(f"cursor_gen: rasterising with {rasteriser[0]}")
    links = 0
    for name in shapes:
        (cur_dir / name).write_bytes(build_cursor(name, sizes, rasteriser))
        for alias in ALIASES.get(name, []):
            link = cur_dir / alias
            if link.is_symlink() or link.exists():
                link.unlink()
            os.symlink(name, link)
            links += 1

    (theme_dir / "index.theme").write_text(
        "[Icon Theme]\n"
        f"Name={THEME_NAME}\n"
        f"Comment={THEME_COMMENT}\n"
        "Inherits=hicolor\n"
    )
    print(f"cursor_gen: {theme_dir} "
          f"({len(shapes)} cursors x {len(sizes)} sizes, {links} aliases)")
    if args.preview:
        write_preview(Path(args.preview), shapes)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
