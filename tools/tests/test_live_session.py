"""A live session must always reach *something* the user can act on.

The live image's entry point is `/usr/local/bin/castalia-live-session`,
written by the desktop chroot hook. Everything a person can do with the ISO
runs through it: try the desktop, install from the boot menu, or — when X
cannot start on that hardware at all — get a console that says what to type
instead of a black screen (Bible §14.5, §18 Phase 2).

None of that is reachable from a unit test on the build host, so what is
checked here is that the wiring exists and agrees with itself.
"""
import re
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
HOOK = REPO / "build" / "hooks" / "desktop-amd64.sh"
SESSION = REPO / "shell" / "session" / "castalia-session"


def read(path):
    return path.read_text(encoding="utf-8")


class LiveSessionTest(unittest.TestCase):
    def setUp(self):
        self.hook = read(HOOK)

    def test_the_launcher_is_installed_and_respawned(self):
        self.assertIn("/usr/local/bin/castalia-live-session", self.hook)
        self.assertRegex(
            self.hook,
            r"respawn:/usr/local/bin/castalia-live-session",
            "nothing in /etc/inittab starts the live session")

    def test_the_session_is_marked_live(self):
        """The desktop's 'Instalar Castalia OS' icon hangs off this."""
        self.assertIn("export CASTALIA_LIVE=1", self.hook)

    def test_graphics_failure_leaves_a_usable_console(self):
        """The worst outcome is a black screen with no way forward."""
        self.assertNotIn(
            "exec startx", self.hook,
            "startx is exec'd, so nothing runs if X fails to start")
        self.assertRegex(
            self.hook, r"exec /sbin/agetty --autologin root tty1",
            "no shell to fall back to when X cannot start")
        # …and the console tells the user what to type.
        for hint in ("castalia-live-session", "castalia-instalar-texto"):
            self.assertIn(hint, self.hook.split("MSG")[1] if "MSG" in
                          self.hook else "",
                          f"the failure message never mentions {hint}")

    def test_the_console_issue_names_the_same_commands(self):
        issue = self.hook.split("<<'ISSUE'")[1].split("ISSUE")[0]
        self.assertIn("castalia-live-session", issue)
        self.assertIn("castalia-instalar-texto", issue)

    def test_a_plain_live_boot_still_demos(self):
        self.assertIn("export CASTALIA_DEMO=1", self.hook)

    def test_gui_install_hands_over_to_the_session(self):
        self.assertIn("export CASTALIA_AUTOSTART_INSTALLER=1", self.hook)
        session = read(SESSION)
        self.assertIn("CASTALIA_AUTOSTART_INSTALLER", session,
                      "the boot menu can ask for the installer but "
                      "castalia-session never opens it")
        self.assertIn("castalia-instalador", session)

    def test_the_text_installer_exists_to_be_execd(self):
        """The text path is the §14.5 #5 guarantee: it must be on the image."""
        self.assertIn("/usr/local/bin/castalia-instalar-texto", self.hook)
        self.assertIn("castalia_installer.tui", self.hook)

    def test_the_installer_binary_is_installed(self):
        self.assertRegex(
            self.hook,
            r"install -Dm755 .*castalia-instalador",
            "the live image would have no graphical installer to open")


class SessionScriptTest(unittest.TestCase):
    def test_installer_autostart_is_not_the_demo_path(self):
        """Opening demo windows over the installer would be a mess."""
        session = read(SESSION)
        auto = session.index("CASTALIA_AUTOSTART_INSTALLER")
        demo = session.index('"${CASTALIA_DEMO:-0}" = "1"')
        self.assertLess(auto, demo,
                        "the installer autostart must be its own branch, "
                        "ahead of the demo welcome")

    def test_posix_sh_only(self):
        """Bible §12: the session is POSIX sh, auditable in one read."""
        session = read(SESSION)
        self.assertTrue(session.startswith("#!/bin/sh"))
        # `[[:space:]]` is a POSIX character class, not a bash test — hence
        # the negative lookahead rather than a bare `[[`.
        for bashism in (r"\[\[(?!:)", r"\bfunction ", r"\$\(\(.*\+\+"):
            self.assertIsNone(re.search(bashism, session),
                              f"bashism in castalia-session: {bashism}")


if __name__ == "__main__":
    unittest.main()
