"""Probe the machine for installable disks and what is already on them (§14).

The parsers are pure (text in, objects out) so they are unit-tested against
real lsblk output from real machines; the ``probe_*`` functions just run lsblk
and hand the result over. §14.3 makes detecting an existing OS a first-class
requirement — an installer that cannot see the Windows it is about to sit next
to cannot honestly offer to sit next to it.
"""

from __future__ import annotations

import subprocess

from .model import DiskInfo, PartitionInfo


def parse_lsblk(text: str) -> list[DiskInfo]:
    """Parse ``lsblk -dnb -o NAME,SIZE,TYPE,MODEL,RM`` output."""
    disks: list[DiskInfo] = []
    for line in text.splitlines():
        parts = line.split()
        if len(parts) < 3 or parts[2] != "disk":
            continue
        name, size_bytes = parts[0], parts[1]
        # RM is the last column we requested (always 0/1); MODEL may be empty,
        # so whitespace-splitting can't tell them apart unless we peel RM off
        # the end first.
        rest = parts[3:]
        removable = False
        if rest and rest[-1] in ("0", "1"):
            removable = rest[-1] == "1"
            rest = rest[:-1]
        model = " ".join(rest) if rest else "disco"
        try:
            size_mib = int(size_bytes) // (1024 * 1024)
        except ValueError:
            continue
        disks.append(DiskInfo(path=f"/dev/{name}", size_mib=size_mib,
                              model=model, removable=removable))
    return disks


def probe_disks() -> list[DiskInfo]:
    """Return the machine's disks (empty list if lsblk is unavailable)."""
    try:
        out = subprocess.run(
            ["lsblk", "-dnb", "-o", "NAME,SIZE,TYPE,MODEL,RM"],
            capture_output=True, text=True, check=True,
        ).stdout
    except (OSError, subprocess.CalledProcessError):
        return []
    return parse_lsblk(out)


def parse_lsblk_partitions(text: str, disk: str) -> list[PartitionInfo]:
    """Parse ``lsblk -bn -o NAME,START,SIZE,TYPE,FSTYPE,LABEL`` for *disk*.

    Only direct partitions of *disk* are returned: an LVM volume or a LUKS
    mapping sitting on one is somebody's storage stack, not a span this
    installer may plan around, and silently treating it as one is how an
    installer eats data.

    START is in 512-byte sectors (lsblk reports it that way even under -b),
    SIZE is in bytes.
    """
    want = disk.rsplit("/", 1)[-1]
    parts: list[PartitionInfo] = []
    for line in text.splitlines():
        fields = line.split(None, 5)
        if len(fields) < 4 or fields[3] != "part":
            continue
        name = fields[0].lstrip("`|-\u2514\u251c\u2500 ")
        if not name.startswith(want) or name == want:
            continue
        # Only a direct child: sda1 yes, but not a name that is really a
        # deeper node lsblk happened to indent under the same disk.
        suffix = name[len(want):].lstrip("p")
        if not suffix.isdigit():
            continue
        try:
            start_mib = int(fields[1]) * 512 // (1024 * 1024)
            size_mib = int(fields[2]) // (1024 * 1024)
        except ValueError:
            continue
        rest = fields[4:]
        fstype = rest[0] if rest else ""
        label = rest[1].strip() if len(rest) > 1 else ""
        parts.append(PartitionInfo(path=f"/dev/{name}", index=int(suffix),
                                   start_mib=start_mib, size_mib=size_mib,
                                   fstype=fstype, label=label))
    return sorted(parts, key=lambda p: p.start_mib)


def probe_partitions(disk: str) -> list[PartitionInfo]:
    """Existing partitions on *disk* (empty list if lsblk is unavailable)."""
    try:
        out = subprocess.run(
            ["lsblk", "-bn", "-o", "NAME,START,SIZE,TYPE,FSTYPE,LABEL", disk],
            capture_output=True, text=True, check=True,
        ).stdout
    except (OSError, subprocess.CalledProcessError):
        return []
    return parse_lsblk_partitions(out, disk)


def parse_ntfs_used_mib(text: str) -> int | None:
    """Used space from ``ntfsresize --info``, in MiB, or None.

    The line we want is the tool's own conclusion:

        You might resize at 33486802944 bytes or 33487 MB (freeing 73888 MB).

    That figure — not the "Space in use" line above it — is what NTFS can
    actually be packed down to, because it accounts for where the MFT and the
    immovable metadata happen to sit rather than only for how many bytes of
    file data exist. Taking the smaller number would let us plan a shrink the
    resizer then refuses, halfway through an install.

    Note the tool's MB are 10^6 and ours are 2^20, so the byte count is the
    only field worth reading.
    """
    for line in text.splitlines():
        line = line.strip()
        if line.startswith("You might resize at"):
            for token in line.split():
                if token.isdigit():
                    return int(token) // (1024 * 1024)
    return None


def parse_ext_used_mib(text: str) -> int | None:
    """Used space from ``dumpe2fs -h``, in MiB, or None.

    (Block count - Free blocks) x Block size. ``resize2fs -P`` would give the
    tool's own minimum instead, but it needs a recently-checked filesystem and
    prints nothing useful when it does not have one; the superblock is always
    readable and always tells the truth about what is allocated.
    """
    fields: dict[str, int] = {}
    for line in text.splitlines():
        key, _, value = line.partition(":")
        key = key.strip()
        if key in ("Block count", "Free blocks", "Block size"):
            value = value.strip()
            if value.isdigit():
                fields[key] = int(value)
    if len(fields) != 3:
        return None
    used_blocks = fields["Block count"] - fields["Free blocks"]
    if used_blocks < 0:
        return None
    return used_blocks * fields["Block size"] // (1024 * 1024)


def probe_used_mib(part: PartitionInfo) -> int | None:
    """How much of *part* is in use, in MiB, or None if we cannot tell.

    None is a real answer and callers must treat it as one: it means the
    installer does not know how full this filesystem is, and a shrink planned
    without that number is a shrink planned on a hope.
    """
    tool = part.shrink_tool
    if tool == "ntfsresize":
        argv = ["ntfsresize", "--info", "--force", part.path]
        parse = parse_ntfs_used_mib
    elif tool == "resize2fs":
        argv = ["dumpe2fs", "-h", part.path]
        parse = parse_ext_used_mib
    else:
        return None
    try:
        proc = subprocess.run(argv, capture_output=True, text=True)
    except OSError:
        return None
    # ntfsresize exits non-zero on a dirty volume but still prints what it
    # learned; the shrink itself will refuse later, and refusing here as well
    # would hide the reason behind a blank screen.
    return parse(proc.stdout + "\n" + proc.stderr)


def probe_used(partitions: list[PartitionInfo]) -> dict[str, int]:
    """Used space for every partition we can measure (path -> MiB).

    Partitions we cannot measure are absent rather than zero, which is what
    :func:`~.model.shrink_candidates` needs in order to skip them.
    """
    out: dict[str, int] = {}
    for part in partitions:
        used = probe_used_mib(part)
        if used is not None:
            out[part.path] = used
    return out
