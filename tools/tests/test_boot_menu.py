"""Rendering the boot menu, including the order it has to happen in.

The first version of this substitution filled TITLE and APPEND and *then*
spliced the install entries in — so the entries' own APPEND placeholder went
out verbatim, and an ISO shipped with `APPEND initrd=/live/initrd.img
@APPEND@ castalia.installer=gui`: an install entry that boots without
`boot=live`. It took building an ISO and taking it apart again to see it.
That is what these tests are for.
"""
import unittest
from pathlib import Path

from boot_menu import render, unfilled

REPO = Path(__file__).resolve().parents[2]
TEMPLATE = REPO / "iso" / "isolinux" / "isolinux.cfg.in"
ENTRIES = REPO / "iso" / "isolinux" / "entries-install.cfg"


class RenderTest(unittest.TestCase):
    def test_install_entries_get_the_shared_kernel_args(self):
        menu = render("@INSTALL@", title="T", append="boot=live",
                      install="APPEND @APPEND@ castalia.installer=gui")
        self.assertIn("APPEND boot=live castalia.installer=gui", menu)
        self.assertEqual(unfilled(menu), [])

    def test_nothing_is_left_unfilled(self):
        menu = render(TEMPLATE.read_text(encoding="utf-8"),
                      title="Castalia Live Desktop (amd64)",
                      append="boot=live console=ttyS0,115200",
                      install=ENTRIES.read_text(encoding="utf-8"))
        self.assertEqual(unfilled(menu), [],
                         "isolinux would show the placeholder to the user")

    def test_an_edition_with_no_installer_gets_no_install_entries(self):
        menu = render(TEMPLATE.read_text(encoding="utf-8"),
                      title="Castalia Live (amd64 boot proof)",
                      append="boot=live", install="")
        # Comments discuss the modes; what matters is that no *entry* offers
        # an install this image cannot perform.
        directives = [line for line in menu.splitlines()
                      if not line.lstrip().startswith("#")]
        self.assertNotIn("castalia.installer=", "\n".join(directives))
        self.assertIn("LABEL live", menu)

    def test_live_is_first_in_the_rendered_menu(self):
        """The whole point: the first thing offered is the live desktop."""
        menu = render(TEMPLATE.read_text(encoding="utf-8"),
                      title="T", append="boot=live",
                      install=ENTRIES.read_text(encoding="utf-8"))
        labels = [line.split()[1] for line in menu.splitlines()
                  if line.startswith("LABEL ")]
        self.assertEqual(labels[0], "live")
        self.assertIn("install", labels)
        self.assertIn("textinstall", labels)
        self.assertIn("livesafe", labels)

    def test_the_title_names_the_edition(self):
        """Two ISOs ship; the screen must say which one you are holding."""
        menu = render(TEMPLATE.read_text(encoding="utf-8"),
                      title="Castalia Live (amd64 boot proof)",
                      append="boot=live", install="")
        self.assertIn("MENU TITLE Castalia Live (amd64 boot proof)", menu)

    def test_the_templates_own_comments_are_not_substituted(self):
        """The comment block used to document the placeholders in their own
        syntax, so rendering rewrote the documentation into the menu."""
        menu = render(TEMPLATE.read_text(encoding="utf-8"),
                      title="TITLE-MARKER", append="APPEND-MARKER",
                      install="INSTALL-MARKER")
        for line in menu.splitlines():
            if line.startswith("#"):
                for marker in ("TITLE-MARKER", "APPEND-MARKER",
                               "INSTALL-MARKER"):
                    self.assertNotIn(marker, line,
                                     "a comment line was substituted")


if __name__ == "__main__":
    unittest.main()
