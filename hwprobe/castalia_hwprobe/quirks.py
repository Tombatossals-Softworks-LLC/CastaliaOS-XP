"""The shipped quirks table, and the decisions it drives (Bible §6.15).

§6.15 is deliberately modest about what "driver management" means on Linux:
the kernel loads its own drivers and does it well. What it cannot do is know
that a particular old GPU's Xorg driver hangs on this hardware, or that
suspend on a given chipset comes back to a black screen. That knowledge is
per-model, it comes from testing (§19), and it has to live somewhere it can
be shipped, versioned, and corrected — which is here.

Two rules keep this table honest:

* **Nothing is claimed that has not been observed.** A machine that is not in
  the table gets ``suspend=unknown`` and no driver override — the modesetting
  path the kernel already chose. "Unknown" is a real answer and the Hardware
  Center shows it as one; a table that guessed "safe" would be worse than an
  empty table, because a wrong "safe" is a lost session.
* **Vendor-wide rows lose to device-specific ones.** A row for a whole vendor
  says "this generation tends to"; a row for one device says "this one does".
  When both match, the specific one is the observation and wins.
"""

from __future__ import annotations

import json

from .model import (
    SUSPEND_SAFE,
    SUSPEND_UNKNOWN,
    SUSPEND_UNSAFE,
    Machine,
    Quirk,
)

#: Xorg drivers by PCI vendor, for the hardware §19 actually targets. This is
#: a *hint* written into the report, not a config file written to the disk —
#: modern Xorg picks correctly on its own almost always, and overriding it
#: blindly is how a machine that worked stops working. The Hardware Center
#: offers it as the thing to try when the automatic choice failed.
VENDOR_XORG = {
    "8086": "modesetting",   # Intel GMA and later
    "10de": "nouveau",       # NVIDIA
    "1002": "radeon",        # ATI/AMD
    "1013": "cirrus",        # Cirrus Logic — QEMU's default, and real 90s HW
    "15ad": "vmware",        # VMware SVGA
    "1234": "modesetting",   # QEMU stdvga
}

#: v0 of the quirks table. Small on purpose: every row here is something that
#: was observed, and §19's hardware certification is what grows it. A row
#: added without a machine behind it is a rumour with a version number.
DEFAULT_QUIRKS = (
    Quirk(
        match="1013",
        note="Cirrus Logic: sin aceleración 3D utilizable; el escritorio "
             "funciona sin compositor (§4.4).",
        xorg_driver="cirrus",
        suspend=SUSPEND_UNKNOWN,
    ),
    Quirk(
        match="1234:1111",
        note="Tarjeta gráfica estándar de QEMU: es una máquina virtual, no "
             "hay suspensión real que probar.",
        xorg_driver="modesetting",
        suspend=SUSPEND_UNSAFE,
    ),
    Quirk(
        match="15ad:0405",
        note="VMware SVGA II: máquina virtual; suspensión gestionada por el "
             "anfitrión.",
        xorg_driver="vmware",
        suspend=SUSPEND_UNSAFE,
    ),
)


def load_quirks(path: str) -> tuple:
    """Read a quirks table from JSON. Malformed rows are skipped, not fatal.

    A quirks file is data that ships and gets corrected in the field, so a
    typo in it must degrade to "we know less about this machine", never to a
    first boot that fails. Whatever could be read is used.
    """
    try:
        with open(path, encoding="utf-8") as fh:
            raw = json.load(fh)
    except (OSError, ValueError):
        return ()
    rows = raw.get("quirks", raw) if isinstance(raw, dict) else raw
    if not isinstance(rows, list):
        return ()
    out = []
    for row in rows:
        if not isinstance(row, dict) or not row.get("match"):
            continue
        out.append(Quirk(
            match=str(row["match"]).lower(),
            note=str(row.get("note", "")),
            xorg_driver=str(row.get("xorg_driver", "")),
            suspend=str(row.get("suspend", "")),
            modules_blacklist=tuple(row.get("modules_blacklist", ())),
        ))
    return tuple(out)


def matching_quirks(machine: Machine, quirks) -> list:
    """Rows that apply to *machine*, vendor-wide first, specific last.

    The order is the precedence: later rows overwrite earlier ones, so a
    device-specific observation lands on top of the vendor-wide guess.
    """
    idents = {d.ident for d in machine.devices}
    vendors = {d.vendor for d in machine.devices}
    wide = [q for q in quirks if q.is_vendor_wide and q.match in vendors]
    exact = [q for q in quirks if not q.is_vendor_wide and q.match in idents]
    return wide + exact


def decide(machine: Machine, quirks=DEFAULT_QUIRKS) -> Machine:
    """Fill in the decisions on *machine* from the quirks table.

    Returns the same object, mutated, because a probe report is one thing
    that gets progressively filled in rather than a chain of copies.
    """
    # The starting point is what the vendor of the primary GPU suggests. The
    # primary GPU is the first one sysfs lists, which is bus order, which is
    # what the firmware posts — near enough on every machine §19 targets, and
    # honestly wrong only on dual-GPU laptops that are not in scope for v1.
    gpus = machine.gpus
    if gpus:
        machine.xorg_driver = VENDOR_XORG.get(gpus[0].vendor, "")
    machine.suspend = SUSPEND_UNKNOWN
    blacklist: list = []

    for quirk in matching_quirks(machine, quirks):
        if quirk.xorg_driver:
            machine.xorg_driver = quirk.xorg_driver
        if quirk.suspend in (SUSPEND_SAFE, SUSPEND_UNSAFE, SUSPEND_UNKNOWN):
            machine.suspend = quirk.suspend
        blacklist.extend(quirk.modules_blacklist)
        if quirk.note:
            machine.notes.append(quirk.note)

    machine.modules_blacklist = tuple(dict.fromkeys(blacklist))

    # Said last, so it is the final line of the report: what is in the machine
    # that nothing is driving. This is the most useful sentence a first-boot
    # probe can produce and the easiest one to leave out of a report that only
    # lists what worked.
    for dev in machine.unbound():
        machine.notes.append(
            f"Sin controlador: {dev.ident} ({dev.kind}) en {dev.address}")
    return machine
