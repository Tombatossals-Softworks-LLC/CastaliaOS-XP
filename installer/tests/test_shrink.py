"""Making room on a disk that has none (Bible §14.3, §14.5).

Alongside install only works on a machine that already has unallocated space,
which in practice means a machine somebody has already prepared. Shrinking is
what makes "install next to Windows" true on a normal computer — and it is the
only thing this installer does to data that was on the disk before it arrived.

So the tests are not about whether the arithmetic is right. They are about
what the installer refuses, and about the one ordering it must never get
wrong: the filesystem shrinks before the partition does. Reversed, the
partition boundary lands inside a filesystem that still believes it owns the
space past it, and everything out there is gone with no error at the time.
"""
import sys
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HERE))

from castalia_installer.model import (  # noqa: E402
    MIN_ALONGSIDE_MIB,
    MODE_ALONGSIDE,
    MODE_SHRINK,
    MODE_WHOLE_DISK,
    SHRINK_KEEP_FREE_FRACTION,
    SHRINK_KEEP_FREE_MIB,
    DiskInfo,
    InstallConfig,
    PartitionInfo,
    available_modes,
    max_freeable_mib,
    min_shrink_mib,
    min_size_after_shrink,
    plan_shrink,
    shrink_candidates,
)
from castalia_installer.plan import build_plan, build_shrink_steps  # noqa: E402
from castalia_installer.probe import (  # noqa: E402
    parse_ext_used_mib,
    parse_ntfs_used_mib,
)

GiB = 1024


def windows(size_gib=200, start=1, index=1, fstype="ntfs"):
    return PartitionInfo(f"/dev/sda{index}", index, start, size_gib * GiB,
                         fstype, "Windows")


def disk(size_gib=200):
    return DiskInfo(path="/dev/sda", size_mib=size_gib * GiB)


def shrink_cfg(part, used_mib, free_mib=None, **over):
    sp = plan_shrink(part, used_mib, free_mib)
    cfg = InstallConfig(
        target_disk="/dev/sda", username="dave", hostname="pc",
        mode=MODE_SHRINK, ram_mib=2048, shrink=sp,
        free_start_mib=sp.freed.start_mib, free_end_mib=sp.freed.end_mib,
        first_index=part.index + 1, **over)
    return cfg


class WhatWeRefuseToTouchTest(unittest.TestCase):
    def test_a_filesystem_we_have_no_resizer_for_is_refused(self):
        for fstype in ("vfat", "xfs", "btrfs", "hfsplus", ""):
            part = windows(fstype=fstype)
            self.assertEqual(part.shrink_tool, "", fstype)
            self.assertEqual(max_freeable_mib(part, 10 * GiB), 0, fstype)
            with self.assertRaises(ValueError) as ctx:
                plan_shrink(part, 10 * GiB)
            self.assertIn("will not resize", str(ctx.exception))

    def test_the_filesystems_we_do_resize(self):
        for fstype, tool in (("ntfs", "ntfsresize"), ("NTFS", "ntfsresize"),
                             ("ext4", "resize2fs"), ("ext3", "resize2fs"),
                             ("ext2", "resize2fs")):
            self.assertEqual(windows(fstype=fstype).shrink_tool, tool, fstype)

    def test_a_used_figure_that_cannot_be_right_is_refused(self):
        # More used than the partition holds, or negative. Either means the
        # measurement is wrong, and a shrink planned on a wrong measurement
        # is the one that takes the data.
        part = windows(size_gib=100)
        for bad in (-1, 100 * GiB + 1, 500 * GiB):
            with self.assertRaises(ValueError) as ctx:
                plan_shrink(part, bad)
            self.assertIn("refusing to plan a shrink", str(ctx.exception))

    def test_a_nearly_full_filesystem_cannot_be_shrunk(self):
        part = windows(size_gib=60)
        with self.assertRaises(ValueError) as ctx:
            plan_shrink(part, 55 * GiB)
        self.assertIn("can only spare", str(ctx.exception))

    def test_asking_for_more_than_is_safe_is_refused(self):
        part = windows(size_gib=200)
        room = max_freeable_mib(part, 40 * GiB)
        with self.assertRaises(ValueError) as ctx:
            plan_shrink(part, 40 * GiB, room + 1)
        self.assertIn("only", str(ctx.exception))
        # ...and exactly the maximum is allowed, or the boundary is a lie.
        self.assertEqual(plan_shrink(part, 40 * GiB, room).freed_mib, room)

    def test_asking_for_less_than_an_install_needs_is_refused(self):
        part = windows(size_gib=200)
        with self.assertRaises(ValueError):
            plan_shrink(part, 40 * GiB, MIN_ALONGSIDE_MIB - 1)


