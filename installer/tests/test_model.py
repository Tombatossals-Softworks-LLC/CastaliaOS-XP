"""Unit tests for the installer data model (Bible §14)."""

import unittest

from castalia_installer.model import (
    BOOT_MIB,
    DiskInfo,
    InstallConfig,
    default_swap_mib,
    partition_path,
)


class SwapSizing(unittest.TestCase):
    def test_capped_at_8gib(self):
        self.assertEqual(default_swap_mib(32 * 1024), 8 * 1024)

    def test_matches_small_ram(self):
        self.assertEqual(default_swap_mib(2048), 2048)

    def test_floor_for_tiny_ram(self):
        self.assertEqual(default_swap_mib(256), 512)

    def test_zero_ram_safe(self):
        self.assertEqual(default_swap_mib(0), 512)


class PartitionNaming(unittest.TestCase):
    def test_sata(self):
        self.assertEqual(partition_path("/dev/sda", 1), "/dev/sda1")

    def test_nvme_gets_p(self):
        self.assertEqual(partition_path("/dev/nvme0n1", 2), "/dev/nvme0n1p2")

    def test_mmc_gets_p(self):
        self.assertEqual(partition_path("/dev/mmcblk0", 3), "/dev/mmcblk0p3")


class ConfigValidation(unittest.TestCase):
    def _disk(self, size_mib=40 * 1024, path="/dev/sda"):
        return DiskInfo(path=path, size_mib=size_mib)

    def test_ok(self):
        cfg = InstallConfig(target_disk="/dev/sda")
        self.assertEqual(cfg.validate(self._disk()), [])

    def test_disk_mismatch(self):
        cfg = InstallConfig(target_disk="/dev/sda")
        errs = cfg.validate(self._disk(path="/dev/sdb"))
        self.assertTrue(any("mismatch" in e for e in errs))

    def test_disk_too_small(self):
        cfg = InstallConfig(target_disk="/dev/sda")
        errs = cfg.validate(self._disk(size_mib=2048))
        self.assertTrue(errs)

    def test_bad_username(self):
        cfg = InstallConfig(target_disk="/dev/sda", username="Bad User")
        errs = cfg.validate(self._disk())
        self.assertTrue(any("username" in e for e in errs))

    def test_bad_hostname(self):
        cfg = InstallConfig(target_disk="/dev/sda", hostname="host name!")
        errs = cfg.validate(self._disk())
        self.assertTrue(any("hostname" in e for e in errs))

    def test_boot_fits_128gib(self):
        # /boot is small enough to always live in the first 128 GiB.
        self.assertLessEqual(BOOT_MIB, 128 * 1024)


if __name__ == "__main__":
    unittest.main()
