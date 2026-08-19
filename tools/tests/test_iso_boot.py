"""The live ISO must boot to a live desktop and offer to install itself.

This gate exists because of a real failure: `iso/isolinux/isolinux.cfg` sat in
the repo as a four-entry "design" that nothing read, while `build/mkiso.sh`
wrote its own single-entry menu inline. Shipped ISOs therefore had one boot
entry, no installer entry anywhere, and a `castalia.installer=` kernel
argument no code on the image had ever parsed (Bible §14.1, §14.5).

So the checks here are deliberately about the *seams*: that the menu the
build renders is the menu in the repo, that the entries it offers are ones
the live session can actually deliver, and that every edition claiming an
installer ships one.
"""
import re
import unittest
from pathlib import Path

import boot_menu

REPO = Path(__file__).resolve().parents[2]
TEMPLATE = REPO / "iso" / "isolinux" / "isolinux.cfg.in"
INSTALL_ENTRIES = REPO / "iso" / "isolinux" / "entries-install.cfg"
MKISO = REPO / "build" / "mkiso.sh"
HOOK = REPO / "build" / "hooks" / "desktop-amd64.sh"
PROFILES = sorted((REPO / "build" / "profiles").glob("*.conf"))


def read(path):
    return path.read_text(encoding="utf-8")


def profile(path):
    """The shell-assignment keys of a profile, as a dict of raw strings."""
    values = {}
    for line in read(path).splitlines():
        m = re.match(r'^([A-Z_]+)="(.*)"$', line.strip())
        if m:
            values[m.group(1)] = m.group(2)
    return values


class BootMenuTest(unittest.TestCase):
    def test_the_build_renders_the_repo_template(self):
        mkiso = read(MKISO)
        self.assertIn("iso/isolinux/isolinux.cfg.in", mkiso,
                      "mkiso.sh does not render the repo's boot menu")
        # …and does not go back to writing its own.
        self.assertNotRegex(
            mkiso, r"cat > .*isolinux\.cfg' <<",
            "mkiso.sh writes an inline boot menu again — the repo template "
            "would silently stop being what ships")

    def test_live_is_the_default_entry(self):
        cfg = read(TEMPLATE)
        self.assertIn("DEFAULT live", cfg)
        labels = re.findall(r"^LABEL (\S+)", cfg, re.MULTILINE)
        self.assertTrue(labels, "no boot entries in the template")
        self.assertEqual(labels[0], "live",
                         "the live session must be the first entry a user "
                         f"sees, not {labels[0]!r}")

    def test_every_entry_boots_something_we_ship(self):
        """A menu entry that boots nothing is worse than no entry."""
        for cfg in (read(TEMPLATE), read(INSTALL_ENTRIES)):
            for kernel in re.findall(r"^\s*KERNEL (\S+)", cfg, re.MULTILINE):
                self.assertEqual(
                    kernel, "/live/vmlinuz",
                    f"boot entry points at {kernel}, which mkiso.sh never "
                    "stages (the old menu offered a memtest we do not ship)")

    def test_the_template_placeholders_are_all_substituted(self):
        used = set(re.findall(r"@([A-Z_]+)@", read(TEMPLATE)))
        self.assertTrue(used, "the template takes no per-edition values")
        unknown = used - set(boot_menu.PLACEHOLDERS)
        self.assertFalse(
            unknown,
            f"the template uses {sorted(unknown)}, which the renderer never "
            "fills — they would ship verbatim to the user")

    def test_the_build_calls_the_renderer(self):
        self.assertIn("tools/boot_menu.py", read(MKISO),
                      "mkiso.sh renders the menu some other way again")

    def test_install_entries_offer_both_paths(self):
        cfg = read(INSTALL_ENTRIES)
        modes = set(re.findall(r"castalia\.installer=(\w+)", cfg))
        self.assertEqual(modes, {"gui", "text"},
                         "§14.5 wants the graphical installer and the text "
                         f"fallback; the menu offers {sorted(modes)}")

    def test_the_live_session_handles_every_mode_the_menu_offers(self):
        """The bug this whole file exists for: a kernel argument nobody read."""
        offered = set(re.findall(r"castalia\.installer=(\w+)",
                                 read(INSTALL_ENTRIES)))
        hook = read(HOOK)
        self.assertIn("castalia.installer=", hook.replace("\\", ""),
                      "the live session never looks at /proc/cmdline")
        for mode in offered:
            self.assertTrue(
                re.search(rf"^\s*{mode}\)", hook, re.MULTILINE),
                f"the boot menu offers castalia.installer={mode} and the "
                "live session has no case for it")


class ChrootHookTest(unittest.TestCase):
    """The hook builds the shell inside the image. If it fails, the image is
    a Debian console with a Castalia name on it — which is exactly what
    castalia-live-desktop-amd64 0.1.1 was."""

    def test_the_hook_failure_stops_the_build(self):
        mkiso = read(MKISO)
        hook_stage = mkiso[mkiso.index("stage_hook()"):
                           mkiso.index("stage_assets()")]
        self.assertIn(
            "set -e", hook_stage,
            "the chroot hook runs in a `sh -c` whose exit status is the last "
            "command's, so a failed build inside the chroot would pass")

    def test_the_build_checks_what_the_hook_produced(self):
        mkiso = read(MKISO)
        for required in ("castalia-panel", "castalia-session",
                         "castalia-live-session", "castalia-instalador"):
            self.assertIn(
                required, mkiso,
                f"nothing verifies {required} landed in the image before it "
                "is squashed and published")

    def test_the_source_the_hook_needs_travels_with_it(self):
        """shell/CMakeLists.txt reads ../VERSION; a chroot without it cannot
        even configure, and that is the whole bug."""
        needed = re.findall(r'\$\{CMAKE_CURRENT_SOURCE_DIR\}/\.\./(\w+)',
                            read(REPO / "shell" / "CMakeLists.txt"))
        self.assertIn("VERSION", needed,
                      "this test is guarding a path that moved")
        for path in PROFILES:
            values = profile(path)
            if "HOOK" not in values:
                continue
            src = values.get("SRC_DIRS", "").split()
            for item in needed:
                self.assertIn(
                    item, src,
                    f"{path.name} stages {src} into the chroot but the shell "
                    f"build reads ../{item}")


class ProfileTest(unittest.TestCase):
    def test_installer_editions_ship_the_installer(self):
        for path in PROFILES:
            values = profile(path)
            if values.get("INSTALLER") != "yes":
                continue
            self.assertIn(
                "HOOK", values,
                f"{path.name} promises install entries but runs no chroot "
                "hook, so the image has no castalia-instalador on it")

    def test_desktop_editions_offer_to_install_themselves(self):
        """A live desktop with no way to install it is a demo, not a distro."""
        for path in PROFILES:
            values = profile(path)
            if "desktop-amd64.sh" not in values.get("HOOK", ""):
                continue
            self.assertEqual(
                values.get("INSTALLER"), "yes",
                f"{path.name} builds a live desktop but its boot menu would "
                "have no install entry")


if __name__ == "__main__":
    unittest.main()
