"""Installing next to an existing OS without eating it (Bible §14.3, §23.7 #3).

"Dual-boot installs preserve an existing Windows partition, verified" is one
of the seven things §23.7 requires before a public alpha, and the verb that
matters is *preserve*. Almost every test here is a way of asking the same
question — does any step of this plan write outside the free space we were
given — because that is the only question whose wrong answer is somebody's
photographs.
"""
import sys
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HERE))

from castalia_installer.model import (  # noqa: E402
    MIN_ALONGSIDE_MIB,
    MODE_ALONGSIDE,
    MODE_WHOLE_DISK,
    DiskInfo,
    InstallConfig,
    PartitionInfo,
    available_modes,
    free_regions,
    largest_free_region,
)
from castalia_installer.plan import build_plan  # noqa: E402
from castalia_installer.probe import parse_lsblk_partitions  # noqa: E402

GiB = 1024


def disk(size_gib=200):
    return DiskInfo(path="/dev/sda", size_mib=size_gib * GiB)


def windows(size_gib=100, start=1, index=1):
    return PartitionInfo(f"/dev/sda{index}", index, start, size_gib * GiB,
                         "ntfs", "Windows")


def alongside_cfg(d, existing, **over):
    region = largest_free_region(d, existing)
    assert region is not None
    kw = dict(target_disk=d.path, mode=MODE_ALONGSIDE,
              free_start_mib=region.start_mib, free_end_mib=region.end_mib,
              first_index=max((p.index for p in existing), default=0) + 1,
              hostname="pc", username="dave", ram_mib=2048)
    kw.update(over)
    return InstallConfig(**kw)


class FreeSpaceTest(unittest.TestCase):
    def test_gap_after_a_partition(self):
        gaps = free_regions(200 * GiB, [windows()])
        self.assertEqual(len(gaps), 1)
        self.assertEqual(gaps[0].start_mib, 100 * GiB + 1)
        self.assertEqual(gaps[0].end_mib, 200 * GiB)

    def test_gap_between_two_partitions(self):
        parts = [windows(50, start=1, index=1),
                 PartitionInfo("/dev/sda2", 2, 150 * GiB, 50 * GiB, "ntfs")]
        gaps = free_regions(200 * GiB, parts)
        self.assertEqual([(g.start_mib, g.end_mib) for g in gaps],
                         [(50 * GiB + 1, 150 * GiB)])

    def test_a_full_disk_has_no_free_space(self):
        full = PartitionInfo("/dev/sda1", 1, 1, 200 * GiB - 1, "ntfs")
        self.assertEqual(free_regions(200 * GiB, full and [full]), [])

    def test_overlapping_entries_do_not_invent_a_gap(self):
        # A partition table that does not make sense must produce no free
        # space, not a plausible-looking gap that is actually data.
        parts = [PartitionInfo("/dev/sda1", 1, 1, 100 * GiB, "ntfs"),
                 PartitionInfo("/dev/sda2", 2, 50 * GiB, 100 * GiB, "ntfs")]
        gaps = free_regions(200 * GiB, parts)
        for gap in gaps:
            for part in parts:
                self.assertFalse(gap.start_mib < part.end_mib
                                 and part.start_mib < gap.end_mib,
                                 f"{gap} overlaps {part.path}")


