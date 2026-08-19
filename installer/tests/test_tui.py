"""Unit tests for the text installer (Bible §14.2 / §14.5 #5)."""

import unittest

from castalia_installer.engine import DryRunner
from castalia_installer.model import DiskInfo
from castalia_installer.tui import TextInstaller


class ScriptedInput:
    """Feeds a fixed list of answers to successive prompts."""

    def __init__(self, answers):
        self.answers = list(answers)

    def __call__(self, _prompt):
        return self.answers.pop(0) if self.answers else ""


def make(answers, disks=None, runner=None):
    disks = disks if disks is not None else [
        DiskInfo(path="/dev/sda", size_mib=40 * 1024, model="QEMU"),
        DiskInfo(path="/dev/sdb", size_mib=16 * 1024, model="USB",
                 removable=True),
    ]
    runner = runner or DryRunner()
    out_lines = []
    ti = TextInstaller(disks, ScriptedInput(answers), out_lines.append, runner)
    return ti, runner, out_lines


class Flow(unittest.TestCase):
    def test_full_install_with_confirmation(self):
        # disk 1, defaults for host/user/name, then type the disk to confirm.
        ti, runner, _ = make(
            ["1", "pc-castalia", "dave", "Dave", "/dev/sda"])
        self.assertEqual(ti.run(), 0)
        issued = {tok for call in runner.calls for tok in call}
        self.assertIn("parted", issued)
        self.assertIn("mkfs.ext4", issued)
        self.assertIn("useradd", issued)

    def test_cancels_on_wrong_confirmation(self):
        ti, runner, out = make(["1", "", "", "", "/dev/WRONG"])
        self.assertEqual(ti.run(), 3)
        # nothing destructive ran
        self.assertEqual(runner.calls, [])
        self.assertTrue(any("cancelada" in ln for ln in out))

    def test_picks_the_second_disk(self):
        ti, runner, _ = make(["2", "", "", "", "/dev/sdb"])
        self.assertEqual(ti.run(), 0)
        # every parted call targets the chosen disk
        parted = [c for c in runner.calls if c and c[0] == "parted"]
        self.assertTrue(parted)
        for c in parted:
            self.assertIn("/dev/sdb", c)

    def test_no_disks(self):
        ti, _, out = make([], disks=[])
        self.assertEqual(ti.run(), 2)
        self.assertTrue(any("ningún disco" in ln for ln in out))

    def test_invalid_disk_choice(self):
        ti, _, _ = make(["9"])
        self.assertEqual(ti.run(), 2)

    def test_invalid_username_rejected(self):
        ti, runner, out = make(["1", "castalia", "Bad User", "Dave"])
        self.assertEqual(ti.run(), 2)
        self.assertEqual(runner.calls, [])


if __name__ == "__main__":
    unittest.main()
