"""Smoke tests for the design-system preview generator."""

import sys
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))

import preview_gen  # noqa: E402


class PreviewGenTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fragment, cls.full = preview_gen.build_page(preview_gen.REPO)

    def test_all_five_themes_are_present(self):
        for tid in ("classic", "azul", "oliva", "plata", "high-contrast"):
            self.assertIn(f".th-{tid}{{", self.fragment, tid)
            self.assertIn(f'data-theme-id="{tid}"', self.fragment, tid)

    def test_all_icons_and_mark_are_inlined(self):
        # each icon contributes at least one <svg ... viewBox="0 0 48 48">
        self.assertGreaterEqual(self.fragment.count('viewBox="0 0 48 48"'), 10)
        self.assertIn("castalia-mark", self.fragment)

    def test_tokens_come_from_theme_conf(self):
        # the Bible §8.2 Classic gradient must appear verbatim from the bundle
        self.assertIn("#3E82B6", self.fragment)
        self.assertIn("#2C6699", self.fragment)

    def test_contrast_checks_all_pass(self):
        # shipped themes pass the linter, so the page must show no FAIL chip
        self.assertNotIn('chip bad">FAIL', self.fragment)
        self.assertIn('chip ok">pass', self.fragment)

    def test_wallpaper_is_embedded(self):
        self.assertIn("data:image/svg+xml,", self.fragment)

    def test_fragment_has_no_document_wrapper(self):
        self.assertNotIn("<html", self.fragment)
        self.assertNotIn("<body", self.fragment)
        self.assertIn("<title>", self.fragment)

    def test_full_document_is_wrapped(self):
        self.assertTrue(self.full.startswith("<!DOCTYPE html>"))
        self.assertIn("</html>", self.full)

    def test_no_unbalanced_theme_braces(self):
        # every generated theme block closes what it opens
        for tid in ("classic", "high-contrast"):
            block_start = self.fragment.index(f".th-{tid}{{")
            block = self.fragment[block_start: self.fragment.index("}", block_start) + 1]
            self.assertEqual(block.count("{"), block.count("}"), block)


if __name__ == "__main__":
    unittest.main()
