"""Tests for the session manager skeleton and runit service definitions."""

import configparser
import subprocess
import sys
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]

SH_SCRIPTS = [
    REPO / "shell" / "session" / "castalia-session",
    REPO / "services" / "lightdm" / "run",
    REPO / "services" / "lightdm" / "log" / "run",
]


class ShellScriptTest(unittest.TestCase):
    def test_posix_syntax(self):
        for script in SH_SCRIPTS:
            proc = subprocess.run(["sh", "-n", str(script)],
                                  capture_output=True, text=True)
            self.assertEqual(proc.returncode, 0,
                             f"{script}: {proc.stderr}")

    def test_shebang_and_executable(self):
        for script in SH_SCRIPTS:
            first = script.read_text(encoding="utf-8").splitlines()[0]
            self.assertEqual(first, "#!/bin/sh", script)
            self.assertTrue(script.stat().st_mode & 0o111,
                            f"{script} not executable")

    def test_no_bashisms_markers(self):
        # cheap guard: the POSIX-only rule (Bible §12) — no `[[ ]]` tests,
        # no `function` keyword. `[[:space:]]` (POSIX character class inside
        # a bracket expression) is fine, so match the bash construct `[[ `.
        for script in SH_SCRIPTS:
            text = script.read_text(encoding="utf-8")
            self.assertNotIn("[[ ", text, script)
            self.assertNotIn("function ", text, script)

    def test_run_scripts_exec_their_daemon(self):
        # runit contract: the run script must exec (stay PID-stable)
        for script in SH_SCRIPTS[1:]:
            text = script.read_text(encoding="utf-8")
            self.assertRegex(text, r"\nexec .+\n", script)


class SessionManagerTest(unittest.TestCase):
    text = (REPO / "shell" / "session" / "castalia-session")\
        .read_text(encoding="utf-8")

    def test_supervision_and_teardown(self):
        self.assertIn("supervise()", self.text)
        self.assertIn("trap shutdown_session TERM INT", self.text)

    def test_theme_fallback_chain(self):
        self.assertIn('THEME="classic"', self.text)
        self.assertIn("/etc/castalia/theme.conf", self.text)

    def test_safe_mode_is_read_from_the_kernel_cmdline(self):
        # iso/grub/11_castalia_safe boots with castalia.safemode=1. If nothing
        # reads it, the Safe Mode menu entry is just a slower normal boot.
        self.assertIn("castalia.safemode=1", self.text)
        self.assertIn("/proc/cmdline", self.text)
        self.assertIn("CASTALIA_SAFE_MODE", self.text)

    def test_safe_mode_strips_the_session(self):
        for var in ("CASTALIA_REDUCE_MOTION=1", "CASTALIA_NO_SOUND=1"):
            self.assertIn(f"export {var}", self.text, var)
        # The user's theme does not survive a boot they took because the
        # desktop would not come up.
        self.assertIn('THEME="high-contrast"', self.text)
        # §6.2 "minimal services": the notification server is gated on it.
        gate = self.text.index('[ "$SAFE_MODE" != "1" ] &&')
        self.assertLess(gate, self.text.index("castalia-notificaciones"))

    def test_safe_mode_is_decided_before_the_theme(self):
        # The theme block reads $SAFE_MODE; under `set -u` a later definition
        # would not be a wrong theme, it would be a session that never starts.
        self.assertLess(self.text.index("SAFE_MODE="),
                        self.text.index('THEME="$(theme_from'))

    def test_wm_starts_before_panel(self):
        self.assertLess(self.text.index("supervise openbox"),
                        self.text.index("castalia-panel"))


class ServiceConfTest(unittest.TestCase):
    def test_lightdm_metadata(self):
        cp = configparser.ConfigParser()
        cp.read(REPO / "services" / "lightdm" / "service.conf")
        self.assertEqual(cp["service"]["name"], "LightDM")
        for key in ("description", "category", "essential"):
            self.assertIn(key, cp["service"])
        self.assertIn(cp["service"]["category"],
                      {"system", "network", "hardware", "optional"})

    def test_desktop_entry(self):
        cp = configparser.ConfigParser()
        cp.read(REPO / "shell" / "session" / "castalia.desktop")
        entry = cp["Desktop Entry"]
        self.assertEqual(entry["Exec"], "castalia-session")
        self.assertEqual(entry["Type"], "XSession")
        self.assertEqual(entry["Name"], "Castalia Classic")


if __name__ == "__main__":
    sys.exit(unittest.main())
