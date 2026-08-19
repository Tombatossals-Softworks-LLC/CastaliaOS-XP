"""Restore Points CLI (Bible §9, P8).

    castalia-restore create  [--label L] [--reason R] [--dry-run]
    castalia-restore list
    castalia-restore restore ID  --confirm  [--dry-run]
    castalia-restore prune   [--dry-run]

Shares the tested engine with the (future) Recovery Center GUI. `--dry-run`
prints the exact rsync commands and touches nothing.
"""

from __future__ import annotations

import argparse
import datetime
import sys

from .engine import (
    DryRunner,
    RestoreRefused,
    SubprocessRunner,
    create_point,
    list_points,
    prune,
    restore_argv,
    restore_point,
    snapshot_argv,
)
from .model import RecoveryConfig


def _now_id() -> str:
    # Microsecond precision so two points created in the same second (e.g. a
    # restore auto-takes a pre-restore point) never collide/overwrite.
    return datetime.datetime.now().strftime("%Y%m%d-%H%M%S-%f")


def _now_iso() -> str:
    return datetime.datetime.now().strftime("%Y-%m-%d %H:%M")


def build_argparser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(prog="castalia-restore")
    p.add_argument("--source", default="/")
    p.add_argument("--store", default="/var/lib/castalia/restore")
    p.add_argument("--keep", type=int, default=5)
    p.add_argument("--dry-run", action="store_true")
    sub = p.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("create")
    c.add_argument("--label", default="")
    c.add_argument("--reason", default="manual")
    sub.add_parser("list")
    r = sub.add_parser("restore")
    r.add_argument("id")
    r.add_argument("--confirm", action="store_true")
    sub.add_parser("prune")
    return p


def main(argv: list[str] | None = None) -> int:
    args = build_argparser().parse_args(argv)
    cfg = RecoveryConfig(source_root=args.source, store_dir=args.store,
                         keep=args.keep)
    problems = cfg.validate()
    if problems:
        for pr in problems:
            print(f"castalia-restore: error: {pr}", file=sys.stderr)
        return 2

    if args.cmd == "create":
        pid = _now_id()
        if args.dry_run:
            print(f"# would create restore point {pid} "
                  f"(reason={args.reason})")
            # prev is the newest existing point
            prev = None
            existing = list_points(SubprocessRunner(), cfg)
            prev = existing[0].id if existing else None
            print("  $ " + " ".join(snapshot_argv(cfg, pid, prev)))
            return 0
        create_point(SubprocessRunner(), cfg, point_id=pid, label=args.label,
                     reason=args.reason, created=_now_iso())
        prune(SubprocessRunner(), cfg)
        print(f"castalia-restore: created {pid}")
        return 0

    if args.cmd == "list":
        pts = list_points(SubprocessRunner(), cfg)
        if not pts:
            print("(sin puntos de restauración)")
        for p in pts:
            print(f"{p.id}  {p.reason:12}  {p.created:16}  {p.label}")
        return 0

    if args.cmd == "prune":
        runner = DryRunner() if args.dry_run else SubprocessRunner()
        removed = prune(runner, cfg)
        print(f"castalia-restore: {'would remove' if args.dry_run else 'removed'}"
              f" {len(removed)} point(s): {', '.join(removed) or '-'}")
        return 0

    if args.cmd == "restore":
        if args.dry_run:
            print(f"# would restore {args.id}")
            print("  $ " + " ".join(restore_argv(cfg, args.id)))
            return 0
        try:
            restore_point(SubprocessRunner(), cfg, args.id,
                          confirm=args.confirm, pre_id=_now_id(),
                          created=_now_iso())
        except RestoreRefused as exc:
            print(f"castalia-restore: {exc}", file=sys.stderr)
            print("castalia-restore: pass --confirm to proceed",
                  file=sys.stderr)
            return 3
        print(f"castalia-restore: restored {args.id}")
        return 0
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
