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
