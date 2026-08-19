"""Unit tests for the theme.conf → QSS/Openbox exporter."""

import sys
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))

import theme_export  # noqa: E402

THEMES = Path(theme_export.REPO) / "themes"
ALL_IDS = ["human", "classic", "azul", "oliva", "plata", "medianoche",
           "high-contrast"]


class MixTest(unittest.TestCase):
    def test_endpoints(self):
        self.assertEqual(theme_export.mix("#000000", "#FFFFFF", 0), "#000000")
        self.assertEqual(theme_export.mix("#000000", "#FFFFFF", 1), "#FFFFFF")

    def test_midpoint(self):
        self.assertEqual(theme_export.mix("#000000", "#FFFFFF", 0.5), "#808080")


class ExportTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.themes = {
            tid: theme_export.load_theme(THEMES / tid / "theme.conf")
            for tid in ALL_IDS
        }

    def test_qss_uses_the_token_gradient(self):
        qss = theme_export.build_qss(self.themes["classic"])
        self.assertIn("#3E82B6", qss)   # Bible §8.2 accent
        self.assertIn("#2C6699", qss)
        self.assertIn("QPushButton", qss)
        self.assertIn("QScrollBar::handle", qss)

    def test_qss_braces_balanced(self):
        for tid, theme in self.themes.items():
            qss = theme_export.build_qss(theme)
            self.assertEqual(qss.count("{"), qss.count("}"), tid)

    def test_high_contrast_gets_dark_fields(self):
        qss = theme_export.build_qss(self.themes["high-contrast"])
        self.assertIn("#101010", qss)
        self.assertIn("#FFD800", qss)
        classic = theme_export.build_qss(self.themes["classic"])
        self.assertIn("#FFFFFF", classic)

    def test_openbox_titlebar_gradient(self):
        ob = theme_export.build_openbox(self.themes["classic"])
        self.assertIn("window.active.title.bg: gradient vertical", ob)
        self.assertIn("window.active.title.bg.color: #3E82B6", ob)
        self.assertIn("window.active.title.bg.colorTo: #2C6699", ob)

    def test_openbox_close_button_is_accent_tinted(self):
        for tid, theme in self.themes.items():
            ob = theme_export.build_openbox(theme)
            accent = theme["colors"]["accent"]
            self.assertIn(
                f"window.active.button.close.unpressed.bg.color: {accent}",
                ob, tid)

    def test_export_writes_both_files_for_all_themes(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            for tid, theme in self.themes.items():
                paths = theme_export.export_theme(theme, out)
                self.assertTrue((out / tid / "castalia.qss").is_file(), tid)
                self.assertTrue(
                    (out / tid / "openbox-3" / "themerc").is_file(), tid)
                self.assertEqual(len(paths), 2, tid)

    def test_generated_marker_present(self):
        qss = theme_export.build_qss(self.themes["azul"])
        ob = theme_export.build_openbox(self.themes["azul"])
        for text in (qss, ob):
            self.assertIn("GENERATED", text)
            self.assertIn("Do not edit", text)


if __name__ == "__main__":
    unittest.main()
