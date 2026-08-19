"""Tests for the Castalia mark — the one asset that appears everywhere.

The logo exists in two expressions. `branding/logo/castalia-logo.png` is **the
artwork**: what the shell paints at 32 px and above, what the press kit ships,
what a person means when they say "the logo". `branding/logo/castalia-mark.svg`
is the **vector edition** of the same mark: it draws crisply at 16 and 24 px
where the artwork turns to mush, and it is what the boot splash, the login
banner and the boot-menu baker embed, since those pipelines need geometry.

The vector edition therefore has to keep matching the artwork, and SVG has no way
to reference another document's shapes, so the mark is necessarily *copied*
into the boot splash, the login banner and the press kit, and *re-drawn* in
two other languages: `castalia::drawMark()` (QPainter, for the Start orb and
a dozen apps) and `tools/bootbg_gen.py` (pure-Python pixels, for the boot
menu background).

Five copies of one logo is exactly the shape of problem that rots — someone
adjusts the SVG, the Start button keeps the old shape for a year, and nobody
notices until a press render sits next to a screenshot. So this pins the
numbers that define the mark in every place it is expressed, and pins that
the retired v0.1 keep is gone from all of them.
"""

import re
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
ARTWORK = REPO / "branding" / "logo" / "castalia-logo.png"
MARK = REPO / "branding" / "logo" / "castalia-mark.svg"
PRESSKIT_MARK = REPO / "presskit" / "logos" / "castalia-mark.svg"
PRESSKIT_ARTWORK = REPO / "presskit" / "logos" / "castalia-logo.png"
SPLASH = REPO / "branding" / "boot" / "splash.svg"
BANNER = REPO / "branding" / "login" / "banner.svg"
PRESSKIT_BANNER = REPO / "presskit" / "logos" / "castalia-wordmark-banner.svg"
MARK_CPP = REPO / "shell" / "libcastalia-ui" / "Mark.cpp"
MARK_H = REPO / "shell" / "libcastalia-ui" / "Mark.h"
PROVENANCE = REPO / "legal" / "ASSET_PROVENANCE.csv"
HUMAN_THEME = REPO / "themes" / "human" / "theme.conf"
GREETER = REPO / "services" / "lightdm" / "lightdm-gtk-greeter.conf"
DESKTOP_CPP = REPO / "shell" / "desktop" / "src" / "DesktopWindow.cpp"
DEFAULT_WALLPAPER = "branding/wallpapers/valle-de-castalia.jpg"
BOOTBG = REPO / "tools" / "bootbg_gen.py"

# The C, as drawn: the arc endpoints at ±35° on a 22.6 outer / 13.4 inner ring.
RING_PATH_START = "M 42.52 11.04"
RING_ARCS = ("A 22.6 22.6 0 1 0 42.52 36.96", "A 13.4 13.4 0 1 1 34.98 16.31")
# The keep's group transform, which every re-drawing has to reproduce.
KEEP_TRANSFORM = "translate(2.9 1.7) scale(0.9)"
# A fragment of the retired v0.1 mark (the sandstone keep's arched door).
RETIRED = "M 21 37 L 21 29"


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def markup(path: Path) -> str:
    """The file with its XML comments removed — so a rule that is *explained*
    in a comment ("no clip path here, because…") does not read as a breach of
    itself."""
    return re.sub(r"<!--.*?-->", "", read(path), flags=re.S)


class ArtworkTest(unittest.TestCase):
    """The PNG is the logo; the vector is its stand-in at small sizes."""

    def test_artwork_ships_and_is_a_png(self):
        self.assertTrue(ARTWORK.is_file())
        self.assertTrue(ARTWORK.read_bytes().startswith(b"\x89PNG\r\n\x1a\n"))

    def test_artwork_has_an_alpha_channel(self):
        """The mark sits on the panel's orange and on a chocolate plate — a
        logo with a baked-in background would show its box on both."""
        colour_type = ARTWORK.read_bytes()[25]
        self.assertIn(colour_type, (4, 6), "artwork must carry alpha")

    def test_press_kit_ships_the_same_artwork(self):
        self.assertEqual(ARTWORK.read_bytes(), PRESSKIT_ARTWORK.read_bytes())

    def test_both_expressions_are_provenance_tracked(self):
        csv = read(PROVENANCE)
        self.assertIn("branding/logo/castalia-logo.png", csv)
        self.assertIn("branding/logo/castalia-mark.svg", csv)

    def test_the_painter_prefers_the_artwork_and_can_fall_back(self):
        cpp, hdr = read(MARK_CPP), read(MARK_H)
        self.assertIn("castalia-logo.png", cpp)
        self.assertIn("drawMarkVector", hdr)
        # the documented small-size rule, so nobody "simplifies" it away
        self.assertIn("< 32.0", cpp)


class DefaultWallpaperTest(unittest.TestCase):
    def test_the_wallpaper_ships(self):
        wall = REPO / DEFAULT_WALLPAPER
        self.assertTrue(wall.is_file())
        self.assertTrue(wall.read_bytes().startswith(b"\xff\xd8"), "JPEG")

    def test_every_surface_points_at_it(self):
        """The flagship theme, the desktop's own fallback and the greeter must
        agree — a desktop and a login screen with different backgrounds is the
        kind of seam users notice immediately."""
        self.assertIn(DEFAULT_WALLPAPER, read(HUMAN_THEME))
        self.assertIn(DEFAULT_WALLPAPER, read(DESKTOP_CPP))
        self.assertIn(DEFAULT_WALLPAPER, read(GREETER))

    def test_it_is_provenance_tracked(self):
        self.assertIn(DEFAULT_WALLPAPER, read(PROVENANCE))

    def test_raster_wallpapers_are_decoded_scaled(self):
        """2560x1664 decoded in full is ~17 MB — a third of what a FLOOR-tier
        machine has for everything (§16)."""
        cpp = read(DESKTOP_CPP)
        self.assertIn("QImageReader", cpp)
        self.assertIn("setScaledSize", cpp)


