"""Command-line driver for the installer backend.

Two modes:

* ``--dry-run`` prints the exact, ordered plan (like ``mkiso.sh --dry-run``) —
  every command it *would* run — and touches nothing;
* otherwise it executes, but every destructive step is refused unless
  ``--confirm-erase DISK`` names the exact target disk (§14.5 #1).

This is the shared backend; the Qt GUI and ncurses TUI call the same functions.
"""

from __future__ import annotations

import argparse
import sys

from .engine import (
    ConfirmationRequired,
    StepFailed,
    SubprocessRunner,
    execute,
)
from .model import (
    MODE_ALONGSIDE,
    MODE_SHRINK,
    MODE_WHOLE_DISK,
    DiskInfo,
    InstallConfig,
    PartitionInfo,
    available_modes,
    largest_free_region,
    max_freeable_mib,
    partition_path,
    plan_shrink,
    shrink_candidates,
)
from .probe import probe_partitions, probe_used
from .plan import build_plan


def _probe_size_mib(disk: str) -> int:
    import subprocess

    out = subprocess.run(
        ["blockdev", "--getsize64", disk], capture_output=True, text=True,
        check=True,
    ).stdout.strip()
    return int(out) // (1024 * 1024)


def build_argparser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="castalia-install",
        description="Castalia guided installer backend (Bible §14).",
    )
    p.add_argument("--disk", required=True, help="target disk, e.g. /dev/sda")
    p.add_argument("--disk-size-mib", type=int, default=0,
                   help="disk size in MiB (probed if omitted)")
    p.add_argument("--hostname", default="castalia")
    p.add_argument("--user", dest="username", default="usuario")
    p.add_argument("--full-name", default="Usuario de Castalia")
    p.add_argument("--theme", default="classic")
    p.add_argument("--locale", default="es_ES.UTF-8")
    p.add_argument("--keymap", default="es")
    p.add_argument("--timezone", default="Europe/Madrid")
    p.add_argument("--ram-mib", type=int, default=2048)
    p.add_argument("--swap-mib", type=int, default=0)
    p.add_argument("--source-root",
                   default="/run/live/rootfs/filesystem.squashfs")
    p.add_argument("--mount-root", default="/target")
    p.add_argument("--autologin", action="store_true")
    p.add_argument("--dry-run", action="store_true",
                   help="print the plan and exit; touch nothing")
    p.add_argument("--copy-only", action="store_true",
                   help="partition/format/copy/fstab only, skip the chroot "
                        "phase (bootloader, user) — used by the loopback smoke")
    p.add_argument("--mode",
                   choices=[MODE_WHOLE_DISK, MODE_ALONGSIDE, MODE_SHRINK],
                   default=MODE_WHOLE_DISK,
                   help="whole-disk erases the target; alongside installs "
                        "into existing free space and leaves every partition "
                        "untouched; shrink takes space off an existing "
                        "filesystem first and then installs into it "
                        "(Bible §14.3)")
    p.add_argument("--shrink", metavar="DEV", default=None,
                   help="with --mode shrink: which existing partition gives "
                        "up the space (default: whichever can spare most)")
    p.add_argument("--free-mib", type=int, default=None,
                   help="with --mode shrink: how much to free, in MiB "
                        "(default: as much as can safely be freed)")
    p.add_argument("--pretend-partition", metavar="SPEC", action="append",
                   default=[],
                   help="--dry-run only: rehearse against a partition table "
                        "described as INDEX:START_MIB:SIZE_MIB:FSTYPE"
                        "[:USED_MIB], repeatable. Lets the alongside and "
                        "shrink plans be inspected on a machine that does "
                        "not have the disk in question")
    p.add_argument("--confirm-erase", metavar="DISK", default=None,
                   help="type the target disk to authorise destructive steps")
    p.add_argument("--password", default=None,
                   help="set the user's password (visible in argv; prefer "
                        "--password-stdin)")
    p.add_argument("--password-stdin", action="store_true",
                   help="read the user's password from the first line of "
                        "stdin (keeps it out of argv and the process list)")
    return p


