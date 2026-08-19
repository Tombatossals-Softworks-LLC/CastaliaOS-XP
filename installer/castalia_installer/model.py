"""Installer data model (Bible §12, §14).

Pure, dependency-free description of *what* to install: the target disk, the
partition layout, and the user/system settings. No side effects live here —
these objects are consumed by :mod:`plan` to produce an ordered, inspectable
list of steps, which keeps the whole backend unit-testable without ever
touching a real disk.
"""

from __future__ import annotations

from dataclasses import dataclass, field

MiB = 1024 * 1024
GiB = 1024 * MiB

# Old-BIOS safety (§6.2): keep /boot wholly within the first 128 GiB so a
# vintage BIOS can always read the kernel.
BOOT_MIB = 1024
FIRST_128_GIB_MIB = 128 * 1024
# Guided install refuses to touch a disk smaller than this (no room for a
# usable system + swap + boot).
MIN_DISK_MIB = 8 * 1024
MIN_ROOT_MIB = 4 * 1024
ALIGN_MIB = 1  # 1 MiB partition alignment (universally safe)


def default_swap_mib(ram_mib: int) -> int:
    """Swap sized to RAM, capped at 8 GiB, hibernate off by default (§4.6).

    Tiny-RAM floor machines still get a little swap headroom; large-RAM
    machines are not asked to donate huge amounts of disk.
    """
    if ram_mib <= 0:
        return 512
    return max(512, min(ram_mib, 8 * 1024))


def partition_path(disk: str, index: int) -> str:
    """Kernel name of the *index*-th partition of *disk*.

    ``/dev/sda`` -> ``/dev/sda1`` but ``/dev/nvme0n1`` -> ``/dev/nvme0n1p1``
    and ``/dev/mmcblk0`` -> ``/dev/mmcblk0p1`` (a trailing digit takes a ``p``
    separator).
    """
    if disk and disk[-1].isdigit():
        return f"{disk}p{index}"
    return f"{disk}{index}"


@dataclass(frozen=True)
class DiskInfo:
    """Facts about a candidate target disk, as probed on the live system."""

    path: str
    size_mib: int
    model: str = ""
    removable: bool = False
    has_existing_os: bool = False

    def is_installable(self) -> bool:
        return self.size_mib >= MIN_DISK_MIB


@dataclass(frozen=True)
class Partition:
    """One partition in the computed layout (all offsets in MiB)."""

    index: int
    role: str  # "boot" | "swap" | "root"
    start_mib: int
    end_mib: int
    fstype: str  # "ext4" | "swap"
    label: str

    @property
    def size_mib(self) -> int:
        return self.end_mib - self.start_mib


@dataclass
class InstallConfig:
    """Everything the user chose. Validated by :meth:`validate`."""

    target_disk: str
    hostname: str = "castalia"
    username: str = "usuario"
    full_name: str = "Usuario de Castalia"
    theme: str = "classic"
    locale: str = "es_ES.UTF-8"
    keymap: str = "es"
    timezone: str = "Europe/Madrid"
    filesystem: str = "ext4"
    autologin: bool = False
    ram_mib: int = 2048
    # Where the running live root is (what we copy onto the target), and the
    # temporary mount point for the new system.
    source_root: str = "/run/live/rootfs/filesystem.squashfs"
    mount_root: str = "/target"
    swap_mib: int = field(default=0)  # 0 => derive from ram_mib

    def resolved_swap_mib(self) -> int:
        return self.swap_mib if self.swap_mib > 0 else default_swap_mib(
            self.ram_mib
        )

    def validate(self, disk: DiskInfo) -> list[str]:
        """Return a list of human-readable problems ([] means OK)."""
        errors: list[str] = []
        if disk.path != self.target_disk:
            errors.append(
                f"disk mismatch: config targets {self.target_disk} "
                f"but probe is for {disk.path}"
            )
        if not disk.is_installable():
            errors.append(
                f"disk too small: {disk.size_mib} MiB < {MIN_DISK_MIB} MiB "
                "minimum"
            )
        needed = ALIGN_MIB + BOOT_MIB + self.resolved_swap_mib() + MIN_ROOT_MIB
        if disk.size_mib < needed:
            errors.append(
                f"disk cannot fit layout: needs >= {needed} MiB, "
                f"has {disk.size_mib} MiB"
            )
        if self.filesystem not in ("ext4",):
            errors.append(f"unsupported filesystem: {self.filesystem}")
        if not self.username.isascii() or not self.username.islower() \
                or not self.username.isalnum():
            errors.append(
                "username must be lowercase alphanumeric ASCII"
            )
        if not self.hostname or not all(
            c.isalnum() or c == "-" for c in self.hostname
        ):
            errors.append("hostname must be alphanumeric/hyphen")
        return errors
