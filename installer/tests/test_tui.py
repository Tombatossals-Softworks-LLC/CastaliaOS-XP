"""Unit tests for the text installer (Bible §14.2 / §14.5 #5).

"Never a dead end" is what this file guards, and the interesting half of it is
not that the installer finishes — it is that a machine which fell back to text
mode because its graphics are broken can still keep the Windows it has on it.
That machine is *more* likely to be dual-booting, not less, so most of what is
below is about the modes reaching the text path and refusing there in the same
words the backend refuses in.
"""

import unittest

from castalia_installer.engine import DryRunner
from castalia_installer.model import (
    MIN_ALONGSIDE_MIB,
    DiskInfo,
    PartitionInfo,
)
from castalia_installer.tui import TextInstaller

GiB = 1024


class ScriptedInput:
    """Feeds a fixed list of answers to successive prompts.

    Records what it was asked, so a test can assert the *questions* as well
    as the outcome — a prompt that silently stopped being asked is a default
    somebody did not choose.
    """

    def __init__(self, answers):
        self.answers = list(answers)
        self.prompts = []

    def __call__(self, prompt):
        self.prompts.append(prompt)
        return self.answers.pop(0) if self.answers else ""


def windows(size_gib=100, start=1, index=1, fstype="ntfs"):
    return PartitionInfo(f"/dev/sda{index}", index, start, size_gib * GiB,
                         fstype, "Windows")


def make(answers, disks=None, runner=None, partitions=(), used=None,
         passwords=("", "")):
    disks = disks if disks is not None else [
        DiskInfo(path="/dev/sda", size_mib=200 * GiB, model="QEMU"),
        DiskInfo(path="/dev/sdb", size_mib=16 * GiB, model="USB",
                 removable=True),
    ]
    runner = runner or DryRunner()
    out_lines = []
    secrets = ScriptedInput(list(passwords))
    inp = ScriptedInput(answers)
    ti = TextInstaller(
        disks, inp, out_lines.append, runner,
        secret=secrets,
        probe_partitions=lambda _disk: list(partitions),
        probe_used=lambda _parts: dict(used or {}),
    )
    ti._scripted_input = inp          # for prompt assertions
    return ti, runner, out_lines


class WholeDiskFlow(unittest.TestCase):
    """The original path, on a disk with nothing on it."""

    def test_full_install_with_confirmation(self):
        # disk 1, mode 1 (the only one on a blank disk), host/user/name,
        # then type the disk to confirm.
        ti, runner, _ = make(
            ["1", "1", "pc-castalia", "dave", "Dave", "/dev/sda"])
        self.assertEqual(ti.run(), 0)
        issued = {tok for call in runner.calls for tok in call}
        self.assertIn("parted", issued)
        self.assertIn("mkfs.ext4", issued)
        self.assertIn("useradd", issued)

    def test_cancels_on_wrong_confirmation(self):
        ti, runner, out = make(["1", "1", "", "", "", "/dev/WRONG"])
        self.assertEqual(ti.run(), 3)
        self.assertEqual(runner.calls, [])
        self.assertTrue(any("cancelada" in ln for ln in out))

    def test_picks_the_second_disk(self):
        ti, runner, _ = make(["2", "1", "", "", "", "/dev/sdb"])
        self.assertEqual(ti.run(), 0)
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

    def test_a_zero_or_negative_disk_choice_is_not_the_last_disk(self):
        # Python indexing would turn "0" into disks[-1] — the last disk in
        # the list, chosen by somebody who typed something else entirely.
        for bad in ("0", "-1"):
            ti, runner, out = make([bad])
            self.assertEqual(ti.run(), 2, bad)
            self.assertEqual(runner.calls, [])

    def test_invalid_username_rejected(self):
        ti, runner, _ = make(["1", "1", "castalia", "Bad User", "Dave"])
        self.assertEqual(ti.run(), 2)
        self.assertEqual(runner.calls, [])


