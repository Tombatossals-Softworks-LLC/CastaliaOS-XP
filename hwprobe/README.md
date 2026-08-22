# hwprobe/ — the hardware probe (Bible §6.15, §18 Phase 1)

Reads what is in the machine, matches it against a shipped quirks table, and
writes a report to `/var/lib/castalia/hwprobe/report.json`:

    castalia-hwprobe                probe and write the report
    castalia-hwprobe --dry-run      print it, write nothing
    castalia-hwprobe --show         print the last report
    castalia-hwprobe --root DIR     probe a captured tree, not this machine

It runs twice in a machine's life and then on every boot: once inside the
chroot at install time, so the very first boot already has something to show,
and then from the runit service in `services/castalia-hwprobe`. Probing on
every boot is deliberate — hardware changes, and a report that was true in
2026 and is presented as current is worse than no report.

## What it decides

| Field | From |
|---|---|
| `xorg_driver` | the primary GPU's PCI vendor, then any quirk that overrides it |
| `suspend` | `safe` / `unsafe` / `unknown` — quirks only; **never** inferred |
| `modules_blacklist` | quirks only |
| `notes` | every quirk that matched, then every device with no driver bound |
| `cpu.sse2` | `/proc/cpuinfo` flags — §18's Legacy build exists for machines without it |

## Two rules

**It reads sysfs, never `lspci`.** sysfs is on every machine, including a
FLOOR-tier install with no `pciutils`. A probe that needed a package installed
would be a probe that does not run on the machine that needs it most. (The
Hardware Center makes the same inversion — see `apps/hardware/src/main.cpp`.)

**It does not claim what it has not been told.** A machine that is not in the
quirks table gets `suspend: unknown` and no driver override, and the Hardware
Center shows "unknown" as the real answer it is. A wrong `safe` is a lost
session and looks identical to a machine somebody actually tested; a wrong
driver override is a machine that worked and stopped. A test asserts that no
row in the shipped table claims `safe`, because no machine has been certified
yet (§19) — so the day one is, claiming it becomes a deliberate edit.

## The quirks table

`quirks.json`, shipped to `/usr/share/castalia/hwprobe/quirks.json` as **data**
next to the code, because §19's certification results are what correct it and
a table that needs a new package to fix a wrong row is a table that stays
wrong. `match` is `vendor:device` or a bare `vendor`; a device-specific row
wins over a vendor-wide one, which is the difference between "this generation
tends to" and "this one does". A malformed file degrades to the built-in table
with a warning — this runs at first boot, and a typo in shipped data must
never be what stops a machine.

## How it is tested

`tests/test_hwprobe.py` builds sysfs trees on disk — the same handful of tiny
text files the kernel exposes — and points the probe at them, because the
machine running the tests is never the machine the code is for. The trees
include the §16 FLOOR reference (Pentium 4, Intel graphics, 512 MB), a
dual-GPU machine, a Wi-Fi card with nothing bound to it, and a CPU with no
SSE2. The rest of the tests are about what the report is not allowed to claim.

## What v0 does not do

It records decisions; it does not apply them. Nothing writes an
`xorg.conf.d` snippet or a modprobe blacklist yet — the report is the input
the Hardware Center (§9) offers to a user who asks for it. Applying a quirk
automatically is a bigger promise than a first release should make, and it is
the kind that turns a machine that boots into one that does not.
