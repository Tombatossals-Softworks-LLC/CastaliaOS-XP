"""Unit tests for disk probing (Bible §14)."""

import unittest

from castalia_installer.probe import parse_lsblk

GiB = 1024 * 1024 * 1024


class ParseLsblk(unittest.TestCase):
    def test_picks_disks_skips_partitions_and_rom(self):
        text = (
            f"sda {40 * GiB} disk QEMU HARDDISK 0\n"
            f"sda1 {1 * GiB} part  0\n"
            f"sr0 {1024} rom QEMU DVD-ROM 1\n"
            f"sdb {16 * GiB} disk Kingston DataTraveler 1\n"
        )
        disks = parse_lsblk(text)
        self.assertEqual([d.path for d in disks], ["/dev/sda", "/dev/sdb"])

    def test_sizes_in_mib(self):
        disks = parse_lsblk(f"sda {40 * GiB} disk Disk 0\n")
        self.assertEqual(disks[0].size_mib, 40 * 1024)

    def test_removable_flag(self):
        disks = parse_lsblk(f"sdb {16 * GiB} disk USB Stick 1\n")
        self.assertTrue(disks[0].removable)
        self.assertEqual(disks[0].model, "USB Stick")

    def test_missing_model_defaults(self):
        disks = parse_lsblk(f"sda {40 * GiB} disk  0\n")
        self.assertEqual(disks[0].model, "disco")

    def test_empty(self):
        self.assertEqual(parse_lsblk(""), [])


if __name__ == "__main__":
    unittest.main()