class HowMuchWeLeaveBehindTest(unittest.TestCase):
    """The neighbour has to still work afterwards, not merely still exist."""

    def test_a_small_filesystem_keeps_the_flat_reserve(self):
        self.assertEqual(min_size_after_shrink(10 * GiB),
                         10 * GiB + SHRINK_KEEP_FREE_MIB)

    def test_a_large_filesystem_keeps_a_proportional_reserve(self):
        # 300 GiB in use: 4 GiB of slack would be a Windows that cannot
        # install an update. 15% is what it gets instead.
        used = 300 * GiB
        self.assertEqual(min_size_after_shrink(used),
                         used + int(used * SHRINK_KEEP_FREE_FRACTION))
        self.assertGreater(min_size_after_shrink(used) - used,
                           SHRINK_KEEP_FREE_MIB)

    def test_we_never_shrink_below_what_is_in_there(self):
        part = windows(size_gib=100)
        for used_gib in (1, 20, 60, 80):
            room = max_freeable_mib(part, used_gib * GiB)
            if room < min_shrink_mib(part):
                continue
            sp = plan_shrink(part, used_gib * GiB)
            self.assertGreater(sp.new_size_mib, used_gib * GiB,
                               f"{used_gib} GiB in use, shrunk to "
                               f"{sp.new_size_mib} MiB")


class TheFreedSpaceTest(unittest.TestCase):
    def test_the_gap_opens_after_the_partition_not_before_it(self):
        # A filesystem is shrunk from its tail. A plan that expected the gap
        # at the front would have the installer write over the boot sector of
        # whatever it is installing next to.
        part = windows(size_gib=200, start=1)
        sp = plan_shrink(part, 40 * GiB)
        self.assertEqual(sp.freed.start_mib, part.start_mib + sp.new_size_mib)
        self.assertEqual(sp.freed.end_mib, part.end_mib)
        self.assertEqual(sp.freed.size_mib, sp.freed_mib)

    def test_the_gap_never_overlaps_what_is_left_of_the_neighbour(self):
        part = windows(size_gib=100)
        for used_gib in (1, 10, 50, 80):
            room = max_freeable_mib(part, used_gib * GiB)
            if room < min_shrink_mib(part):
                continue
            sp = plan_shrink(part, used_gib * GiB)
            self.assertGreaterEqual(sp.freed.start_mib, sp.new_end_mib)
            self.assertLessEqual(sp.freed.end_mib, part.end_mib)

    def test_a_config_whose_region_is_not_the_freed_one_is_invalid(self):
        # The install region and the shrink have to describe the same span.
        # If they drift apart the layout is written inside a filesystem that
        # was only shrunk on paper.
        part = windows(size_gib=200)
        cfg = shrink_cfg(part, 40 * GiB)
        cfg.free_start_mib -= 1024        # reach back into the neighbour
        problems = cfg.validate(disk())
        self.assertTrue(any("is not the space the shrink frees" in p
                            for p in problems), problems)

    def test_shrink_mode_without_a_shrink_plan_is_invalid(self):
        cfg = InstallConfig(target_disk="/dev/sda", username="dave",
                            hostname="pc", mode=MODE_SHRINK,
                            free_start_mib=1024, free_end_mib=60 * GiB,
                            first_index=2)
        self.assertIn("shrink mode with no partition chosen to shrink",
                      cfg.validate(disk()))


