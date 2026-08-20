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
# A free region has to hold /boot + swap + a usable root before "install
# alongside" is worth offering. Offering it and then failing halfway is worse
# than not offering it (§14.5 #2: always leave a bootable path).
MIN_ALONGSIDE_MIB = ALIGN_MIB + BOOT_MIB + 512 + MIN_ROOT_MIB
# An msdos label holds four primary partitions and we need three. A disk that
# already has two is not one we can install alongside on without extended
# partitions, which is a trap on old BIOSes and is deliberately not attempted.
MAX_MSDOS_PRIMARIES = 4


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


#: Guided install writes a fresh partition table over everything.
MODE_WHOLE_DISK = "whole-disk"
#: Install into unallocated space, leaving every existing partition alone.
MODE_ALONGSIDE = "alongside"
INSTALL_MODES = (MODE_WHOLE_DISK, MODE_ALONGSIDE)


@dataclass(frozen=True)
class Region:
    """A span of a disk, in MiB. Used for both free gaps and whole extents."""

    start_mib: int
    end_mib: int

    @property
    def size_mib(self) -> int:
        return max(0, self.end_mib - self.start_mib)


@dataclass(frozen=True)
class PartitionInfo:
    """An existing partition found on a candidate disk (§14.3).

    ``kind`` is what we are willing to say about it in the interface, which is
    deliberately coarser than the filesystem: the installer needs to know "is
    there a Windows here that I must not touch", not which NTFS version it is.
    """

    path: str
    index: int
    start_mib: int
    size_mib: int
    fstype: str = ""
    label: str = ""

    @property
    def end_mib(self) -> int:
        return self.start_mib + self.size_mib

    @property
    def kind(self) -> str:
        fs = (self.fstype or "").lower()
        if fs in ("ntfs", "ntfs3"):
            return "windows"
        if fs == "vfat" and "efi" in (self.label or "").lower():
            return "efi"
        if fs in ("ext2", "ext3", "ext4", "btrfs", "xfs"):
            return "linux"
        if fs in ("swap", "linux-swap"):
            return "swap"
        return "data" if fs else "unknown"

    def describe(self) -> str:
        names = {"windows": "Windows", "efi": "arranque EFI",
                 "linux": "Linux", "swap": "intercambio",
                 "data": "datos", "unknown": "sin identificar"}
        label = f" «{self.label}»" if self.label else ""
        return f"{self.path}{label}: {names[self.kind]}, {self.size_mib} MiB"


def free_regions(disk_size_mib: int,
                 partitions: list[PartitionInfo]) -> list[Region]:
    """Unallocated spans of a disk, largest first.

    Overlapping or out-of-order partition entries are tolerated rather than
    trusted: a partition table is read off someone's real machine, and one
    that does not make sense must produce no free space rather than a
    plausible-looking gap that is actually somebody's data.
    """
    cursor = ALIGN_MIB
    gaps: list[Region] = []
    for part in sorted(partitions, key=lambda p: p.start_mib):
        if part.start_mib > cursor:
            gaps.append(Region(cursor, part.start_mib))
        cursor = max(cursor, part.end_mib)
    if disk_size_mib > cursor:
        gaps.append(Region(cursor, disk_size_mib))
    return sorted((g for g in gaps if g.size_mib > 0),
                  key=lambda g: g.size_mib, reverse=True)


def available_modes(disk: "DiskInfo",
                    partitions: list[PartitionInfo]) -> list[str]:
    """Which install modes this disk can actually offer, best first (§14.3).

    "Alongside" is offered only when it can be carried out without touching a
    single existing partition — that is the whole promise of the mode, and a
    version of it that shrinks something is a different, more dangerous
    feature that this installer does not have yet.
    """
    modes = []
    if largest_free_region(disk, partitions) is not None:
        modes.append(MODE_ALONGSIDE)
    if disk.is_installable():
        modes.append(MODE_WHOLE_DISK)
    return modes


def largest_free_region(disk: "DiskInfo",
                        partitions: list[PartitionInfo]) -> "Region | None":
    """The free span an alongside install would use, or None if there is none.

    None is the answer whenever anything makes the mode unsafe or impossible:
    no gap big enough, or no room left in the partition table for the three
    partitions the layout needs.
    """
    if len(partitions) + 3 > MAX_MSDOS_PRIMARIES:
        return None
    for gap in free_regions(disk.size_mib, partitions):
        if gap.size_mib >= MIN_ALONGSIDE_MIB:
            return gap
    return None


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
    #: MODE_WHOLE_DISK writes a fresh table over everything. MODE_ALONGSIDE
    #: adds partitions inside free space and leaves every existing one alone
    #: (§14.3); free_start_mib/free_end_mib bound the span it may use and
    #: first_index is where the existing table's numbering continues.
    mode: str = MODE_WHOLE_DISK
    free_start_mib: int = 0
    free_end_mib: int = 0
    first_index: int = 1

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
        if self.mode not in INSTALL_MODES:
            errors.append(f"unknown install mode: {self.mode}")
        if self.mode == MODE_ALONGSIDE:
            span = self.free_end_mib - self.free_start_mib
            if span < MIN_ALONGSIDE_MIB:
                errors.append(
                    f"free space too small for an alongside install: "
                    f"{span} MiB < {MIN_ALONGSIDE_MIB} MiB")
            if self.free_start_mib < ALIGN_MIB:
                errors.append("free region starts before the alignment floor")
            if self.free_end_mib > disk.size_mib:
                errors.append(
                    f"free region ends past the disk "
                    f"({self.free_end_mib} > {disk.size_mib} MiB)")
            if self.first_index + 2 > MAX_MSDOS_PRIMARIES:
                errors.append(
                    f"no room in the partition table: an alongside install "
                    f"needs three primaries from index {self.first_index}")
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
