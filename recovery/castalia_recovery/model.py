"""Restore Points — data model (Bible §9, P8 "Recoverable").

A Restore Point is a space-efficient, hardlinked rsync snapshot of the system
state (everything except volatile dirs and user data), taken before risky
operations (updates) or on demand. It works on plain ext4 — no btrfs required —
so it holds on the FLOOR-tier hardware Castalia targets.

Pure data here; :mod:`engine` builds the rsync commands and runs them behind a
Runner boundary, keeping the logic unit-testable without touching a real disk.
"""

from __future__ import annotations

from dataclasses import dataclass, field

# Never snapshotted: kernel virtual filesystems, runtime dirs, caches, and —
# deliberately — user data. A Restore Point reverts the SYSTEM (a bad update, a
# botched config); it must never roll back or delete a user's /home.
DEFAULT_EXCLUDES = (
    "/proc/*", "/sys/*", "/dev/*", "/run/*", "/tmp/*", "/var/tmp/*",
    "/mnt/*", "/media/*", "/lost+found", "/swapfile",
    "/var/cache/apt/archives/*.deb", "/var/lib/castalia/restore",
    "/home/*", "/root/.cache/*",
    ".castalia-restore",  # our per-point metadata never lands on the target
)

VALID_REASONS = ("manual", "pre-update", "scheduled", "pre-restore")


@dataclass(frozen=True)
class RestorePoint:
    """One snapshot on disk: an id (sortable timestamp), label and reason."""

    id: str            # e.g. "20260709-153012"
    label: str
    reason: str
    created: str = ""  # human ISO date, from metadata

    @property
    def dirname(self) -> str:
        return self.id


@dataclass
class RecoveryConfig:
    source_root: str = "/"
    store_dir: str = "/var/lib/castalia/restore"
    keep: int = 5                     # prune to this many most-recent points
    excludes: tuple = field(default_factory=lambda: DEFAULT_EXCLUDES)

    def point_path(self, point_id: str) -> str:
        return f"{self.store_dir}/{point_id}"

    def validate(self) -> list[str]:
        errs: list[str] = []
        if self.keep < 1:
            errs.append("keep must be >= 1")
        if not self.store_dir.startswith("/"):
            errs.append("store_dir must be an absolute path")
        return errs
