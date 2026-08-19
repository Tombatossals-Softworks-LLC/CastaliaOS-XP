"""Tests for the shipped Openbox configuration (Bible §7.6, §7.7).

`shell/session/openbox-rc.xml` is the single source of truth for the window
manager's decorations and the **global keyboard map** — §7.7 requires the OS
to be "fully operable keyboard-only", and a binding that silently disappears
(or points at an app that no longer ships) is exactly the kind of rot nobody
notices until a user reaches for the key.

So this pins:

* the file is well-formed XML in the Openbox namespace;
* the bindings §7.7 names by hand are present;
* every app a binding launches is a real first-party binary from
  `tests/apps.manifest` (or `castalia-explorer`, the shell's own plane);
* apps are launched through `castalia-open`, so a hotkey-started app inherits
  the session's asset tree and active theme instead of the compiled-in
  defaults;
* both shipping paths — `packages/mkdeb.sh` and the ISO hook — install this
  same file, so a live image and an installed system cannot diverge;
* and neither of them installs it to ``/etc/xdg/openbox/rc.xml``. That path
  is owned by the **openbox package**, so shipping it from `castalia-desktop`
  makes dpkg refuse to unpack openbox ("trying to overwrite
  '/etc/xdg/openbox/rc.xml', which is also in package castalia-desktop") and
  the whole install aborts. It is a one-line mistake to make again, and it
  only shows up when apt installs the two together, so it is pinned here.
"""

import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
RC = REPO / "shell" / "session" / "openbox-rc.xml"
OPEN_HELPER = REPO / "shell" / "session" / "castalia-open"
MANIFEST = REPO / "tests" / "apps.manifest"
MKDEB = REPO / "packages" / "mkdeb.sh"
HOOK = REPO / "build" / "hooks" / "desktop-amd64.sh"

NS = {"ob": "http://openbox.org/3.4/rc"}

#: The bindings §7.7 spells out, plus the era-defining window chords.
REQUIRED_KEYS = {
    "W-r",              # Run
    "W-l",              # lock
    "W-e",              # Explorer
    "W-t",              # terminal
    "W-f",              # search
    "W-d",              # show desktop
    "Print",            # screenshot
    "A-F4",             # close window
    "C-A-Delete",       # task manager
}

#: Bindings that must NOT be here: castalia-panel takes the X grab for these
#: itself and shows Castalia's own switcher (§7.6). X hands a key grab to the
#: first client that asks, so an Openbox binding would quietly win the race
#: and put Openbox's OSD back on screen — with nothing in any log to say so.
PANEL_OWNED_KEYS = {"A-Tab", "A-S-Tab", "A-grave"}


def binaries():
    """The first-party binary names from the canonical app manifest."""
    names = set()
    for line in MANIFEST.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("#"):
            continue
        names.add(Path(line.split("|")[1]).name)
    # The shell's own planes are not "apps" in the manifest sense.
    names.update({"castalia-explorer", "castalia-panel", "castalia-desktop"})
    return names


