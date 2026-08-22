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
#: Shrink an existing filesystem to make the gap, then install into it.
MODE_SHRINK = "shrink"
INSTALL_MODES = (MODE_WHOLE_DISK, MODE_ALONGSIDE, MODE_SHRINK)

#: Filesystems this installer is willing to shrink, and the tool that does it.
#:
#: The list is short on purpose. XFS cannot shrink at all; btrfs can, but its
#: failure modes on an old disk are not ones we can talk a user through; FAT
#: and unknown filesystems are somebody's data with no verifier to check the
#: result. Everything not named here is refused, loudly, rather than attempted
#: hopefully — an installer that guesses about shrinking is an installer that
#: eats a Windows.
SHRINK_TOOLS = {
    "ntfs": "ntfsresize",
    "ntfs3": "ntfsresize",
    "ext2": "resize2fs",
    "ext3": "resize2fs",
    "ext4": "resize2fs",
}

#: Never shrink a filesystem to closer than this to what it is using.
#: Windows in particular stops working — updates fail, then it stops booting —
#: long before it is literally full, and the user who agreed to "make room for
#: Castalia" did not agree to that.
SHRINK_KEEP_FREE_MIB = 4 * 1024
#: ...or this fraction of what it is using, whichever leaves more room. A
#: 400 GiB Windows with 300 GiB in it needs a lot more than 4 GiB of slack.
SHRINK_KEEP_FREE_FRACTION = 0.15


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

    @property
    def shrink_tool(self) -> str:
        """The resizer for this filesystem, or "" if we refuse to shrink it."""
        return SHRINK_TOOLS.get((self.fstype or "").lower(), "")

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
                    partitions: list[PartitionInfo],
                    used: "dict[str, int] | None" = None) -> list[str]:
    """Which install modes this disk can actually offer, best first (§14.3).

    Best-first is least-destructive-first, and the order is the whole point:
    whatever the interface puts at the top is what most people will pick.

    "Alongside" comes first because it touches nothing at all. "Shrink" is
    offered only when *used* says how full the candidate partitions are —
    without that measurement the mode is not merely unavailable, it is
    unanswerable, and the honest response is to not offer it. "Whole disk"
    is last because it is the one that erases the machine.
    """
    modes = []
    if largest_free_region(disk, partitions) is not None:
        modes.append(MODE_ALONGSIDE)
    if used and shrink_candidates(disk, partitions, used):
        modes.append(MODE_SHRINK)
    if disk.is_installable():
        modes.append(MODE_WHOLE_DISK)
    return modes


@dataclass(frozen=True)
class ShrinkPlan:
    """A decision to take *freed* MiB off the end of an existing partition.

    Immutable and computed by :func:`plan_shrink`, which is where every rule
    about what may be shrunk lives. Nothing here runs anything; the steps that
    do are built in :mod:`plan` from these numbers.
    """

    partition: PartitionInfo
    used_mib: int
    new_size_mib: int

    @property
    def freed_mib(self) -> int:
        return self.partition.size_mib - self.new_size_mib

    @property
    def freed(self) -> Region:
        """The gap the shrink opens up — always immediately after the
        partition's new end, because a filesystem is shrunk from its tail."""
        start = self.partition.start_mib + self.new_size_mib
        return Region(start, self.partition.end_mib)

    @property
    def new_end_mib(self) -> int:
        return self.partition.start_mib + self.new_size_mib

    def describe(self) -> str:
        return (f"{self.partition.path}: {self.partition.size_mib} MiB → "
                f"{self.new_size_mib} MiB "
                f"(en uso {self.used_mib} MiB, libera {self.freed_mib} MiB)")


def min_size_after_shrink(used_mib: int) -> int:
    """The smallest we will let a filesystem holding *used_mib* become.

    Used space plus slack, where slack is 4 GiB or 15% of what is in there,
    whichever is more. This is not the filesystem's own minimum — those tools
    will happily pack a Windows down to almost nothing and hand back a volume
    that cannot install an update. It is the smallest size at which the OS we
    are shrinking is still a working OS.
    """
    slack = max(SHRINK_KEEP_FREE_MIB, int(used_mib * SHRINK_KEEP_FREE_FRACTION))
    return used_mib + slack


