"""What hwprobe knows about a machine. Pure data (Bible §6.15).

Deliberately small. hwprobe's job is not to describe hardware exhaustively —
the Hardware Center does that, live, from the same sysfs — but to record the
handful of facts that a *decision* hangs on, so that decision can be made once
at install time and re-read later without re-probing.
"""

from __future__ import annotations

from dataclasses import dataclass, field

#: PCI class prefixes we care about, and the name we file them under. The
#: full class is three bytes (class, subclass, prog-if); the first byte is
#: what says "this is a display controller" and is the only part stable
#: enough to key on.
PCI_CLASSES = {
    "03": "gpu",
    "04": "multimedia",
    "02": "net",
    "01": "storage",
    "0c": "serial",
}

#: What we are willing to say about suspend-to-RAM. §4.6 has hibernate off by
#: default; this is about S3, and the honest default is that we do not know.
SUSPEND_SAFE = "safe"
SUSPEND_UNSAFE = "unsafe"
SUSPEND_UNKNOWN = "unknown"


@dataclass(frozen=True)
class Device:
    """One PCI device, as sysfs describes it."""

    address: str          # "0000:00:02.0"
    vendor: str           # "8086", lowercase hex, no 0x
    device: str           # "0126"
    pci_class: str        # "030000"
    driver: str = ""      # kernel module bound to it, "" if none

    @property
    def ident(self) -> str:
        """``vendor:device``, the form quirks are keyed on."""
        return f"{self.vendor}:{self.device}"

    @property
    def kind(self) -> str:
        return PCI_CLASSES.get(self.pci_class[:2].lower(), "other")

    @property
    def bound(self) -> bool:
        return bool(self.driver)


@dataclass(frozen=True)
class Quirk:
    """One row of the shipped quirks table.

    *match* is either ``vendor:device`` or just ``vendor`` — the second form
    exists because a whole generation of GPUs from one vendor often shares a
    problem, and listing four hundred device ids to say so would be a table
    nobody maintains.
    """

    match: str
    note: str = ""
    xorg_driver: str = ""
    suspend: str = ""
    modules_blacklist: tuple = ()

    @property
    def is_vendor_wide(self) -> bool:
        return ":" not in self.match


@dataclass
class Machine:
    """Everything one probe found."""

    devices: list[Device] = field(default_factory=list)
    cpu_model: str = ""
    cpu_flags: tuple = ()
    ram_mib: int = 0
    #: Filled in by :func:`~.quirks.decide`, not by probing.
    xorg_driver: str = ""
    suspend: str = SUSPEND_UNKNOWN
    modules_blacklist: tuple = ()
    notes: list = field(default_factory=list)

    def of_kind(self, kind: str) -> list:
        return [d for d in self.devices if d.kind == kind]

    @property
    def gpus(self) -> list:
        return self.of_kind("gpu")

    @property
    def has_sse2(self) -> bool:
        """§18's Legacy build exists for machines where this is False."""
        return "sse2" in self.cpu_flags

    def unbound(self) -> list:
        """Devices with no driver. The honest answer to "does it all work?".

        Reported rather than hidden: a machine with an unbound network
        controller is the single most useful thing a first-boot probe can
        tell somebody, and it is exactly what a report that only listed
        successes would leave out.
        """
        return [d for d in self.devices
                if not d.bound and d.kind in ("gpu", "net", "multimedia")]
