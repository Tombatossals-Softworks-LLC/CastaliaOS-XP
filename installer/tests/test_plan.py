"""Unit tests for the install planner (Bible §14.3)."""

import unittest

from castalia_installer.model import (
    BOOT_MIB,
    FIRST_128_GIB_MIB,
    InstallConfig,
)
from castalia_installer.plan import (
    build_layout,
    build_plan,
    render_fstab,
    render_hostname,
    render_hosts,
)


class Layout(unittest.TestCase):
    def setUp(self):
        self.cfg = InstallConfig(target_disk="/dev/sda", ram_mib=2048)
        self.parts = build_layout(self.cfg, 40 * 1024)

    def test_three_partitions(self):
        self.assertEqual([p.role for p in self.parts],
                         ["boot", "swap", "root"])

    def test_boot_size(self):
        self.assertEqual(self.parts[0].size_mib, BOOT_MIB)

    def test_swap_follows_ram(self):
        self.assertEqual(self.parts[1].size_mib, 2048)

    def test_partitions_are_contiguous(self):
        for a, b in zip(self.parts, self.parts[1:]):
            self.assertEqual(a.end_mib, b.start_mib)

    def test_root_takes_the_rest(self):
        self.assertEqual(self.parts[2].end_mib, 40 * 1024)

    def test_boot_within_first_128gib(self):
        self.assertLessEqual(self.parts[0].end_mib, FIRST_128_GIB_MIB)

    def test_no_overlap_and_within_disk(self):
        self.assertGreaterEqual(self.parts[0].start_mib, 1)
        self.assertLessEqual(self.parts[-1].end_mib, 40 * 1024)