class ModesReachTheTextInstallerTest(unittest.TestCase):
    """The point of the rewrite: text mode is not whole-disk-only."""

    def test_a_disk_with_a_windows_and_free_space_offers_three_modes(self):
        parts = [windows(size_gib=100)]
        ti, _, out = make(["1", "1", "", "", "", "/dev/sda"],
                          partitions=parts, used={"/dev/sda1": 40 * GiB})
        ti.run()
        screen = "\n".join(out)
        self.assertIn("Instalar junto al sistema actual", screen)
        self.assertIn("Hacer sitio encogiendo una partición", screen)
        self.assertIn("Usar el disco entero", screen)

    def test_the_first_option_is_the_least_destructive_one(self):
        # Whatever is option 1 is what a tired person presses Enter on.
        parts = [windows(size_gib=100)]
        ti, runner, out = make(["1", "1", "", "", "", "/dev/sda"],
                               partitions=parts,
                               used={"/dev/sda1": 40 * GiB})
        self.assertEqual(ti.run(), 0)
        screen = "\n".join(out)
        self.assertIn("1) Instalar junto al sistema actual", screen)
        # ...and it really did install alongside: no new partition table.
        for call in runner.calls:
            self.assertNotIn("mklabel", call)

    def test_it_shows_what_is_already_on_the_disk_before_asking(self):
        parts = [windows(size_gib=100)]
        ti, _, out = make(["1", "1", "", "", "", "/dev/sda"],
                          partitions=parts, used={"/dev/sda1": 40 * GiB})
        ti.run()
        screen = "\n".join(out)
        self.assertIn("En este disco ya hay:", screen)
        self.assertIn("Windows", screen)

    def test_a_full_disk_does_not_offer_alongside(self):
        ti, _, out = make(["1", "1", "", "", "", "", "/dev/sda"],
                          disks=[DiskInfo(path="/dev/sda",
                                          size_mib=100 * GiB)],
                          partitions=[windows(size_gib=99)],
                          used={"/dev/sda1": 40 * GiB})
        ti.run()
        screen = "\n".join(out)
        self.assertNotIn("Instalar junto al sistema actual", screen)
        self.assertIn("1) Hacer sitio encogiendo una partición", screen)

    def test_alongside_install_lands_in_the_free_region(self):
        parts = [windows(size_gib=100)]
        ti, runner, _ = make(["1", "1", "", "", "", "/dev/sda"],
                             partitions=parts, used={"/dev/sda1": 40 * GiB})
        self.assertEqual(ti.run(), 0)
        mkparts = [c for c in runner.calls if "mkpart" in c]
        self.assertTrue(mkparts)
        for call in mkparts:
            start = next(a for a in call if a.endswith("MiB"))
            self.assertGreaterEqual(int(start[:-3]), 100 * GiB)


SMALL_DISK = [DiskInfo(path="/dev/sda", size_mib=100 * GiB, model="QEMU")]


