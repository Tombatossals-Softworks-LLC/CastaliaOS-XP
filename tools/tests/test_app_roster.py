"""The app roster is shared, so a typo in it is wrong twice.

`shell/panel/src/AppRoster.cpp` is the one table behind both the launch menu
(Bible §7.3) and the Alt+Tab switcher (§7.6). A misspelt icon name renders a
blank square in both places and raises no error anywhere — Qt just returns a
null icon — and a binary that does not ship gives a menu entry that does
nothing at all. Both are caught here instead.
"""
import re
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
ROSTER = REPO / "shell" / "panel" / "src" / "AppRoster.cpp"
ICONS = REPO / "themes" / "icons" / "48"
MANIFEST = REPO / "tests" / "apps.manifest"

#: {QT_TRANSLATE_NOOP("AppRoster", "Label"), "castalia-bin", "icon"} — the
#: label is marked for translation (§7.13) but the table still holds the
#: Spanish source string, which is what this file checks.
ENTRY = re.compile(
    r'\{\s*QT_TRANSLATE_NOOP\("AppRoster",\s*"(?P<label>[^"]+)"\),\s*'
    r'\n?\s*"(?P<bin>castalia-[a-z0-9-]+)",\s*'
    r'\n?\s*"(?P<icon>[a-z0-9-]+)"',
    re.MULTILINE,
)


def entries():
    return [m.groupdict() for m in ENTRY.finditer(ROSTER.read_text("utf-8"))]


def shipped_binaries():
    names = set()
    for line in MANIFEST.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("#"):
            continue
        names.add(Path(line.split("|")[1]).name)
    # The shell's own planes are not "apps" in the manifest sense.
    names.update({"castalia-explorer", "castalia-panel", "castalia-desktop"})
    return names


class AppRosterTest(unittest.TestCase):
    def test_every_entry_is_parsed(self):
        """A regex that silently skips entries would make this file useless."""
        text = ROSTER.read_text("utf-8")
        declared = len(re.findall(r'"castalia-[a-z0-9-]+"', text))
        self.assertEqual(len(entries()), declared,
                         "the roster has entries this test cannot see")
        self.assertGreaterEqual(declared, 30,
                                "the roster lost most of its entries")

    def test_every_icon_exists(self):
        missing = sorted({e["icon"] for e in entries()
                          if not (ICONS / f"{e['icon']}.svg").is_file()})
        self.assertFalse(
            missing,
            f"roster icons with no SVG in themes/icons/48: {missing}")

    def test_every_binary_ships(self):
        shipped = shipped_binaries()
        missing = sorted({e["bin"] for e in entries()
                          if e["bin"] not in shipped})
        self.assertFalse(
            missing,
            f"roster entries whose binary is not in tests/apps.manifest: "
            f"{missing}")

    def test_labels_are_spanish_first(self):
        """§8/§7.3: the UI is Spanish-first — no English labels slip in."""
        english = {"Settings", "Files", "Search", "Help", "Games"}
        offenders = sorted({e["label"] for e in entries()
                            if e["label"] in english})
        self.assertFalse(offenders, f"English labels in the roster: "
                                    f"{offenders}")


if __name__ == "__main__":
    unittest.main()
