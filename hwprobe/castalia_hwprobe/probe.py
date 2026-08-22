"""Reading the machine, and writing down what was read (Bible §6.15).

Everything is rooted at a *root* argument that defaults to ``/``. That is not
a testing convenience bolted on afterwards — it is the only way hardware code
can be tested at all, because the build machine's hardware is never the user's.
The tests point it at captured sysfs trees from the machines §19 certifies.

Nothing here uses ``lspci``. sysfs is always present, on every machine,
including a FLOOR-tier install with no pciutils; a probe that needed a package
to run would be a probe that does not run on the machine that needs it most.
The same inversion the Hardware Center makes (apps/hardware/src/main.cpp).
"""

from __future__ import annotations

import json
import os

from .model import Device, Machine


def _read(path: str) -> str:
    try:
        with open(path, encoding="utf-8", errors="replace") as fh:
            return fh.read().strip()
    except OSError:
        return ""


def _hex(value: str) -> str:
    """``0x8086`` -> ``8086``. Anything unreadable becomes "".""" 
    value = value.strip().lower()
    if value.startswith("0x"):
        value = value[2:]
    return value if value and all(c in "0123456789abcdef" for c in value) \
        else ""


def probe_pci(root: str = "/") -> list:
    """Every PCI device under *root*, in bus order.

    Bus order is sysfs's own directory order sorted, which is the order the
    firmware enumerated them — so the first display controller is the one the
    machine posted on, which is what "the primary GPU" means in practice.
    """
    base = os.path.join(root, "sys/bus/pci/devices")
    try:
        names = sorted(os.listdir(base))
    except OSError:
        return []
    out = []
    for name in names:
        path = os.path.join(base, name)
        vendor = _hex(_read(os.path.join(path, "vendor")))
        device = _hex(_read(os.path.join(path, "device")))
        pci_class = _hex(_read(os.path.join(path, "class")))
        if not vendor or not device or not pci_class:
            continue
        # The bound module is the basename of the `driver` symlink. A device
        # with nothing bound simply has no such link, which is the fact the
        # report most needs to carry.
        driver = ""
        link = os.path.join(path, "driver")
        if os.path.islink(link):
            driver = os.path.basename(os.readlink(link))
        out.append(Device(address=name, vendor=vendor, device=device,
                          pci_class=pci_class.zfill(6), driver=driver))
    return out


def probe_cpu(root: str = "/") -> tuple:
    """``(model name, flags)`` from /proc/cpuinfo. Empty when unreadable."""
    model, flags = "", ()
    for line in _read(os.path.join(root, "proc/cpuinfo")).splitlines():
        key, _, value = line.partition(":")
        key, value = key.strip(), value.strip()
        if key == "model name" and not model:
            model = value
        elif key == "flags" and not flags:
            flags = tuple(value.split())
        if model and flags:
            break
    return model, flags


def probe_ram_mib(root: str = "/") -> int:
    for line in _read(os.path.join(root, "proc/meminfo")).splitlines():
        if line.startswith("MemTotal:"):
            parts = line.split()
            if len(parts) >= 2 and parts[1].isdigit():
                return int(parts[1]) // 1024
    return 0


def probe_machine(root: str = "/") -> Machine:
    """Everything, in one pass. Decisions are NOT made here — see quirks."""
    model, flags = probe_cpu(root)
    return Machine(devices=probe_pci(root), cpu_model=model, cpu_flags=flags,
                   ram_mib=probe_ram_mib(root))


#: Where a probed machine's report lives. §6.15 names this path.
REPORT_DIR = "/var/lib/castalia/hwprobe"
REPORT_PATH = f"{REPORT_DIR}/report.json"

#: Bumped when the shape of the report changes. A reader that does not know
#: the version it finds must say so rather than guessing at the fields — a
#: Hardware Center from a newer release reading an older machine's report is
#: an ordinary situation, not an error.
REPORT_VERSION = 1


def report_dict(machine: Machine, *, probed_at: str = "") -> dict:
    return {
        "version": REPORT_VERSION,
        "probed_at": probed_at,
        "cpu": {"model": machine.cpu_model, "sse2": machine.has_sse2},
        "ram_mib": machine.ram_mib,
        "xorg_driver": machine.xorg_driver,
        "suspend": machine.suspend,
        "modules_blacklist": list(machine.modules_blacklist),
        "notes": list(machine.notes),
        "devices": [
            {"address": d.address, "id": d.ident, "class": d.pci_class,
             "kind": d.kind, "driver": d.driver}
            for d in machine.devices
        ],
    }


def write_report(machine: Machine, path: str = REPORT_PATH, *,
                 probed_at: str = "") -> str:
    """Write the report, atomically, and return the path.

    Atomically because this runs at first boot, on a machine that may be
    switched off during it, and a half-written report that parses as valid
    JSON is worse than no report — the Hardware Center would show it.
    """
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    tmp = f"{path}.tmp"
    with open(tmp, "w", encoding="utf-8") as fh:
        json.dump(report_dict(machine, probed_at=probed_at), fh,
                  ensure_ascii=False, indent=2)
        fh.write("\n")
        fh.flush()
        os.fsync(fh.fileno())
    os.replace(tmp, path)
    return path


def read_report(path: str = REPORT_PATH) -> dict:
    """The last report, or ``{}`` if there is not one we understand."""
    try:
        with open(path, encoding="utf-8") as fh:
            doc = json.load(fh)
    except (OSError, ValueError):
        return {}
    if not isinstance(doc, dict) or doc.get("version") != REPORT_VERSION:
        return {}
    return doc