class ShrinkFlowTest(unittest.TestCase):
    """A 100 GiB disk full of Windows: every offset is inside the first
    128 GiB, so §6.2 imposes no floor and the arithmetic is the plain kind.
    The §6.2 floor gets its own class below."""

    def small(self, answers, used_gib=40):
        return make(answers, disks=list(SMALL_DISK),
                    partitions=[windows(size_gib=99)],
                    used={"/dev/sda1": used_gib * GiB})

    def test_it_shows_the_numbers_before_asking_how_much_to_take(self):
        # Somebody about to shrink their Windows needs to see how full it is
        # and what the ceiling is, on the same screen as the question.
        ti, _, out = self.small(["1", "1", "", "", "", "", "/dev/sda"])
        self.assertEqual(ti.run(), 0)
        screen = "\n".join(out)
        self.assertIn("En uso ahora:", screen)
        self.assertIn("Puede ceder hasta:", screen)

    def test_it_says_what_the_partition_will_become(self):
        ti, _, out = self.small(["1", "1", "", "", "", "", "/dev/sda"])
        ti.run()
        screen = "\n".join(out)
        self.assertIn("/dev/sda1 pasará de", screen)
        self.assertIn("dejando", screen)

    def test_the_shrink_really_happens(self):
        ti, runner, _ = self.small(["1", "1", "", "", "", "", "/dev/sda"])
        self.assertEqual(ti.run(), 0)
        tools = [c[0] for c in runner.calls if c]
        self.assertIn("ntfsresize", tools)
        self.assertIn("sfdisk", tools)

    def test_asking_for_more_than_is_available_is_re_asked_not_clamped(self):
        # Clamping would be the installer deciding how much of somebody's
        # Windows to take, having been told a different number.
        ti, runner, out = self.small(
            ["1", "1", "", "", "", "999999", "20480", "/dev/sda"])
        self.assertEqual(ti.run(), 0)
        self.assertTrue(any("Tiene que estar entre" in ln for ln in out))
        sfdisk = next(c for c in runner.calls if c and c[0] == "sfdisk")
        self.assertIn("/dev/sda", sfdisk)

    def test_a_non_numeric_answer_is_re_asked(self):
        ti, _, out = self.small(
            ["1", "1", "", "", "", "mucho", "20480", "/dev/sda"])
        self.assertEqual(ti.run(), 0)
        self.assertTrue(any("no es un número" in ln for ln in out))

    def test_it_will_not_take_less_than_an_install_needs(self):
        ti, _, out = self.small(
            ["1", "1", "", "", "", "100", str(20 * GiB), "/dev/sda"])
        self.assertEqual(ti.run(), 0)
        self.assertTrue(any(str(MIN_ALONGSIDE_MIB) in ln for ln in out))

    def test_shrink_is_not_offered_for_a_filesystem_we_will_not_resize(self):
        ti, _, out = make(["1", "1", "", "", "", "/dev/sda"],
                          disks=list(SMALL_DISK),
                          partitions=[windows(size_gib=99, fstype="xfs")],
                          used={"/dev/sda1": 40 * GiB})
        ti.run()
        screen = "\n".join(out)
        self.assertNotIn("Hacer sitio encogiendo", screen)


class TheHundredAndTwentyEightGigRuleTest(unittest.TestCase):
    """§6.2, in the place it is least expected.

    /boot has to sit inside the first 128 GiB so a vintage BIOS can read the
    kernel. A shrink opens its gap at the partition's *new end*, so on a
    partition that reaches out past that mark, taking too LITTLE leaves the
    gap — and /boot with it — out of reach. The constraint is a floor, and it
    is satisfied by taking more space rather than less, which is the opposite
    of every other limit in this feature.

    Before this existed the plan raised ValueError from inside build_plan and
    the text installer died with a traceback on the last screen.
    """

    def big(self, answers, size_gib=199):
        return make(answers,
                    disks=[DiskInfo(path="/dev/sda", size_mib=200 * GiB,
                                    model="QEMU")],
                    partitions=[windows(size_gib=size_gib)],
                    used={"/dev/sda1": 40 * GiB})

    def test_it_says_the_floor_and_why(self):
        ti, _, out = self.big(["1", "1", "", "", "", "", "/dev/sda"])
        self.assertEqual(ti.run(), 0)
        screen = "\n".join(out)
        self.assertIn("Hay que liberar al menos", screen)
        self.assertIn("128 GiB", screen)

    def test_taking_too_little_is_refused_not_crashed_on(self):
        ti, runner, out = self.big(
            ["1", "1", "", "", "", "20480", "90000", "/dev/sda"])
        self.assertEqual(ti.run(), 0)
        self.assertTrue(any("Tiene que estar entre" in ln for ln in out))

    def test_the_layout_really_lands_inside_the_first_128_gib(self):
        ti, runner, _ = self.big(["1", "1", "", "", "", "", "/dev/sda"])
        self.assertEqual(ti.run(), 0)
        mkpart = next(c for c in runner.calls if "mkpart" in c)
        start = next(a for a in mkpart if a.endswith("MiB"))
        self.assertLessEqual(int(start[:-3]) + 1024, 128 * 1024)

    def test_alongside_is_not_offered_for_a_gap_past_the_mark(self):
        # A 400 GiB disk whose only free space starts at 300 GiB. The mode
        # used to be offered and then raised while building the plan.
        disk = DiskInfo(path="/dev/sda", size_mib=400 * GiB)
        parts = [windows(size_gib=300)]
        ti, _, out = make(["1", "1", "", "", "", "/dev/sda"], disks=[disk],
                          partitions=parts, used={})
        ti.run()
        self.assertNotIn("Instalar junto al sistema actual", "\n".join(out))


