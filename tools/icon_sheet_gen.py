"""Generate the Castalia OS icon-family contact sheet for the press kit.

Composes every icon in ``themes/icons/48/`` (the source of truth) into one
labelled sheet with a chocolate title band, and mirrors the SVGs into
``presskit/icons/`` so the kit stays a faithful, reproducible snapshot. Run
it whenever the icon family changes; the counts in the docs follow the same
number it prints.

Requires ImageMagick (montage + convert) and the DejaVu fonts.

Usage:
    python3 tools/icon_sheet_gen.py [--out presskit/icons]
"""

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
ICONS = REPO / "themes" / "icons" / "48"

PAPER = "#EFEBE7"
INK = "#3E3028"
MUTED = "#8A7A67"
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
FONT_RE = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"

COLUMNS = 7
TILE_W = 336
TILE_H = 240
SHEET_W = COLUMNS * TILE_W


def run(cmd: list[str]) -> None:
    subprocess.run(cmd, check=True)


def build(out_dir: Path) -> int:
    svgs = sorted(ICONS.glob("*.svg"))
    if not svgs:
        print("icon_sheet_gen: no icons found", file=sys.stderr)
        return 0
    out_dir.mkdir(parents=True, exist_ok=True)

    # Mirror the SVGs into the press kit so it ships the same geometry.
    for svg in svgs:
        dest = out_dir / svg.name
        if not dest.exists() or dest.read_bytes() != svg.read_bytes():
            shutil.copyfile(svg, dest)

    grid = out_dir / "_grid.png"
    sheet = out_dir / "icon-family-sheet.png"
    band = out_dir / "_band.png"

    # The labelled grid: each icon rasterised large and centred on paper, its
    # file stem (the icon's name) beneath it in muted ink.
    run([
        "montage", *[str(s) for s in svgs],
        "-tile", f"{COLUMNS}x", "-background", PAPER,
        "-fill", MUTED, "-font", FONT_RE, "-pointsize", "22",
        "-label", "%t", "-geometry", f"{TILE_W - 32}x{TILE_H - 40}+16+12",
        str(grid),
    ])

    # A title band that states the current icon count — the honest number.
    count = len(svgs)
    rows = (count + COLUMNS - 1) // COLUMNS
    subtitle = (f"{count} original SVG icons · one 48-px grid · "
                "Tombatossals Softworks")
    run([
        "convert", "-size", f"{SHEET_W}x220", f"xc:{PAPER}",
        "-font", FONT, "-fill", INK, "-pointsize", "72",
        "-gravity", "West", "-annotate", "+64+-26", "Castalia OS",
        "-font", FONT_RE, "-fill", MUTED, "-pointsize", "30",
        "-annotate", "+66+42", subtitle,
        str(band),
    ])

    run(["convert", "-background", PAPER, "-append", str(band), str(grid),
         str(sheet)])
    grid.unlink(missing_ok=True)
    band.unlink(missing_ok=True)
    print(f"icon_sheet_gen: {sheet} ({count} icons, {rows} rows)")
    return count


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(REPO / "presskit" / "icons"),
                    help="output directory for the sheet + mirrored SVGs")
    args = ap.parse_args()
    build(Path(args.out))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
