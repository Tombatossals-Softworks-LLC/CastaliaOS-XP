"""Restore Points — the engine (Bible §9, P8).

`snapshot_argv` / `restore_argv` are pure functions (unit-tested to the flag),
and everything that touches the disk goes through a :class:`Runner`, so the
create/list/restore/prune flow runs under test with a fake runner and never
mutates a real system. Restoring is gated: it refuses without an explicit
`confirm=True`, and it auto-takes a `pre-restore` point first so a restore is
itself undoable (P8 "always leave a way back").
"""

from __future__ import annotations

import os
import subprocess
from dataclasses import dataclass, field

from .model import RecoveryConfig, RestorePoint


class Runner:
    def run(self, argv: list[str]) -> str:
        raise NotImplementedError

    def listdir(self, path: str) -> list[str]:
        raise NotImplementedError

    def read(self, path: str) -> str:
        raise NotImplementedError

    def write(self, path: str, content: str) -> None:
        raise NotImplementedError


@dataclass
class DryRunner(Runner):
    """Records commands; simulates a small store for list/prune tests."""

    calls: list[list[str]] = field(default_factory=list)
    store: dict = field(default_factory=dict)   # id -> metadata text

    def run(self, argv: list[str]) -> str:
        self.calls.append(argv)
        return ""

    def listdir(self, path: str) -> list[str]:
        return sorted(self.store.keys())

    def read(self, path: str) -> str:
        pid = path.rstrip("/").split("/")[-2]  # .../<id>/meta
        return self.store.get(pid, "")

    def write(self, path: str, content: str) -> None:
        pid = path.rstrip("/").split("/")[-2]
        self.store[pid] = content


class SubprocessRunner(Runner):
    def run(self, argv: list[str]) -> str:
        return subprocess.run(argv, capture_output=True, text=True,
                              check=True).stdout

    def listdir(self, path: str) -> list[str]:
        return sorted(os.listdir(path)) if os.path.isdir(path) else []

    def read(self, path: str) -> str:
        try:
            with open(path, encoding="utf-8") as fh:
                return fh.read()
        except OSError:
            return ""

    def write(self, path: str, content: str) -> None:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(content)


def _rsync_base() -> list[str]:
    return ["rsync", "-aHAXS", "--numeric-ids", "--delete"]


def snapshot_argv(cfg: RecoveryConfig, point_id: str,
                  prev_id: str | None) -> list[str]:
    """rsync command that snapshots the system into a new point.

    ``--link-dest`` at the previous point makes unchanged files hardlinks, so a
    snapshot costs only the delta on disk.
    """
    argv = _rsync_base()
    for ex in cfg.excludes:
        argv += ["--exclude", ex]
    if prev_id:
        argv += ["--link-dest", cfg.point_path(prev_id)]
    argv += [cfg.source_root.rstrip("/") + "/", cfg.point_path(point_id) + "/"]
    return argv


def restore_argv(cfg: RecoveryConfig, point_id: str) -> list[str]:
    """rsync command that restores a point back over the live system."""
    argv = _rsync_base()
    for ex in cfg.excludes:
        argv += ["--exclude", ex]
    argv += [cfg.point_path(point_id) + "/", cfg.source_root.rstrip("/") + "/"]
    return argv


def list_points(runner: Runner, cfg: RecoveryConfig) -> list[RestorePoint]:
    points: list[RestorePoint] = []
    for name in runner.listdir(cfg.store_dir):
        meta = runner.read(cfg.point_path(name) + "/.castalia-restore")
        label, reason, created = "", "manual", ""
        for line in meta.splitlines():
            k, _, v = line.partition("=")
            if k == "label":
                label = v
            elif k == "reason":
                reason = v
            elif k == "created":
                created = v
        points.append(RestorePoint(name, label, reason, created))
    return sorted(points, key=lambda p: p.id, reverse=True)


def create_point(runner: Runner, cfg: RecoveryConfig, *, point_id: str,
                 label: str = "", reason: str = "manual",
                 created: str = "") -> RestorePoint:
    existing = list_points(runner, cfg)
    prev = existing[0].id if existing else None
    runner.run(["mkdir", "-p", cfg.point_path(point_id)])
    runner.run(snapshot_argv(cfg, point_id, prev))
    meta = (f"label={label}\nreason={reason}\ncreated={created}\n"
            f"prev={prev or ''}\n")
    runner.write(cfg.point_path(point_id) + "/.castalia-restore", meta)
    return RestorePoint(point_id, label, reason, created)


def prune(runner: Runner, cfg: RecoveryConfig) -> list[str]:
    """Remove the oldest points beyond `keep`. Returns removed ids."""
    points = list_points(runner, cfg)          # newest first
    removed = [p.id for p in points[cfg.keep:]]
    for pid in removed:
        runner.run(["rm", "-rf", cfg.point_path(pid)])
    return removed


class RestoreRefused(RuntimeError):
    pass


def restore_point(runner: Runner, cfg: RecoveryConfig, point_id: str, *,
                  confirm: bool = False, pre_id: str | None = None,
                  created: str = "") -> None:
    """Restore *point_id* over the system.

    Refuses unless ``confirm`` is True. Before restoring it takes a
    ``pre-restore`` point (when ``pre_id`` is given) so the restore is itself
    reversible.
    """
    if not confirm:
        raise RestoreRefused(
            f"refusing to restore {point_id} without confirm=True")
    ids = {p.id for p in list_points(runner, cfg)}
    if point_id not in ids:
        raise RestoreRefused(f"no such restore point: {point_id}")
    if pre_id:
        create_point(runner, cfg, point_id=pre_id, label="antes de restaurar",
                     reason="pre-restore", created=created)
    runner.run(restore_argv(cfg, point_id))