class PlanShape(unittest.TestCase):
    def setUp(self):
        self.cfg = InstallConfig(target_disk="/dev/sda", hostname="pc-castalia",
                                 username="dave")
        self.plan = build_plan(self.cfg, 40 * 1024)

    def test_first_destructive_is_mklabel(self):
        first = self.plan.steps[0]
        self.assertTrue(first.destructive)
        self.assertEqual(first.argv[:4],
                         ["parted", "-s", "/dev/sda", "mklabel"])

    def test_has_mkfs_for_boot_and_root(self):
        cmds = [s.argv for s in self.plan.steps if s.argv]
        self.assertIn(["mkfs.ext4", "-F", "-L", "castalia-boot", "/dev/sda1"],
                      cmds)
        self.assertIn(["mkfs.ext4", "-F", "-L", "castalia-root", "/dev/sda3"],
                      cmds)
        self.assertIn(["mkswap", "-L", "castalia-swap", "/dev/sda2"], cmds)

    def test_copies_the_system(self):
        rsyncs = [s for s in self.plan.steps
                  if s.argv and s.argv[0] == "rsync"]
        self.assertEqual(len(rsyncs), 1)
        self.assertIn("--exclude", rsyncs[0].argv)
        self.assertTrue(rsyncs[0].argv[-1].endswith("/target/"))

    def test_installs_grub_to_the_disk(self):
        grub = [s for s in self.plan.steps
                if s.argv and s.argv[0] == "grub-install"]
        self.assertEqual(len(grub), 1)
        self.assertIn("/dev/sda", grub[0].argv)
        self.assertTrue(grub[0].destructive)

    def test_grub_boots_by_uuid(self):
        # A device-independence fixup runs in the chroot right after
        # grub-mkconfig, rewriting root=/dev/... to root=UUID=...
        titles = [s.title for s in self.plan.steps]
        self.assertIn("Make GRUB boot by UUID (device-independent)", titles)
        gi = titles.index("Generate GRUB config")
        fi = titles.index("Make GRUB boot by UUID (device-independent)")
        self.assertEqual(fi, gi + 1)
        step = self.plan.steps[fi]
        self.assertTrue(step.chroot)
        self.assertIn("grub.cfg", " ".join(step.argv))

    def test_boot_identity_lands_before_the_menu_is_generated(self):
        # /etc/default/grub, the theme and the Safe Mode generator are inputs
        # to grub-mkconfig. Any of them written afterwards is a file that only
        # takes effect on the *next* update-grub — i.e. never, for a machine
        # that is installed once and then just used.
        titles = [s.title for s in self.plan.steps]
        gen = titles.index("Generate GRUB config")
        for title in ("Write /etc/default/grub (boot identity, §6.2)",
                      "Install the Castalia GRUB theme",
                      "Install the Safe Mode boot entry (§6.2)"):
            self.assertIn(title, titles, title)
            self.assertLess(titles.index(title), gen, title)

    def test_boot_identity_is_written_into_the_target(self):
        step = next(s for s in self.plan.steps
                    if s.title.startswith("Write /etc/default/grub"))
        self.assertIsNotNone(step.write)
        path, render = step.write
        self.assertEqual(path, "/target/etc/default/grub")
        self.assertIn('GRUB_DISTRIBUTOR="Castalia OS"', render({}))

    def test_boot_asset_steps_are_fail_open(self):
        # A missing theme is a plain-looking menu. It must never be a failed
        # install, so both scripts end in an unconditional exit 0 and run in
        # the chroot, where /usr/share/castalia is the target's copy.
        for title in ("Install the Castalia GRUB theme",
                      "Install the Safe Mode boot entry (§6.2)"):
            step = next(s for s in self.plan.steps if s.title == title)
            self.assertTrue(step.chroot, title)
            self.assertFalse(step.destructive, title)
            script = step.argv[-1]
            self.assertEqual(step.argv[:2], ["sh", "-c"], title)
            self.assertTrue(script.rstrip().endswith("exit 0"), title)
            self.assertIn("/usr/share/castalia/grub", script, title)

    def test_creates_the_user_in_chroot(self):
        useradd = [s for s in self.plan.steps
                   if s.argv and s.argv[0] == "useradd"]
        self.assertEqual(len(useradd), 1)
        self.assertTrue(useradd[0].chroot)
        self.assertIn("dave", useradd[0].argv)

    def test_no_network_steps_offline_capable(self):
        # §14.5 #4: a full install completes offline — nothing reaches out.
        net = {"apt-get", "curl", "wget", "ping", "nmcli", "dhclient"}
        for s in self.plan.steps:
            if s.argv:
                self.assertNotIn(s.argv[0], net)

    def test_nvme_partition_names(self):
        cfg = InstallConfig(target_disk="/dev/nvme0n1")
        plan = build_plan(cfg, 40 * 1024)
        cmds = [s.argv for s in plan.steps if s.argv]
        self.assertIn(
            ["mkfs.ext4", "-F", "-L", "castalia-root", "/dev/nvme0n1p3"], cmds)

    def test_set_password_step_before_unmount(self):
        # The password step must run in the chroot while /target is mounted —
        # i.e. before any umount step (regression: it used to run after).
        plan = build_plan(self.cfg, 40 * 1024, set_password=True)
        pw = [i for i, s in enumerate(plan.steps)
              if s.stdin_key == "password"]
        self.assertEqual(len(pw), 1)
        step = plan.steps[pw[0]]
        self.assertTrue(step.chroot)
        self.assertTrue(step.sensitive)
        self.assertEqual(step.argv, ["chpasswd"])
        umounts = [i for i, s in enumerate(plan.steps)
                   if s.argv and s.argv[0] == "umount"]
        self.assertTrue(umounts)
        self.assertLess(pw[0], min(umounts))

    def test_no_password_step_by_default(self):
        plan = build_plan(self.cfg, 40 * 1024)
        self.assertFalse(any(s.stdin_key for s in plan.steps))

    def test_password_step_is_redacted(self):
        plan = build_plan(self.cfg, 40 * 1024, set_password=True)
        step = next(s for s in plan.steps if s.stdin_key == "password")
        self.assertIn("redacted", step.describe())

    def test_configure_target_false_omits_chroot(self):
        # The disk-only subset (for the loopback smoke) has no chroot steps
        # and still copies + writes fstab.
        plan = build_plan(self.cfg, 40 * 1024, configure_target=False)
        self.assertFalse(any(s.chroot for s in plan.steps))
        self.assertFalse(any(s.argv and s.argv[0] == "grub-install"
                             for s in plan.steps))
        self.assertTrue(any(s.argv and s.argv[0] == "rsync"
                            for s in plan.steps))
        self.assertTrue(any(s.write and s.write[0].endswith("/etc/fstab")
                            for s in plan.steps))


class Renders(unittest.TestCase):
    def _ctx(self):
        cfg = InstallConfig(target_disk="/dev/sda", hostname="pc-castalia")
        return {"config": cfg,
                "uuids": {"boot": "BID", "swap": "SID", "root": "RID"}}

    def test_fstab_uses_uuids_and_pass_order(self):
        text = render_fstab(self._ctx())
        self.assertIn("UUID=RID  /      ext4", text)
        self.assertIn("UUID=BID  /boot  ext4", text)
        self.assertIn("UUID=SID  none   swap", text)
        # root is fsck pass 1, boot pass 2, swap pass 0
        self.assertTrue(text.rstrip().endswith("sw                 0  0"))

    def test_hostname_render(self):
        self.assertEqual(render_hostname(self._ctx()), "pc-castalia\n")

    def test_hosts_has_loopback_and_host(self):
        text = render_hosts(self._ctx())
        self.assertIn("127.0.0.1\tlocalhost", text)
        self.assertIn("127.0.1.1\tpc-castalia", text)


if __name__ == "__main__":
    unittest.main()
