"""Rules about the *files* we ship, not their contents.

Two kinds of mistake are easy to make with binary assets and impossible to
notice by reading a diff:

1. **A format the shipped stack cannot decode.** Qt 5's qtbase ships image
   plugins for PNG, JPEG, GIF, ICO, BMP and SVG — and *not* WebP, which lives
   in the separate `qt5-image-formats-plugins` package. A WebP wallpaper looks
   fine in a browser and lands on the user's desktop as a black rectangle.
   Adding that package as a runtime dependency to decode a wallpaper is not a
   trade the FLOOR tier (§16) should make, so the rule is: convert first.

2. **A file name that borrows Microsoft's.** §3 is categorical about assets,
   and a file name travels further than the file — into the `.deb`, the ISO,
   the greeter config, and any label derived from it.

Both are cheap to check and expensive to discover late.
"""

import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
ASSET_DIRS = ["branding", "themes"]

# What the shipped Qt can actually decode, plus the source formats we keep.
ALLOWED_SUFFIXES = {
    ".svg", ".png", ".jpg", ".jpeg", ".gif", ".ico", ".bmp",   # images
    ".wav", ".toml", ".conf", ".md", ".csv", ".txt", ".xml",   # data & docs
}

# Formats that need a plugin the base install does not have.
UNSUPPORTED_IMAGE_SUFFIXES = {".webp", ".avif", ".jxl", ".heic", ".tiff",
                              ".tif"}

# Names Microsoft made famous. Substring match, case-insensitive, on the file
# name only — prose may of course *discuss* Windows.
MICROSOFT_ECHOES = ("bliss", "luna", "aero", "windows", "microsoft", "msft",
                    "clippy", "zune", "cortana", "wingdings")


def shipped_assets() -> list[Path]:
    out = []
    for d in ASSET_DIRS:
        for p in sorted((REPO / d).rglob("*")):
            if p.is_file():
                out.append(p)
    return out


class AssetFormatTest(unittest.TestCase):
    def test_no_undecodable_image_formats(self):
        for p in shipped_assets():
            self.assertNotIn(
                p.suffix.lower(), UNSUPPORTED_IMAGE_SUFFIXES,
                f"{p.relative_to(REPO)}: qtbase ships no plugin for this "
                f"format — convert it to JPEG or PNG")

    def test_every_shipped_file_has_a_known_suffix(self):
        """A new extension should be a deliberate decision, not a surprise at
        install time."""
        for p in shipped_assets():
            self.assertIn(p.suffix.lower(), ALLOWED_SUFFIXES,
                          f"{p.relative_to(REPO)}: unexpected file type")

    def test_wallpapers_are_not_absurdly_large(self):
        """Every wallpaper ships in the ISO and the .deb. 2 MB each is already
        generous for a JPEG at 2560x1664; a multi-megabyte PNG of a photo is a
        packaging mistake, not a design choice."""
        for p in sorted((REPO / "branding" / "wallpapers").iterdir()):
            if p.is_file():
                self.assertLess(p.stat().st_size, 2 * 1024 * 1024,
                                f"{p.name} is {p.stat().st_size // 1024} KiB")


class AssetNamingTest(unittest.TestCase):
    def test_no_microsoft_echoes_in_asset_names(self):
        for p in shipped_assets():
            lowered = p.name.lower()
            for echo in MICROSOFT_ECHOES:
                self.assertNotIn(
                    echo, lowered,
                    f"{p.relative_to(REPO)}: the name borrows Microsoft's "
                    f"({echo!r}) — §3 keeps our assets our own")

    def test_asset_names_are_lowercase_kebab(self):
        for p in shipped_assets():
            if p.name in ("README.md", "SCHEMA.md"):
                continue          # documentation keeps its shouted convention
            if "cursors" in p.parts:
                continue          # X11 cursor names are the spec's, not ours
                                  # (left_ptr, sb_v_double_arrow, …)
            stem = p.stem
            self.assertEqual(stem, stem.lower(), f"{p.name} is not lowercase")
            self.assertNotIn(" ", p.name, f"{p.name} contains a space")
            self.assertNotIn("_", stem, f"{p.name} uses an underscore")


if __name__ == "__main__":
    unittest.main()
