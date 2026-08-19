"""Unit tests for castalia_qa.color (WCAG math)."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from castalia_qa import color  # noqa: E402


class ParseHexTest(unittest.TestCase):
    def test_parses_rrggbb(self):
        self.assertEqual(color.parse_hex("#3E82B6"), (0x3E, 0x82, 0xB6))

    def test_rejects_short_and_bare_forms(self):
        for bad in ("#FFF", "3E82B6", "#GGGGGG", "", "#12345", "#1234567"):
            with self.assertRaises(ValueError, msg=bad):
                color.parse_hex(bad)


class LuminanceContrastTest(unittest.TestCase):
    def test_black_and_white_extremes(self):
        self.assertAlmostEqual(color.relative_luminance("#000000"), 0.0)
        self.assertAlmostEqual(color.relative_luminance("#FFFFFF"), 1.0)

    def test_max_contrast_is_21(self):
        self.assertAlmostEqual(color.contrast_ratio("#000000", "#FFFFFF"), 21.0)

    def test_contrast_is_symmetric_and_min_1(self):
        self.assertEqual(
            color.contrast_ratio("#3E82B6", "#FFFFFF"),
            color.contrast_ratio("#FFFFFF", "#3E82B6"),
        )
        self.assertAlmostEqual(color.contrast_ratio("#808080", "#808080"), 1.0)

    def test_known_wcag_value(self):
        # #767676 on white is the canonical "just passes AA" grey (~4.54:1).
        ratio = color.contrast_ratio("#767676", "#FFFFFF")
        self.assertGreater(ratio, 4.5)
        self.assertLess(ratio, 4.6)


class GradientRuleTest(unittest.TestCase):
    def test_classic_titlebar_gradient_is_16bit_safe(self):
        # The gradient the Bible specifies for the default theme (§8.2).
        delta = color.gradient_luminance_delta("#3E82B6", "#2C6699")
        self.assertLessEqual(delta, color.GRADIENT_MAX_LUMINANCE_DELTA)

    def test_extreme_gradient_fails_the_rule(self):
        delta = color.gradient_luminance_delta("#FFFFFF", "#000000")
        self.assertGreater(delta, color.GRADIENT_MAX_LUMINANCE_DELTA)

    def test_midpoint_is_mean_of_stops(self):
        mid = color.gradient_midpoint_luminance("#000000", "#FFFFFF")
        self.assertAlmostEqual(mid, 0.5)


if __name__ == "__main__":
    unittest.main()
