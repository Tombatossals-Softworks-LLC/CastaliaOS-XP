"""castalia-hwprobe — the hardware probe service (Bible §6.15, §18 P1).

    castalia-hwprobe                 probe and write /var/lib/castalia/hwprobe
    castalia-hwprobe --dry-run       print the report, write nothing
    castalia-hwprobe --root DIR      probe a captured tree instead of this
                                     machine (what the tests use)
    castalia-hwprobe --quirks FILE   use a quirks table other than the
                                     built-in one
    castalia-hwprobe --show          print the last report and exit

Run at install time by the installer, and at every boot by the runit service
in services/castalia-hwprobe. Probing on every boot rather than only once is
deliberate: hardware changes — a card is added, a disk is moved into a
different machine — and a report that was true in 2026 and is presented as
current is worse than no report.
"""

from __future__ import annotations

import argparse
import datetime
import json
import sys

from .probe import REPORT_PATH, probe_machine, read_report, write_report
from .quirks import DEFAULT_QUIRKS, decide, load_quirks


def build_argparser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="castalia-hwprobe",
        description="Castalia hardware probe (Bible §6.15).")
    p.add_argument("--root", default="/",
                   help="probe this tree instead of the running machine")
    p.add_argument("--out", default=REPORT_PATH,
                   help=f"where to write the report (default {REPORT_PATH})")
    p.add_argument("--quirks", default=None,
                   help="a quirks table in JSON, instead of the built-in one")
    p.add_argument("--dry-run", action="store_true",
                   help="print the report and exit; write nothing")
    p.add_argument("--show", action="store_true",
                   help="print the last report written and exit")
    return p


def human(doc: dict) -> str:
    """The report as something a person reads over a serial console."""
    cpu = doc.get("cpu", {})
    lines = [
        "castalia-hwprobe:",
        f"  CPU:        {cpu.get('model') or '(desconocida)'}"
        f"{'' if cpu.get('sse2') else '  [sin SSE2]'}",
        f"  RAM:        {doc.get('ram_mib', 0)} MiB",
        f"  Xorg:       {doc.get('xorg_driver') or '(automático)'}",
        f"  Suspensión: {doc.get('suspend', 'unknown')}",
    ]
    black = doc.get("modules_blacklist") or []
    if black:
        lines.append(f"  Módulos desactivados: {', '.join(black)}")
    devices = doc.get("devices", [])
    lines.append(f"  Dispositivos PCI: {len(devices)}")
    for dev in devices:
        if dev.get("kind") in ("gpu", "net", "multimedia"):
            drv = dev.get("driver") or "SIN CONTROLADOR"
            lines.append(f"    {dev['address']}  {dev['id']}  "
                         f"{dev['kind']:<10} {drv}")
    for note in doc.get("notes", []):
        lines.append(f"  * {note}")
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    args = build_argparser().parse_args(argv)

    if args.show:
        doc = read_report(args.out)
        if not doc:
            print("castalia-hwprobe: no hay ningún informe legible en "
                  f"{args.out}", file=sys.stderr)
            return 1
        print(human(doc))
        return 0

    quirks = DEFAULT_QUIRKS
    if args.quirks:
        loaded = load_quirks(args.quirks)
        if not loaded:
            # Not fatal: a broken quirks file must degrade to "we know less
            # about this machine", never to a boot that fails. But it is said
            # out loud, because silently ignoring the table would look
            # exactly like a machine with no quirks.
            print(f"castalia-hwprobe: aviso: no se ha podido leer "
                  f"{args.quirks}; se usa la tabla incorporada",
                  file=sys.stderr)
        else:
            quirks = loaded

    machine = decide(probe_machine(args.root), quirks)
    if args.dry_run:
        from .probe import report_dict

        print(json.dumps(report_dict(machine), ensure_ascii=False, indent=2))
        return 0

    stamp = datetime.datetime.now().strftime("%Y-%m-%d %H:%M")
    try:
        path = write_report(machine, args.out, probed_at=stamp)
    except OSError as exc:
        print(f"castalia-hwprobe: no se ha podido escribir el informe: {exc}",
              file=sys.stderr)
        return 1
    print(human(read_report(path)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
