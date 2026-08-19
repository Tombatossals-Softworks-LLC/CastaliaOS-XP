"""Tests for the QEMU boot smoke harness (does not run QEMU itself)."""

import subprocess
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
SCRIPT = REPO / "tests" / "qemu" / "boot-smoke.sh"


class BootSmokeScriptTest(unittest.TestCase):
    text = SCRIPT.read_text(encoding="utf-8")

    def test_posix_syntax(self):
        proc = subprocess.run(["sh", "-n", str(SCRIPT)],
                              capture_output=True, text=True)
        self.assertEqual(proc.returncode, 0, proc.stderr)

    def test_marker_is_userspace_only(self):
        # The default marker must NOT be the bootloader menu title
        # "Castalia Classic" (which also appears on the isolinux screen) —
        # that would false-pass before the kernel even boots.
        marker_line = next(line for line in self.text.splitlines()
                           if line.startswith("MARKER="))
        self.assertNotIn("Castalia Classic", marker_line)
        for token in ("arranque de prueba", "root@castalia",
                      "INIT: Entering runlevel"):
            self.assertIn(token, marker_line)

    def test_floor_tier_defaults(self):
        # §16 FLOOR reference: 512 MB, 1 vCPU, TCG (no KVM flag)
        self.assertIn("MEM=512", self.text)
        self.assertIn("-smp 1", self.text)
        self.assertNotIn("-enable-kvm", self.text)

    def test_requires_an_iso_argument(self):
        proc = subprocess.run(["sh", str(SCRIPT)],
                              capture_output=True, text=True)
        self.assertNotEqual(proc.returncode, 0)

    def test_missing_iso_fails_cleanly(self):
        proc = subprocess.run(["sh", str(SCRIPT), "/nonexistent.iso"],
                              capture_output=True, text=True)
        self.assertNotEqual(proc.returncode, 0)
        self.assertIn("no such ISO", proc.stderr)


if __name__ == "__main__":
    unittest.main()
