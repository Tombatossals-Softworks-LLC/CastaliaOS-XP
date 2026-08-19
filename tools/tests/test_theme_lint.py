"""Unit tests for castalia_qa.theme_lint."""

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from castalia_qa import theme_lint  # noqa: E402

GOOD_THEME = """\
[meta]
name = "Test Theme"
id = "testtheme"
version = "0.0.1"
author = "Unit Test"

[colors]
accent = "#3E82B6"
titlebar_top = "#3E82B6"
titlebar_bottom = "#2C6699"
titlebar_text = "#FFFFFF"
titlebar_inactive_top = "#7D8A94"
titlebar_inactive_bottom = "#66727C"
titlebar_inactive_text = "#FFFFFF"
surface = "#ECE9E4"
surface_alt = "#E2DED6"
text = "#1E1E1E"
text_secondary = "#5A564E"
selection_bg = "#2C6699"
selection_text = "#FFFFFF"
border = "#B8B2A6"

[metrics]
base_unit = 4
titlebar_height = 26
corner_radius = 2
panel_height = 30
panel_height_800 = 28

[fonts]
ui = "DejaVu Sans"
mono = "DejaVu Sans Mono"
"""


def write_theme(root: Path, theme_id: str, content: str) -> Path:
    theme_dir = root / theme_id
    theme_dir.mkdir(parents=True)
    conf = theme_dir / "theme.conf"
    conf.write_text(content, encoding="utf-8")
    return conf


class ThemeLintTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)
        self.addCleanup(self._tmp.cleanup)

    def test_good_theme_passes(self):
        conf = write_theme(self.root, "testtheme", GOOD_THEME)
        self.assertEqual(theme_lint.lint_theme(conf), [])

    def test_id_must_match_directory(self):
        conf = write_theme(self.root, "wrongdir", GOOD_THEME)
        errors = theme_lint.lint_theme(conf)
        self.assertTrue(any("does not match directory" in e for e in errors))

    def test_banding_gradient_fails(self):
        bad = GOOD_THEME.replace(
            'titlebar_bottom = "#2C6699"', 'titlebar_bottom = "#04101C"'
        )
        conf = write_theme(self.root, "testtheme", bad)
        errors = theme_lint.lint_theme(conf)
        self.assertTrue(any("luminance delta" in e for e in errors))

    def test_low_contrast_text_fails(self):
        bad = GOOD_THEME.replace('text = "#1E1E1E"', 'text = "#B0ADA6"')
        conf = write_theme(self.root, "testtheme", bad)
        errors = theme_lint.lint_theme(conf)
        self.assertTrue(any("text vs surface" in e for e in errors))

    def test_missing_color_key_fails(self):
        bad = GOOD_THEME.replace('selection_bg = "#2C6699"\n', "")
        conf = write_theme(self.root, "testtheme", bad)
        errors = theme_lint.lint_theme(conf)
        self.assertTrue(any("selection_bg" in e for e in errors))

    def test_wrong_base_unit_fails(self):
        bad = GOOD_THEME.replace("base_unit = 4", "base_unit = 8")
        conf = write_theme(self.root, "testtheme", bad)
        errors = theme_lint.lint_theme(conf)
        self.assertTrue(any("base_unit" in e for e in errors))

    def test_high_contrast_raises_the_bar(self):
        # 4.6:1 text passes a normal theme but must FAIL a high-contrast one.
        hc = GOOD_THEME.replace(
            'author = "Unit Test"', 'author = "Unit Test"\nhigh_contrast = true'
        ).replace('text = "#1E1E1E"', 'text = "#767676"').replace(
            'surface = "#ECE9E4"', 'surface = "#FFFFFF"'
        ).replace('surface_alt = "#E2DED6"', 'surface_alt = "#F2F2F2"')
        conf = write_theme(self.root, "testtheme", hc)
        errors = theme_lint.lint_theme(conf)
        self.assertTrue(any("text vs surface" in e for e in errors))

    def test_empty_tree_is_an_error(self):
        errors = theme_lint.lint_tree(self.root)
        self.assertTrue(any("no */theme.conf" in e for e in errors))

    def test_shipped_themes_all_pass(self):
        shipped = Path(__file__).resolve().parents[2] / "themes"
        errors = theme_lint.lint_tree(shipped)
        self.assertEqual(errors, [], "\n".join(errors))


if __name__ == "__main__":
    unittest.main()