class StepOrderTest(unittest.TestCase):
    """The property this whole feature stands or falls on."""

    def _titles(self, fstype):
        part = windows(size_gib=200, fstype=fstype)
        return [s.title for s in build_shrink_steps(plan_shrink(part, 40 * GiB))]

    def test_the_filesystem_shrinks_before_the_partition_does(self):
        for fstype in ("ntfs", "ext4"):
            titles = self._titles(fstype)
            fs = next(i for i, t in enumerate(titles)
                      if t.startswith("Shrink the filesystem"))
            pt = next(i for i, t in enumerate(titles)
                      if t.startswith("Shrink partition"))
            self.assertLess(fs, pt,
                            f"{fstype}: the partition would be resized before "
                            f"the filesystem inside it")

    def test_nothing_is_written_before_the_checks_have_run(self):
        for fstype in ("ntfs", "ext4"):
            part = windows(size_gib=200, fstype=fstype)
            steps = build_shrink_steps(plan_shrink(part, 40 * GiB))
            first_write = next(i for i, s in enumerate(steps) if s.destructive)
            before = [s.title for s in steps[:first_write]]
            self.assertTrue(any("not mounted" in t for t in before), before)
            self.assertTrue(any(t.startswith("Check the filesystem")
                                for t in before), before)
            self.assertTrue(any("Rehearse" in t for t in before), before)

    def test_the_rehearsal_writes_nothing(self):
        part = windows(size_gib=200)
        steps = build_shrink_steps(plan_shrink(part, 40 * GiB))
        rehearsal = next(s for s in steps if "Rehearse" in s.title)
        self.assertFalse(rehearsal.destructive)
        self.assertIn("--no-action", rehearsal.argv)

    def test_the_result_is_checked_afterwards_too(self):
        for fstype in ("ntfs", "ext4"):
            steps = build_shrink_steps(
                plan_shrink(windows(size_gib=200, fstype=fstype), 40 * GiB))
            self.assertTrue(steps[-1].title.startswith("Verify"), steps[-1])
            self.assertFalse(steps[-1].destructive)

    def test_the_filesystem_is_given_less_room_than_the_partition_has(self):
        # A cushion, so the partition boundary is never exactly on the end of
        # the filesystem. The resizers round in clusters and blocks; this way
        # we never have to be right about which direction.
        part = windows(size_gib=200)
        sp = plan_shrink(part, 40 * GiB)
        steps = build_shrink_steps(sp)
        shrink_fs = next(s for s in steps
                         if s.title.startswith("Shrink the filesystem"))
        size_bytes = int(shrink_fs.argv[shrink_fs.argv.index("--size") + 1])
        self.assertLess(size_bytes, sp.new_size_mib * 1024 * 1024)

    def test_the_partition_is_resized_on_the_disk_not_on_itself(self):
        # sfdisk takes the DISK and a partition number. Handing it the
        # partition node instead is a command that either fails or does
        # something surprising to a disk-shaped view of a partition.
        sp = plan_shrink(windows(size_gib=200), 40 * GiB)
        step = next(s for s in build_shrink_steps(sp)
                    if s.title.startswith("Shrink partition"))
        self.assertEqual(step.argv, ["sfdisk", "--force", "-N", "1",
                                     "/dev/sda"])

    def test_the_partition_resize_only_gives_a_size_never_a_start(self):
        # This is the safety property of using sfdisk -N: an entry given only
        # a size keeps the start sector it already had, so the partition
        # cannot have moved onto anything, whatever else went wrong. A start=
        # in this input would silently make that untrue.
        sp = plan_shrink(windows(size_gib=200), 40 * GiB)
        step = next(s for s in build_shrink_steps(sp)
                    if s.title.startswith("Shrink partition"))
        self.assertEqual(step.stdin_text,
                         f"size={sp.new_size_mib}MiB\n")
        self.assertNotIn("start", step.stdin_text)

    def test_parted_is_not_used_to_shrink_a_partition(self):
        # `parted -s ... resizepart` answers its own "are you sure?" with NO
        # and exits 1. It looks right and cannot work; the loopback smoke
        # found that by running it. Keep it out.
        sp = plan_shrink(windows(size_gib=200), 40 * GiB)
        for step in build_shrink_steps(sp):
            if step.argv and step.argv[0] == "parted":
                self.assertNotIn("resizepart", step.argv)


