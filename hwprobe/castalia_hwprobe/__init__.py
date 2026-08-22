"""Castalia hwprobe (Bible §6.15, §18 Phase 1 "hwprobe v0").

Reads what is in the machine out of sysfs and procfs, matches it against a
shipped quirks table, and writes a report the rest of the system can act on:
which Xorg driver to ask for, whether suspend is safe to offer, and what was
found but not recognised.

Everything here is a pure function of a filesystem tree, so the tests probe
captured sysfs layouts from real machines rather than the build host — which
is the whole difficulty with hardware code, and the reason this is Python in
a package rather than shell in a boot script.
"""

from .model import (
    Device,
    Machine,
    Quirk,
    SUSPEND_SAFE,
    SUSPEND_UNKNOWN,
    SUSPEND_UNSAFE,
)
from .quirks import DEFAULT_QUIRKS, decide, load_quirks
from .probe import probe_machine, read_report, write_report

__all__ = [
    "DEFAULT_QUIRKS",
    "Device",
    "Machine",
    "Quirk",
    "SUSPEND_SAFE",
    "SUSPEND_UNKNOWN",
    "SUSPEND_UNSAFE",
    "decide",
    "load_quirks",
    "probe_machine",
    "read_report",
    "write_report",
]
