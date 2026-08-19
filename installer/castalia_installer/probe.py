"""Probe the machine for installable disks (Bible §14).

`parse_lsblk` is pure (text in, `DiskInfo` list out) so it is unit-tested
directly; `probe_disks` just runs lsblk and hands its output to the parser.
"""

from __future__ import annotations

import subprocess

from .model import DiskInfo


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