class ThePlanAsAWholeTest(unittest.TestCase):
    def test_a_shrink_install_writes_nowhere_but_the_space_it_freed(self):
        part = windows(size_gib=200)
        cfg = shrink_cfg(part, 40 * GiB)
        plan = build_plan(cfg, 200 * GiB, configure_target=False)
        low = cfg.shrink.freed.start_mib
        for p in plan.partitions:
            self.assertGreaterEqual(p.start_mib, low)
            self.assertLessEqual(p.end_mib, cfg.free_end_mib)

    def test_a_shrink_install_never_writes_a_new_partition_table(self):
        # mklabel is the one command that turns this into "install over
        # Windows". It must not appear anywhere in the plan.
        cfg = shrink_cfg(windows(size_gib=200), 40 * GiB)
        plan = build_plan(cfg, 200 * GiB)
        for step in plan.steps:
            if step.argv:
                self.assertNotIn("mklabel", step.argv, step.title)

    def test_the_neighbours_partition_number_is_not_reused(self):
        cfg = shrink_cfg(windows(size_gib=200), 40 * GiB)
        plan = build_plan(cfg, 200 * GiB, configure_target=False)
        self.assertNotIn(1, [p.index for p in plan.partitions])

    def test_every_shrink_step_comes_before_every_install_step(self):
        cfg = shrink_cfg(windows(size_gib=200), 40 * GiB)
        plan = build_plan(cfg, 200 * GiB, configure_target=False)
        titles = [s.title for s in plan.steps]
        last_shrink = max(i for i, t in enumerate(titles)
                          if "Shrink" in t or "Verify the shrunk" in t)
        first_create = min(i for i, t in enumerate(titles)
                           if t.startswith("Create /boot"))
        self.assertLess(last_shrink, first_create)

    def test_the_whole_disk_plan_has_no_shrink_steps(self):
        cfg = InstallConfig(target_disk="/dev/sda", username="dave",
                            hostname="pc", mode=MODE_WHOLE_DISK)
        plan = build_plan(cfg, 200 * GiB, configure_target=False)
        self.assertFalse([s for s in plan.steps if "hrink" in s.title])