class MarkSourceTest(unittest.TestCase):
    def test_the_master_draws_the_c(self):
        svg = read(MARK)
        self.assertIn(RING_PATH_START, svg)
        for arc in RING_ARCS:
            self.assertIn(arc, svg)
        self.assertIn(KEEP_TRANSFORM, svg)

    def test_no_clip_path(self):
        """Qt 5's QSvgRenderer ignores <clipPath>, and it is what paints this
        file in the shell — a mark that needs one only looks right in a
        browser."""
        self.assertNotIn("clipPath", markup(MARK))
        self.assertNotIn("clip-path", markup(MARK))

    def test_no_external_dependencies(self):
        """Geometry only: no fonts, no embedded rasters, no filters."""
        svg = markup(MARK)
        for forbidden in ("<text", "<image", "font-family", "filter=",
                          "data:image"):
            self.assertNotIn(forbidden, svg)

    def test_press_kit_copy_is_identical(self):
        self.assertEqual(read(MARK), read(PRESSKIT_MARK),
                         "presskit/logos/castalia-mark.svg has drifted from "
                         "branding/logo/castalia-mark.svg")


class EmbeddedCopiesTest(unittest.TestCase):
    def test_boot_splash_embeds_the_mark(self):
        svg = read(SPLASH)
        self.assertIn(RING_PATH_START, svg)
        self.assertIn(KEEP_TRANSFORM, svg)

    def test_login_banner_embeds_the_mark(self):
        svg = read(BANNER)
        self.assertIn(RING_PATH_START, svg)
        self.assertIn(KEEP_TRANSFORM, svg)

    def test_press_kit_banner_matches_the_login_banner(self):
        self.assertEqual(read(BANNER), read(PRESSKIT_BANNER))

    def test_embedded_gradient_ids_are_namespaced(self):
        """The copies live in documents with their own <defs>; unprefixed ids
        would collide (and silently repaint half the artwork)."""
        for path in (SPLASH, BANNER):
            svg = read(path)
            self.assertIn('id="mk-ring"', svg, str(path))
            self.assertNotIn('id="ring"', svg, str(path))


class NativeRedrawingsTest(unittest.TestCase):
    def test_qpainter_mark_uses_the_same_geometry(self):
        cpp = read(MARK_CPP)
        # outer r 22.6 about (24,24) → the rect the arcs are inscribed in
        self.assertIn("QRectF outer(1.4, 1.4, 45.2, 45.2)", cpp)
        self.assertIn("QRectF inner(10.6, 10.6, 26.8, 26.8)", cpp)
        self.assertIn("arcTo(outer, 35, 290)", cpp)
        self.assertIn("translate(2.9, 1.7)", cpp)
        self.assertIn("scale(0.9, 0.9)", cpp)

    def test_qpainter_mark_repeats_the_hill_control_points(self):
        cpp = read(MARK_CPP)
        for point in ("2.58, 31.2", "45.79, 30", "4.15, 34.8", "44.17, 34.2"):
            self.assertIn(point, cpp, point)

    def test_boot_background_uses_the_same_constants(self):
        src = read(BOOTBG)
        self.assertIn("R_OUT, R_IN, GAP_DEG = 22.6, 13.4, 35.0", src)
        self.assertIn("KEEP_DX, KEEP_DY, KEEP_S = 2.9, 1.7, 0.9", src)
        # the hills come from the same Bézier control points as the SVG
        for point in ("(2.58, 31.2)", "(45.79, 30)", "(4.15, 34.8)",
                      "(44.17, 34.2)"):
            self.assertIn(point, src, point)

    def test_boot_background_still_paints_the_mark(self):
        import sys
        sys.path.insert(0, str(REPO / "tools"))
        import bootbg_gen

        # the centre of the ring's left arm is ring-coloured…
        self.assertEqual(bootbg_gen.mark_colour(6, 24), bootbg_gen.RING)
        # …the gap on the right is empty…
        self.assertIsNone(bootbg_gen.mark_colour(44, 24))
        # …and the bottom of the badge is hillside.
        self.assertEqual(bootbg_gen.mark_colour(24, 42),
                         bootbg_gen.HILL_FRONT)


class RetiredMarkTest(unittest.TestCase):
    def test_v01_keep_is_gone_everywhere(self):
        for path in (MARK, PRESSKIT_MARK, SPLASH, BANNER, PRESSKIT_BANNER):
            self.assertNotIn(RETIRED, read(path), f"{path} still ships the "
                                                  f"retired v0.1 mark")

    def test_no_sandstone_keep_left_in_the_logo_files(self):
        """#D8C49A was the v0.1 mark's sandstone; the wallpapers may still use
        the colour as scenery, but the logo files must not."""
        for path in (MARK, PRESSKIT_MARK):
            self.assertNotIn("#D8C49A", read(path), str(path))


if __name__ == "__main__":
    unittest.main()
