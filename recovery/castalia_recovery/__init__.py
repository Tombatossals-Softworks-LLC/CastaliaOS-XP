"""Castalia Restore Points backend (Bible §9, P8 "Recoverable").

Space-efficient hardlinked rsync snapshots of the system state that work on
plain ext4. A pure, unit-testable core shared by the CLI (`castalia-restore`)
and the future Recovery Center GUI.
"""

from .model import DEFAULT_EXCLUDES, RecoveryConfig, RestorePoint
from .engine import (
    DryRunner,
    RestoreRefused,
    create_point,
    list_points,
    prune,
    restore_argv,
    restore_point,
    snapshot_argv,
)

__all__ = [
    "DEFAULT_EXCLUDES",
    "RecoveryConfig",
    "RestorePoint",
    "DryRunner",
    "RestoreRefused",
    "create_point",
    "list_points",
    "prune",
    "restore_argv",
    "restore_point",
    "snapshot_argv",
]