class OpenboxRcTest(unittest.TestCase):
    def setUp(self):
        self.root = ET.parse(RC).getroot()

    def test_is_openbox_config(self):
        self.assertTrue(self.root.tag.endswith("openbox_config"), self.root.tag)

    def test_theme_is_a_castalia_theme(self):
        name = self.root.find("ob:theme/ob:name", NS)
        self.assertIsNotNone(name, "no <theme><name>")
        self.assertTrue(name.text.startswith("Castalia-"), name.text)
        theme_id = name.text[len("Castalia-"):]
        self.assertTrue((REPO / "themes" / theme_id / "theme.conf").is_file(),
                        f"rc.xml names a theme that does not ship: {theme_id}")

    def test_required_bindings_present(self):
        keys = {kb.get("key")
                for kb in self.root.findall("ob:keyboard/ob:keybind", NS)}
        missing = REQUIRED_KEYS - keys
        self.assertFalse(missing, f"§7.7 bindings missing: {sorted(missing)}")

    def test_switcher_keys_left_to_the_panel(self):
        keys = {kb.get("key")
                for kb in self.root.findall("ob:keyboard/ob:keybind", NS)}
        stolen = PANEL_OWNED_KEYS & keys
        self.assertFalse(
            stolen,
            f"openbox-rc.xml binds keys castalia-panel needs: {sorted(stolen)}")

    def test_no_duplicate_bindings(self):
        keys = [kb.get("key")
                for kb in self.root.findall("ob:keyboard/ob:keybind", NS)]
        dupes = {k for k in keys if keys.count(k) > 1}
        self.assertFalse(dupes, f"a key is bound twice: {sorted(dupes)}")

    def test_every_launched_app_ships(self):
        known = binaries()
        for cmd in self.root.iterfind(
                "ob:keyboard/ob:keybind/ob:action/ob:command", NS):
            parts = cmd.text.split()
            self.assertEqual(parts[0], "castalia-open",
                             f"launch {parts[0]} through castalia-open so it "
                             f"inherits the session theme/repo: {cmd.text}")
            self.assertIn(parts[1], known,
                          f"rc.xml launches an app that does not ship: "
                          f"{parts[1]}")

    def test_every_execute_action_has_a_command(self):
        for action in self.root.iterfind(
                "ob:keyboard/ob:keybind/ob:action", NS):
            if action.get("name") != "Execute":
                continue
            command = action.find("ob:command", NS)
            self.assertIsNotNone(command, "an Execute action with no command")
            self.assertTrue(command.text.strip(), "an empty command")


SESSION = REPO / "shell" / "session" / "castalia-session"


class ShippingTest(unittest.TestCase):
    def test_mkdeb_installs_the_rc(self):
        text = MKDEB.read_text(encoding="utf-8")
        self.assertIn("shell/session/openbox-rc.xml", text)
        self.assertIn("openbox/rc.xml", text)

    def test_hook_installs_the_rc(self):
        text = HOOK.read_text(encoding="utf-8")
        self.assertIn("shell/session/openbox-rc.xml", text)
        self.assertNotIn("<openbox_config", text,
                         "the hook must install the shipped rc.xml, not "
                         "write its own copy (they drift)")

    def test_never_shipped_into_the_openbox_package_path(self):
        """/etc/xdg/openbox/rc.xml belongs to the openbox package.

        Installing it from castalia-desktop makes dpkg abort the whole
        transaction with "trying to overwrite ... which is also in package
        castalia-desktop". Keep it in the Castalia asset tree.
        """
        for path in (MKDEB, HOOK):
            for line in path.read_text(encoding="utf-8").splitlines():
                if line.lstrip().startswith("#"):
                    continue          # the comment explaining exactly this
                self.assertNotIn(
                    "/etc/xdg/openbox", line,
                    f"{path.name} ships into openbox's own dpkg namespace: "
                    f"{line.strip()}")

    def test_session_points_openbox_at_the_shipped_rc(self):
        text = SESSION.read_text(encoding="utf-8")
        self.assertIn("--config-file", text,
                      "the session must point openbox at the shipped rc.xml")
        self.assertIn("openbox/rc.xml", text)

    def test_session_lets_the_user_override_the_bindings(self):
        """§7.7: the keyboard map is "all rebindable"."""
        text = SESSION.read_text(encoding="utf-8")
        self.assertIn(".config/openbox/rc.xml", text,
                      "a user's own ~/.config/openbox/rc.xml must win over "
                      "the shipped one")

    def test_both_ship_the_open_helper(self):
        for path in (MKDEB, HOOK):
            self.assertIn("castalia-open", path.read_text(encoding="utf-8"),
                          f"{path.name} does not install castalia-open")

    def test_open_helper_is_posix_sh_and_executable(self):
        text = OPEN_HELPER.read_text(encoding="utf-8")
        self.assertTrue(text.startswith("#!/bin/sh"), "must be POSIX sh")
        self.assertIn("exec ", text, "must exec the app, not fork and linger")
        self.assertIn("--repo", text)
        self.assertIn("--theme", text)


if __name__ == "__main__":
    unittest.main()
