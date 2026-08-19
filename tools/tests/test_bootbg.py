"""Unit tests for the boot-menu background baker and boot menu configs."""

import struct
import sys
import unittest
import zlib
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))

import bootbg_gen  # noqa: E402

REPO = bootbg_gen.REPO


class BootBgTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.px = bootbg_gen.render()

    def test_dimensions(self):
        self.assertEqual(len(self.px), bootbg_gen.H)
        self.assertEqual(len(self.px[0]), bootbg_gen.W)

    def test_deterministic(self):
        self.assertEqual(self.px[240][320], bootbg_gen.render()[240][320])

    def test_lit_window_present(self):
        flat = {c for row in self.px for c in row}
        self.assertIn(bootbg_gen.WINDOW, flat)
        self.assertIn(bootbg_gen.WAVE_LINE, flat)
        self.assertIn(bootbg_gen.KEEP, flat)

    def test_png_structure(self):
        out = REPO / "iso" / "boot-bg" / "splash.png"
        data = out.read_bytes()
        self.assertTrue(data.startswith(b"\x89PNG\r\n\x1a\n"))
        w, h = struct.unpack(">II", data[16:24])
        self.assertEqual((w, h), (bootbg_gen.W, bootbg_gen.H))
        # IDAT decompresses to exactly H * (1 + W*3) filtered bytes
        idat_start = data.index(b"IDAT") + 4
        idat_len = struct.unpack(">I", data[data.index(b"IDAT") - 4:
                                            data.index(b"IDAT")])[0]
        raw = zlib.decompress(data[idat_start:idat_start + idat_len])
        self.assertEqual(len(raw), bootbg_gen.H * (1 + bootbg_gen.W * 3))


class BootMenuConfigTest(unittest.TestCase):
    def test_grub_menu_has_the_bible_entries(self):
        cfg = (REPO / "iso" / "grub" / "grub.cfg").read_text(encoding="utf-8")
        for needle in ('menuentry "Castalia Classic"', "Safe Mode",
                       "Recovery", "memtest", "castalia.safemode=1",
                       "castalia.recovery=1", "set theme="):
            self.assertIn(needle, cfg, needle)

    def test_grub_theme_uses_castalia_colors(self):
        theme = (REPO / "iso" / "grub" / "theme" / "theme.txt")\
            .read_text(encoding="utf-8")
        for needle in ("splash.png", "#3E82B6", "boot_menu",
                       "Tombatossals Softworks", "C A S T A L I A"):
            self.assertIn(needle, theme, needle)

    def test_isolinux_menu_has_the_bible_entries(self):
        # The template mkiso.sh renders, plus the install entries it splices
        # in. This used to read iso/isolinux/isolinux.cfg — a file the build
        # ignored — so it happily passed on "LABEL memtest" and
        # "castalia.installer=text" while neither ever reached an ISO.
        cfg = (REPO / "iso" / "isolinux" / "isolinux.cfg.in")\
            .read_text(encoding="utf-8")
        cfg += (REPO / "iso" / "isolinux" / "entries-install.cfg")\
            .read_text(encoding="utf-8")
        for needle in ("UI vesamenu.c32", "MENU BACKGROUND splash.png",
                       "LABEL live", "LABEL livesafe", "LABEL textinstall",
                       "LABEL install", "nomodeset",
                       "castalia.installer=text", "castalia.installer=gui"):
            self.assertIn(needle, cfg, needle)

    def test_isolinux_selection_uses_azure(self):
        cfg = (REPO / "iso" / "isolinux" / "isolinux.cfg.in")\
            .read_text(encoding="utf-8")
        self.assertIn("#FF2C6699", cfg)   # selection bar = azure deep


if __name__ == "__main__":
    unittest.main()