class ModeOfferingTest(unittest.TestCase):
    def test_shrink_is_not_offered_without_a_measurement(self):
        # No used-space map means we do not know how full anything is. The
        # mode is not merely unavailable, it is unanswerable.
        d, parts = disk(), [windows(size_gib=199, start=1)]
        self.assertNotIn(MODE_SHRINK, available_modes(d, parts))
        self.assertIn(MODE_SHRINK,
                      available_modes(d, parts, {"/dev/sda1": 40 * GiB}))

    def test_least_destructive_first(self):
        # A 200 GiB disk with a 100 GiB Windows at the front: there is
        # already free space, so alongside must be offered ahead of shrink,
        # and whole-disk last.
        d = disk(200)
        parts = [windows(size_gib=100, start=1)]
        modes = available_modes(d, parts, {"/dev/sda1": 40 * GiB})
        self.assertEqual(modes, [MODE_ALONGSIDE, MODE_SHRINK,
                                 MODE_WHOLE_DISK])

    def test_a_full_disk_with_a_shrinkable_windows_offers_shrink(self):
        d = disk(200)
        parts = [windows(size_gib=199, start=1)]
        modes = available_modes(d, parts, {"/dev/sda1": 40 * GiB})
        self.assertEqual(modes, [MODE_SHRINK, MODE_WHOLE_DISK])

    def test_a_table_with_no_room_left_offers_nothing_but_whole_disk(self):
        # msdos holds four primaries and the layout needs three.
        d = disk(400)
        parts = [windows(size_gib=90, start=1 + 90 * GiB * i, index=i + 1)
                 for i in range(2)]
        used = {p.path: 10 * GiB for p in parts}
        self.assertEqual(shrink_candidates(d, parts, used), [])
        self.assertNotIn(MODE_SHRINK, available_modes(d, parts, used))

    def test_an_msdos_table_can_only_ever_offer_one_candidate(self):
        # Worth stating because it is surprising: the layout needs three
        # primaries and msdos has four, so the moment a disk has two
        # partitions there is no room to install beside them at all —
        # whatever their filesystems are and however empty they are. This is
        # the ceiling that makes "which one should we shrink?" a question
        # with at most one answer today. shrink_candidates still sorts by
        # what each could give, which costs nothing and is the right answer
        # the day this installer learns GPT.
        d = disk(400)
        small = PartitionInfo("/dev/sda1", 1, 1, 50 * GiB, "ntfs", "A")
        big = PartitionInfo("/dev/sda2", 2, 1 + 50 * GiB, 300 * GiB,
                            "ext4", "B")
        used = {"/dev/sda1": 10 * GiB, "/dev/sda2": 20 * GiB}
        self.assertEqual(shrink_candidates(d, [small, big], used), [])
        self.assertEqual([p.path for p in shrink_candidates(d, [big], used)],
                         ["/dev/sda2"])

    def test_a_partition_we_could_not_measure_is_skipped_not_assumed_empty(self):
        d = disk(200)
        parts = [windows(size_gib=199, start=1)]
        self.assertEqual(shrink_candidates(d, parts, {}), [])


class ProbeParsingTest(unittest.TestCase):
    """Parsed from what the real tools really print."""

    NTFS = """ntfsresize v2022.10.3 (libntfs-3g)
Device name        : /dev/sda1
NTFS volume version: 3.1
Cluster size       : 4096 bytes
Current volume size: 107374178304 bytes (107375 MB)
Current device size: 107374182400 bytes (107375 MB)
Checking filesystem consistency ...
100.00 percent completed
Accounting clusters ...
Space in use       : 33487 MB (31.2%)
Collecting resizing constraints ...
You might resize at 33486802944 bytes or 33487 MB (freeing 73888 MB).
Please make a test run using both the -n and -s options before real resizing!
"""

    DUMPE2FS = """dumpe2fs 1.47.0 (5-Feb-2023)
Filesystem volume name:   <none>
Filesystem UUID:          8f1f3a2e-1111-4444-9999-aaaaaaaaaaaa
Block count:              26214400
Reserved block count:     1310720
Free blocks:              24234353
Free inodes:              6529845
Block size:               4096
"""

    def test_ntfs_uses_the_tools_own_minimum_not_its_space_in_use(self):
        # "Space in use: 33487 MB" is 10^6 MB; the byte figure is the one
        # that is both exact and achievable.
        self.assertEqual(parse_ntfs_used_mib(self.NTFS),
                         33486802944 // (1024 * 1024))

    def test_ext_used_is_allocated_blocks_times_block_size(self):
        self.assertEqual(parse_ext_used_mib(self.DUMPE2FS),
                         (26214400 - 24234353) * 4096 // (1024 * 1024))

    def test_output_without_the_numbers_returns_none_not_zero(self):
        # Zero would mean "empty, shrink it as far as you like".
        for text in ("", "ntfsresize: command not found",
                     "dumpe2fs: Bad magic number in super-block"):
            self.assertIsNone(parse_ntfs_used_mib(text), text)
            self.assertIsNone(parse_ext_used_mib(text), text)

    def test_a_truncated_superblock_dump_returns_none(self):
        self.assertIsNone(parse_ext_used_mib(
            "Block count:              26214400\nBlock size:  4096\n"))


if __name__ == "__main__":
    unittest.main()