def parse_pretend(disk: str,
                  specs: list[str]) -> tuple[list[PartitionInfo], dict]:
    """Turn ``--pretend-partition`` strings into a table and a used-space map.

    Named *pretend* rather than anything more respectable because that is
    exactly what it is, and a person reading a plan produced this way has to
    know the numbers came off a command line rather than off a disk. It is
    refused outside ``--dry-run`` for the same reason.
    """
    parts: list[PartitionInfo] = []
    used: dict[str, int] = {}
    for spec in specs:
        bits = spec.split(":")
        if len(bits) not in (4, 5):
            raise ValueError(
                f"--pretend-partition {spec!r}: expected "
                f"INDEX:START_MIB:SIZE_MIB:FSTYPE[:USED_MIB]")
        index, start, size, fstype = bits[0], bits[1], bits[2], bits[3]
        path = partition_path(disk, int(index))
        part = PartitionInfo(path=path, index=int(index),
                             start_mib=int(start), size_mib=int(size),
                             fstype=fstype)
        parts.append(part)
        if len(bits) == 5:
            used[path] = int(bits[4])
    return sorted(parts, key=lambda pt: pt.start_mib), used


def config_from_args(args: argparse.Namespace) -> InstallConfig:
    return InstallConfig(
        target_disk=args.disk,
        hostname=args.hostname,
        username=args.username,
        full_name=args.full_name,
        theme=args.theme,
        locale=args.locale,
        keymap=args.keymap,
        timezone=args.timezone,
        autologin=args.autologin,
        ram_mib=args.ram_mib,
        swap_mib=args.swap_mib,
        source_root=args.source_root,
        mount_root=args.mount_root,
        mode=args.mode,
    )


def _setup_shrink(cfg: InstallConfig, disk: DiskInfo,
                  existing: list[PartitionInfo], used: dict[str, int],
                  args: argparse.Namespace) -> int:
    """Choose what to shrink and by how much. Returns 0, or an exit code.

    Every failure here is a refusal to proceed, and every refusal prints the
    numbers behind it. This is the one mode where the installer modifies data
    that was on the machine before it arrived, so "no, and here is exactly
    why" is the only acceptable way to decline.
    """
    if not existing:
        print(f"castalia-install: error: nothing to shrink on {disk.path} — "
              f"no existing partitions were found", file=sys.stderr)
        return 2
    candidates = shrink_candidates(disk, existing, used)
    if args.shrink:
        chosen = next((p for p in existing if p.path == args.shrink), None)
        if chosen is None:
            print(f"castalia-install: error: {args.shrink} is not a partition "
                  f"of {disk.path}", file=sys.stderr)
            for part in existing:
                print(f"  found: {part.describe()}", file=sys.stderr)
            return 2
    elif candidates:
        chosen = candidates[0]
    else:
        print(f"castalia-install: error: no partition on {disk.path} can "
              f"spare enough space for an install", file=sys.stderr)
        for part in existing:
            u = used.get(part.path)
            if not part.shrink_tool:
                room = (f"nothing — this installer does not resize "
                        f"{part.fstype or 'unidentified'} filesystems")
            elif u is None:
                room = "unknown — could not measure how full it is"
            else:
                room = (f"{max_freeable_mib(part, u)} MiB "
                        f"({u} MiB in use, and it keeps room to work in)")
            print(f"  {part.describe()} — can spare {room}", file=sys.stderr)
        print("  offer --mode whole-disk (erases everything) instead",
              file=sys.stderr)
        return 2

    if chosen.path not in used:
        print(f"castalia-install: error: could not measure how full "
              f"{chosen.path} is, and will not resize a filesystem on a "
              f"guess", file=sys.stderr)
        return 2
    try:
        shrink = plan_shrink(chosen, used[chosen.path], args.free_mib)
    except ValueError as exc:
        print(f"castalia-install: error: {exc}", file=sys.stderr)
        return 2

    cfg.shrink = shrink
    cfg.free_start_mib = shrink.freed.start_mib
    cfg.free_end_mib = shrink.freed.end_mib
    cfg.first_index = max((pt.index for pt in existing), default=0) + 1
    return 0