class WhatItSaysBeforeTheLastKeypressTest(unittest.TestCase):
    """The confirmation screen is the last chance to notice a mistake."""

    def test_whole_disk_names_what_will_be_lost(self):
        parts = [windows(size_gib=100)]
        ti, _, out = make(["1", "3", "", "", "", "/dev/sda"],
                          partitions=parts, used={"/dev/sda1": 40 * GiB})
        self.assertEqual(ti.run(), 0)
        screen = "\n".join(out)
        self.assertIn("se borrará TODO", screen)
        self.assertIn("se pierde: /dev/sda1", screen)

    def test_alongside_says_nothing_existing_is_touched(self):
        parts = [windows(size_gib=100)]
        ti, _, out = make(["1", "1", "", "", "", "/dev/sda"],
                          partitions=parts, used={"/dev/sda1": 40 * GiB})
        ti.run()
        screen = "\n".join(out)
        self.assertIn("No se modificará ninguna partición existente", screen)
        self.assertIn("se conserva: /dev/sda1", screen)

    def test_shrink_says_the_data_is_kept_and_asks_for_a_backup(self):
        ti, _, out = make(["1", "1", "", "", "", "", "/dev/sda"],
                          disks=[DiskInfo(path="/dev/sda",
                                          size_mib=100 * GiB)],
                          partitions=[windows(size_gib=99)],
                          used={"/dev/sda1": 40 * GiB})
        ti.run()
        screen = "\n".join(out)
        self.assertIn("Sus datos se conservan", screen)
        self.assertIn("copia de seguridad", screen)


class PasswordTest(unittest.TestCase):
    def test_a_password_is_asked_for_and_set(self):
        ti, runner, _ = make(["1", "1", "", "dave", "", "/dev/sda"],
                             passwords=("hunter2", "hunter2"))
        self.assertEqual(ti.run(), 0)
        chpasswd = [c for c in runner.calls if "chpasswd" in c]
        self.assertTrue(chpasswd, "no password step in the plan")

    def test_a_mismatch_is_asked_again_rather_than_accepted(self):
        ti, runner, out = make(["1", "1", "", "dave", "", "/dev/sda"],
                               passwords=("hunter2", "hunter3",
                                          "hunter2", "hunter2"))
        self.assertEqual(ti.run(), 0)
        self.assertTrue(any("no coinciden" in ln for ln in out))
        self.assertTrue([c for c in runner.calls if "chpasswd" in c])

    def test_the_password_never_reaches_the_plan_text(self):
        # It goes in on stdin. A password in argv is a password in `ps`.
        ti, runner, out = make(["1", "1", "", "dave", "", "/dev/sda"],
                               passwords=("hunter2", "hunter2"))
        ti.run()
        self.assertNotIn("hunter2", "\n".join(out))
        for call in runner.calls:
            self.assertNotIn("hunter2", call)
        self.assertIn("dave:hunter2\n", runner.inputs)

    def test_an_empty_password_leaves_the_account_without_one(self):
        ti, runner, _ = make(["1", "1", "", "dave", "", "/dev/sda"],
                             passwords=("", ""))
        self.assertEqual(ti.run(), 0)
        self.assertFalse([c for c in runner.calls if "chpasswd" in c])


if __name__ == "__main__":
    unittest.main()