class ModeOfferTest(unittest.TestCase):
    def test_alongside_is_offered_when_there_is_room(self):
        self.assertIn(MODE_ALONGSIDE, available_modes(disk(), [windows()]))

    def test_alongside_is_not_offered_on_a_full_disk(self):
        full = [PartitionInfo("/dev/sda1", 1, 1, 200 * GiB - 1, "ntfs")]
        self.assertNotIn(MODE_ALONGSIDE, available_modes(disk(), full))

    def test_alongside_is_not_offered_for_a_gap_too_small_to_use(self):
        big = PartitionInfo("/dev/sda1", 1, 1,
                            200 * GiB - MIN_ALONGSIDE_MIB // 2, "ntfs")
        self.assertNotIn(MODE_ALONGSIDE, available_modes(disk(), [big]))

    def test_alongside_is_not_offered_without_table_room(self):
        # Three primaries are needed and an msdos label holds four. Extended
        # partitions on a vintage BIOS are a trap we decline to walk into.
        parts = [PartitionInfo(f"/dev/sda{i}", i, i * 10 * GiB, 5 * GiB,
                               "ext4") for i in (1, 2)]
        self.assertNotIn(MODE_ALONGSIDE, available_modes(disk(), parts))

    def test_whole_disk_is_always_offered_on_an_installable_disk(self):
        for existing in ([], [windows()],
                         [PartitionInfo("/dev/sda1", 1, 1, 199 * GiB, "ntfs")]):
            self.assertIn(MODE_WHOLE_DISK, available_modes(disk(), existing))


class PreservesTheOtherOsTest(unittest.TestCase):
    """The point of the whole feature, asked five different ways."""

    def setUp(self):
        self.d = disk()
        self.win = windows()
        self.cfg = alongside_cfg(self.d, [self.win])
        self.plan = build_plan(self.cfg, self.d.size_mib)

    def test_the_partition_table_is_never_replaced(self):
        # mklabel is the single command that turns "install beside Windows"
        # into "install over Windows".
        for step in self.plan.steps:
            self.assertNotIn("mklabel", " ".join(step.argv or []),
                             step.title)

    def test_no_step_names_the_existing_partition(self):
        for step in self.plan.steps:
            self.assertNotIn(self.win.path, step.argv or [], step.title)

    def test_nothing_is_written_before_the_free_region(self):
        for part in self.plan.partitions:
            self.assertGreaterEqual(part.start_mib, self.cfg.free_start_mib)

    def test_nothing_is_written_past_the_free_region(self):
        # Whole-disk root runs to "100%"; alongside must stop dead at the end
        # of the gap, because what comes after it is somebody else's data.
        for part in self.plan.partitions:
            self.assertLessEqual(part.end_mib, self.cfg.free_end_mib)
        root = [s for s in self.plan.steps if s.title == "Create root partition"]
        self.assertEqual(len(root), 1)
        self.assertNotIn("100%", root[0].argv)
        self.assertIn(f"{self.cfg.free_end_mib}MiB", root[0].argv)

    def test_new_partitions_continue_the_existing_numbering(self):
        self.assertEqual([p.index for p in self.plan.partitions], [2, 3, 4])
        self.assertEqual(self.plan.boot.index, 2)

    def test_the_boot_flag_goes_on_our_partition_not_partition_one(self):
        step = next(s for s in self.plan.steps
                    if s.title == "Mark /boot bootable")
        self.assertIn("2", step.argv)
        self.assertNotIn("1", step.argv)

    def test_every_disk_writing_step_is_still_gated_as_destructive(self):
        # Not erasing the disk does not make writing a partition table safe;
        # the typed confirmation (§14.5 #1) still has to cover it.
        for title in ("Create /boot partition", "Create swap partition",
                      "Create root partition", "Re-read the partition table"):
            step = next(s for s in self.plan.steps if s.title == title)
            self.assertTrue(step.destructive, title)


class WholeDiskIsUnchangedTest(unittest.TestCase):
    """The mode that erases everything must keep erasing everything."""

    def test_whole_disk_still_writes_a_fresh_label(self):
        cfg = InstallConfig(target_disk="/dev/sda", hostname="pc",
                            username="dave", ram_mib=2048)
        plan = build_plan(cfg, 200 * GiB)
        labels = [s for s in plan.steps if "mklabel" in " ".join(s.argv or [])]
        self.assertEqual(len(labels), 1)
        self.assertTrue(labels[0].destructive)

    def test_whole_disk_root_still_takes_the_rest_of_the_disk(self):
        cfg = InstallConfig(target_disk="/dev/sda", hostname="pc",
                            username="dave", ram_mib=2048)
        plan = build_plan(cfg, 200 * GiB)
        root = next(s for s in plan.steps if s.title == "Create root partition")
        self.assertIn("100%", root.argv)


class ValidationTest(unittest.TestCase):
    def test_a_region_smaller_than_the_minimum_is_refused(self):
        cfg = InstallConfig(target_disk="/dev/sda", mode=MODE_ALONGSIDE,
                            free_start_mib=1, free_end_mib=100,
                            hostname="pc", username="dave")
        self.assertTrue(any("too small" in e for e in cfg.validate(disk())))

    def test_a_region_past_the_end_of_the_disk_is_refused(self):
        cfg = InstallConfig(target_disk="/dev/sda", mode=MODE_ALONGSIDE,
                            free_start_mib=1, free_end_mib=999 * GiB,
                            hostname="pc", username="dave")
        self.assertTrue(any("past the disk" in e for e in cfg.validate(disk())))

    def test_no_room_in_the_partition_table_is_refused(self):
        cfg = InstallConfig(target_disk="/dev/sda", mode=MODE_ALONGSIDE,
                            free_start_mib=1, free_end_mib=100 * GiB,
                            first_index=3, hostname="pc", username="dave")
        self.assertTrue(any("no room in the partition table" in e
                            for e in cfg.validate(disk())))

    def test_boot_past_the_first_128_gib_is_refused(self):
        # §6.2: a vintage BIOS may not be able to read a kernel that far in,
        # and free space late on a big disk is exactly where that bites.
        d = DiskInfo(path="/dev/sda", size_mib=400 * GiB)
        cfg = InstallConfig(target_disk="/dev/sda", mode=MODE_ALONGSIDE,
                            free_start_mib=200 * GiB, free_end_mib=400 * GiB,
                            first_index=2, hostname="pc", username="dave",
                            ram_mib=2048)
        with self.assertRaises(ValueError) as caught:
            build_plan(cfg, d.size_mib)
        self.assertIn("128 GiB", str(caught.exception))


class ProbeTest(unittest.TestCase):
    SAMPLE = (
        "sda    0          214748364800 disk\n"
        "|-sda1 2048       555745280    part vfat  SYSTEM\n"
        "|-sda2 1085440    107374182400 part ntfs  Windows\n"
        "`-sda3 210864128  16106127360  part ntfs  Recovery\n"
    )

    def test_reads_offsets_and_types(self):
        parts = parse_lsblk_partitions(self.SAMPLE, "/dev/sda")
        self.assertEqual([p.path for p in parts],
                         ["/dev/sda1", "/dev/sda2", "/dev/sda3"])
        self.assertEqual(parts[1].kind, "windows")
        self.assertEqual(parts[1].size_mib, 100 * GiB)
        self.assertEqual(parts[1].start_mib, 530)

    def test_ignores_other_disks(self):
        text = self.SAMPLE + "sdb    0  100 disk\n`-sdb1 2048  100 part ext4\n"
        parts = parse_lsblk_partitions(text, "/dev/sda")
        self.assertTrue(all(p.path.startswith("/dev/sda") for p in parts))

    def test_ignores_lvm_and_luks_children(self):
        # A volume on top of a partition is somebody's storage stack, not a
        # span to plan around.
        text = self.SAMPLE + "  `-vg-root 0 1000 lvm  ext4\n"
        self.assertEqual(len(parse_lsblk_partitions(text, "/dev/sda")), 3)

    def test_a_partition_with_no_filesystem_is_not_called_windows(self):
        parts = parse_lsblk_partitions("sda1 2048 1000000 part\n", "/dev/sda")
        self.assertEqual(parts[0].kind, "unknown")

    def test_describe_is_human_and_says_what_it_found(self):
        parts = parse_lsblk_partitions(self.SAMPLE, "/dev/sda")
        self.assertIn("Windows", parts[1].describe())
        self.assertIn("/dev/sda2", parts[1].describe())


if __name__ == "__main__":
    unittest.main()
