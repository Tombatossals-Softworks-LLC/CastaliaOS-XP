"""Unit tests for the execution engine and its safety gate (Bible §14.5)."""

import unittest

from castalia_installer.engine import (
    ConfirmationRequired,
    DryRunner,
    execute,
)
from castalia_installer.model import InstallConfig
from castalia_installer.plan import build_plan


class SafetyGate(unittest.TestCase):
    def setUp(self):
        self.cfg = InstallConfig(target_disk="/dev/sda")
        self.plan = build_plan(self.cfg, 40 * 1024)

    def test_refuses_without_confirmation(self):
        # §14.5 #1: no destructive action without a typed confirmation.
        with self.assertRaises(ConfirmationRequired):
            execute(self.plan, DryRunner(), confirm_disk=None)

    def test_refuses_wrong_disk_confirmation(self):
        with self.assertRaises(ConfirmationRequired):
            execute(self.plan, DryRunner(), confirm_disk="/dev/sdb")

    def test_nothing_ran_before_refusal(self):
        runner = DryRunner()
        try:
            execute(self.plan, runner, confirm_disk=None)
        except ConfirmationRequired:
            pass
        # The very first step is destructive, so no command should have run.
        self.assertEqual(runner.calls, [])

    def test_runs_fully_with_confirmation(self):
        runner = DryRunner()
        execute(self.plan, runner, confirm_disk="/dev/sda")
        # partitioning, mkfs, rsync, grub, useradd all issued (useradd runs
        # under chroot, so match anywhere in the argv, not just argv[0]).
        issued = {tok for call in runner.calls for tok in call}
        for expected in ("parted", "mkfs.ext4", "mkswap", "rsync",
                         "grub-install", "useradd"):
            self.assertIn(expected, issued)

    def test_fstab_written_with_probed_uuids(self):
        runner = DryRunner()
        execute(self.plan, runner, confirm_disk="/dev/sda")
        fstab = dict(runner.writes).get("/target/etc/fstab", "")
        # DryRunner returns UUID-<devname>; fstab must interpolate them.
        self.assertIn("UUID=UUID-sda3  /", fstab)
        self.assertIn("UUID=UUID-sda1  /boot", fstab)
        self.assertIn("UUID=UUID-sda2  none   swap", fstab)

    def test_chroot_steps_are_prefixed(self):
        runner = DryRunner()
        execute(self.plan, runner, confirm_disk="/dev/sda")
        useradd = [c for c in runner.calls if "useradd" in c][0]
        self.assertEqual(useradd[:2], ["chroot", "/target"])

    def test_hostname_and_hosts_written(self):
        runner = DryRunner()
        execute(self.plan, runner, confirm_disk="/dev/sda")
        paths = dict(runner.writes)
        self.assertIn("/target/etc/hostname", paths)
        self.assertIn("/target/etc/hosts", paths)

    def test_password_fed_on_stdin_before_target_unmount(self):
        plan = build_plan(self.cfg, 40 * 1024, set_password=True)
        runner = DryRunner()
        execute(plan, runner, confirm_disk="/dev/sda",
                secrets={"password": "s3cret"})
        chpasswd = [i for i, c in enumerate(runner.calls) if "chpasswd" in c]
        self.assertEqual(len(chpasswd), 1)
        i = chpasswd[0]
        # username default is "usuario"; password arrives on stdin, not argv
        self.assertEqual(runner.inputs[i], "usuario:s3cret\n")
        self.assertNotIn("s3cret", " ".join(runner.calls[i]))
        self.assertEqual(runner.calls[i], ["chroot", "/target", "chpasswd"])
        # and it must precede unmounting /target
        umount_target = [j for j, c in enumerate(runner.calls)
                         if c[:1] == ["umount"] and c[-1] == "/target"]
        self.assertTrue(umount_target)
        self.assertLess(i, min(umount_target))

    def test_no_stdin_for_ordinary_steps(self):
        runner = DryRunner()
        execute(self.plan, runner, confirm_disk="/dev/sda")
        self.assertTrue(all(inp is None for inp in runner.inputs))


if __name__ == "__main__":
    unittest.main()