def main(argv: list[str] | None = None) -> int:
    args = build_argparser().parse_args(argv)
    cfg = config_from_args(args)

    size_mib = args.disk_size_mib
    if size_mib <= 0 and not args.dry_run:
        size_mib = _probe_size_mib(args.disk)
    if size_mib <= 0:
        # dry-run with no probe: assume a representative 40 GiB disk
        size_mib = 40 * 1024

    disk = DiskInfo(path=args.disk, size_mib=size_mib)

    # An alongside install has to be told WHERE the free space is, and the
    # only trustworthy source for that is the disk itself. Reading it here
    # rather than taking it on the command line means the offsets the plan
    # uses are the ones the partition table actually has — a stale figure
    # typed by a person is a partition written over somebody's data.
    if args.pretend_partition and not args.dry_run:
        print("castalia-install: error: --pretend-partition is a rehearsal "
              "aid and is refused outside --dry-run; a real install reads "
              "the real partition table", file=sys.stderr)
        return 2
    if args.pretend_partition:
        try:
            existing, used = parse_pretend(args.disk, args.pretend_partition)
        except ValueError as exc:
            print(f"castalia-install: error: {exc}", file=sys.stderr)
            return 2
    elif args.dry_run:
        existing, used = [], {}
    else:
        existing = probe_partitions(args.disk)
        # Only measure when we are about to make a decision that needs it:
        # ntfsresize --info walks the whole volume, which is not something to
        # do to every partition of every disk just to draw a menu.
        used = probe_used(existing) if cfg.mode == MODE_SHRINK else {}

    if cfg.mode == MODE_SHRINK:
        rc = _setup_shrink(cfg, disk, existing, used, args)
        if rc:
            return rc
    elif cfg.mode == MODE_ALONGSIDE:
        # With no partitions at all — a blank disk, or a dry run that never
        # looked — the "free region" is the whole disk and index 1, which is
        # the correct answer rather than a special case.
        region = largest_free_region(disk, existing)
        if region is None:
            print("castalia-install: error: no free space big enough for an "
                  "alongside install on " + args.disk, file=sys.stderr)
            for part in existing:
                print(f"  existing: {part.describe()}", file=sys.stderr)
            print("  offer --mode whole-disk (erases everything) instead",
                  file=sys.stderr)
            return 2
        else:
            cfg.first_index = max((pt.index for pt in existing), default=0) + 1
        cfg.free_start_mib = region.start_mib
        cfg.free_end_mib = region.end_mib

    problems = cfg.validate(disk)
    if problems:
        for pr in problems:
            print(f"castalia-install: error: {pr}", file=sys.stderr)
        return 2

    # A password may be requested via argv (--password) or, preferably, on
    # stdin (--password-stdin). Either way it becomes a plan step run in the
    # chroot BEFORE /target is unmounted (never a detached post-step).
    want_password = bool(args.password) or args.password_stdin
    plan = build_plan(cfg, size_mib, configure_target=not args.copy_only,
                      set_password=want_password and not args.copy_only)

    if args.dry_run:
        print(f"# Castalia install plan for {cfg.target_disk} "
              f"({size_mib} MiB), mode: {cfg.mode}")
        if cfg.mode == MODE_SHRINK:
            assert cfg.shrink is not None
            print(f"#   SHRINKS {cfg.shrink.describe()}")
            print(f"#   then installs into {cfg.free_start_mib}"
                  f"–{cfg.free_end_mib} MiB, the space that frees")
            for part in existing:
                if part.path != cfg.shrink.partition.path:
                    print(f"#   keeping {part.describe()}")
        elif cfg.mode == MODE_ALONGSIDE:
            print(f"#   installing into {cfg.free_start_mib}"
                  f"–{cfg.free_end_mib} MiB; no existing partition is "
                  f"touched and the partition table is not replaced")
            for part in existing:
                print(f"#   keeping {part.describe()}")
        elif existing:
            print("#   ERASES these existing partitions:")
            for part in existing:
                print(f"#     {part.describe()}")
            print(f"#   (this disk could also take "
                  f"{', '.join(available_modes(disk, existing))})")
        print(f"#   /boot {plan.boot.size_mib} MiB · swap "
              f"{plan.swap.size_mib} MiB · / {plan.root.size_mib} MiB")
        for i, step in enumerate(plan.steps, 1):
            flag = " [DESTRUCTIVE]" if step.destructive else ""
            print(f"{i:2}. {step.title}{flag}")
            print(f"      $ {step.describe()}")
        return 0

    password = args.password
    if args.password_stdin:
        password = sys.stdin.readline().rstrip("\n")

    runner = SubprocessRunner()
    try:
        execute(plan, runner, confirm_disk=args.confirm_erase,
                secrets={"password": password or ""},
                log=lambda m: print(f"castalia-install: {m}"))
    except ConfirmationRequired as exc:
        print(f"castalia-install: {exc}", file=sys.stderr)
        print("castalia-install: pass --confirm-erase "
              f"{cfg.target_disk} to proceed", file=sys.stderr)
        return 3
    except StepFailed as exc:
        # A step failed. Say which and what it printed — not a traceback.
        print(f"castalia-install: step failed: {exc}", file=sys.stderr)
        return 4
    print("castalia-install: done — the system is installed and bootable")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
