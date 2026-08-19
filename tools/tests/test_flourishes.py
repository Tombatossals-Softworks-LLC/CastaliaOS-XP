"""Tests for Castalia's flourishes — the aurora and the hidden menu entry.

An easter egg is the least-exercised code in a tree: nothing routine touches
it, so it rots quietly and is discovered broken by the one person who went
looking. `docs/EASTER-EGGS.md` documents them precisely *so this file can
enforce the documentation*, the same way test_openbox_rc.py enforces the
keyboard map.

What is pinned here:

* the aurora has exactly **one** implementation (``castalia::paintAurora``),
  shared by the desktop's flourish and the screensaver scene — the egg is not
  a private copy that drifts from the real one;
* the aurora screensaver mode is offered by the app **and** by the Control
  Center's picker, so the two cannot disagree about what exists;
* the key sequence in the code is the sequence the document promises;
* the Start Menu's secret words are all documented;
* every flourish name the desktop accepts is documented, and vice versa.
"""

import re
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
AURORA_H = REPO / "shell" / "libcastalia-ui" / "Aurora.h"
AURORA_CPP = REPO / "shell" / "libcastalia-ui" / "Aurora.cpp"
DESKTOP_CPP = REPO / "shell" / "desktop" / "src" / "DesktopWindow.cpp"
DESKTOP_MAIN = REPO / "shell" / "desktop" / "src" / "main.cpp"
MENU_CPP = REPO / "shell" / "panel" / "src" / "CastaliaMenu.cpp"
#: The app labels moved out of the menu into the roster the menu and
#: the Alt+Tab switcher share (§7.3, §7.6).
ROSTER_CPP = REPO / "shell" / "panel" / "src" / "AppRoster.cpp"
SAVER = REPO / "apps" / "screensaver" / "src" / "main.cpp"
CONTROL = REPO / "apps" / "control-center" / "src" / "ControlCenter.cpp"
DOC = REPO / "docs" / "EASTER-EGGS.md"

# Qt::Key values for ↑ ↑ ↓ ↓ ← → ← → B A, in order.
KONAMI = [0x01000013, 0x01000013, 0x01000015, 0x01000015,
          0x01000012, 0x01000014, 0x01000012, 0x01000014, 0x42, 0x41]


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


class AuroraIsSharedTest(unittest.TestCase):
    def test_one_implementation_two_consumers(self):
        self.assertIn("void paintAurora(", read(AURORA_CPP))
        # Nobody re-implements it: both consumers call the shared painter.
        self.assertIn("castalia::paintAurora(", read(DESKTOP_CPP))
        self.assertIn("castalia::paintAurora(", read(SAVER))

    def test_painter_is_pure(self):
        """No timers, no widgets, no globals in the painting code."""
        src = read(AURORA_CPP)
        for forbidden in ("QTimer", "QWidget", "static QPixmap", "qrand("):
            self.assertNotIn(forbidden, src)

    def test_screensaver_mode_is_offered_everywhere(self):
        saver = read(SAVER)
        self.assertIn("ondas|mystify|estrellas|aurora", saver)
        self.assertIn('QStringLiteral("aurora")', saver)
        # …and the Control Center's picker lists it, so the settings page and
        # the app cannot disagree about which scenes exist.
        self.assertIn('QStringLiteral("aurora")', read(CONTROL))


class KonamiTest(unittest.TestCase):
    def test_sequence_matches_the_document(self):
        src = read(AURORA_CPP)
        block = src.split("static const QVector<int> keys = {", 1)[1]
        block = block.split("};", 1)[0]
        found = [int(v, 16) for v in re.findall(r"0x[0-9A-Fa-f]+", block)]
        self.assertEqual(found, KONAMI)

    def test_document_spells_out_the_same_sequence(self):
        self.assertIn("↑ ↑ ↓ ↓ ← → ← → B A", read(DOC))

    def test_detector_is_headless_testable(self):
        """The state machine must stay widget-free (the C++ selftest runs it
        under QCoreApplication, where a QWidget would abort)."""
        self.assertNotIn("QWidget", read(AURORA_H))


class SecretWordsTest(unittest.TestCase):
    def secret_words(self) -> list[str]:
        body = read(MENU_CPP).split("bool CastaliaMenu::isSecretWord", 1)[1]
        body = body.split("}", 1)[0]
        return re.findall(r'QLatin1String\("([^"]+)"\)', body)

    def test_words_are_documented(self):
        words = self.secret_words()
        self.assertTrue(words, "no secret words found in isSecretWord()")
        doc = read(DOC)
        for word in words:
            self.assertIn(word, doc, f"undocumented secret word: {word}")

    def test_no_secret_word_collides_with_a_real_entry(self):
        """A secret word that also matches a shipped app would reveal the
        entry during ordinary use — and hide the app behind it."""
        roster = read(ROSTER_CPP)
        labels = re.findall(
            r'QT_TRANSLATE_NOOP\("AppRoster",\s*"([^"]+)"\),\s*'
            r'\n?\s*"castalia-[^"]+",', roster)
        self.assertTrue(labels, "no app labels parsed from the roster")
        for word in self.secret_words():
            for label in labels:
                self.assertNotIn(word, label.lower(),
                                 f"{word!r} collides with {label!r}")


class FlourishFlagTest(unittest.TestCase):
    def test_desktop_accepts_only_documented_flourishes(self):
        main = read(DESKTOP_MAIN)
        self.assertIn("--easter-egg", read(DOC))
        # The flag rejects anything it does not know, rather than silently
        # starting nothing.
        self.assertIn("unknown flourish", main)
        self.assertIn('QLatin1String("aurora")', main)

    def test_panel_menu_query_is_documented(self):
        self.assertIn("--menu-query", read(DOC))
        self.assertIn("menu-query",
                      read(REPO / "shell" / "panel" / "src" / "main.cpp"))


if __name__ == "__main__":
    unittest.main()