def max_freeable_mib(part: PartitionInfo, used_mib: int) -> int:
    """How much *part* could give up, at most. 0 when it must not be touched.

    This is the number a slider in the installer is bounded by, so it has to
    be honest in both directions: never more than is safe, and never less
    than is actually available, or the mode looks unavailable on disks where
    it would have worked.
    """
    if not part.shrink_tool:
        return 0
    if used_mib < 0 or used_mib > part.size_mib:
        return 0
    floor = min_size_after_shrink(used_mib)
    # Round the floor up to the alignment grid: a partition boundary that is
    # not aligned is a slow disk at best and an unbootable one at worst.
    floor = -(-floor // ALIGN_MIB) * ALIGN_MIB
    return max(0, part.size_mib - floor)


def plan_shrink(part: PartitionInfo, used_mib: int,
                free_mib: int | None = None) -> ShrinkPlan:
    """Decide how far to shrink *part*, or explain why we will not.

    *free_mib* is how much space the user asked for; ``None`` means "as much
    as is safe". Every refusal raises :class:`ValueError` with a message meant
    to be shown to a person, because "the installer declined and did not say
    why" is how someone reaches for a partition editor they do not know how to
    use.
    """
    if not part.shrink_tool:
        raise ValueError(
            f"{part.path} is {part.fstype or 'de tipo desconocido'}, which "
            f"this installer will not resize — only "
            f"{', '.join(sorted(set(SHRINK_TOOLS)))} are supported")
    if used_mib < 0 or used_mib > part.size_mib:
        raise ValueError(
            f"{part.path}: used space ({used_mib} MiB) is not a figure this "
            f"partition ({part.size_mib} MiB) can have — refusing to plan a "
            f"shrink on a measurement that cannot be right")
    available = max_freeable_mib(part, used_mib)
    if available < MIN_ALONGSIDE_MIB:
        raise ValueError(
            f"{part.path} can only spare {available} MiB once "
            f"{min_size_after_shrink(used_mib)} MiB is left for what is on "
            f"it; an install needs at least {MIN_ALONGSIDE_MIB} MiB")
    want = available if free_mib is None else free_mib
    if want < MIN_ALONGSIDE_MIB:
        raise ValueError(
            f"asked to free {want} MiB, but an install needs at least "
            f"{MIN_ALONGSIDE_MIB} MiB")
    if want > available:
        raise ValueError(
            f"asked to free {want} MiB from {part.path}, but only "
            f"{available} MiB can be freed without leaving too little room "
            f"for what is already on it")
    # Align the boundary downwards, which frees a whole megabyte more rather
    # than a fraction less — the safe rounding direction for the shrunk side.
    new_size = (part.size_mib - want) // ALIGN_MIB * ALIGN_MIB
    return ShrinkPlan(partition=part, used_mib=used_mib,
                      new_size_mib=new_size)


def shrink_candidates(disk: "DiskInfo", partitions: list[PartitionInfo],
                      used: dict[str, int]) -> list[PartitionInfo]:
    """Existing partitions that could be shrunk to make room, biggest gain
    first. *used* maps a partition path to its used space in MiB.

    A partition with no entry in *used* is skipped rather than assumed empty.
    The one thing worse than declining to shrink something is shrinking it on
    a guess about how full it is.
    """
    if len(partitions) + 3 > MAX_MSDOS_PRIMARIES:
        return []
    scored = [(max_freeable_mib(p, used[p.path]), p)
              for p in partitions if p.path in used]
    return [p for gain, p in sorted(scored, key=lambda t: t[0], reverse=True)
            if gain >= MIN_ALONGSIDE_MIB]


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
    #: MODE_SHRINK is MODE_ALONGSIDE with one extra act before it: the
    #: existing filesystem in ``shrink`` gives up its tail, and the gap that
    #: opens is the region the layout then goes into.
    mode: str = MODE_WHOLE_DISK
    free_start_mib: int = 0
    free_end_mib: int = 0
    first_index: int = 1
    shrink: "ShrinkPlan | None" = None

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
        if self.mode == MODE_SHRINK:
            if self.shrink is None:
                errors.append(
                    "shrink mode with no partition chosen to shrink")
            elif (self.free_start_mib, self.free_end_mib) != (
                    self.shrink.freed.start_mib, self.shrink.freed.end_mib):
                # The two must agree or the layout goes somewhere the shrink
                # did not clear — which is inside the filesystem we just
                # resized, on top of its data.
                errors.append(
                    f"the install region ({self.free_start_mib}"
                    f"–{self.free_end_mib} MiB) is not the space the shrink "
                    f"frees ({self.shrink.freed.start_mib}"
                    f"–{self.shrink.freed.end_mib} MiB)")
            elif self.shrink.partition.index >= self.first_index:
                errors.append(
                    f"the shrunk partition ({self.shrink.partition.index}) "
                    f"would be renumbered by a layout starting at "
                    f"{self.first_index}")
        if self.mode in (MODE_ALONGSIDE, MODE_SHRINK):
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
