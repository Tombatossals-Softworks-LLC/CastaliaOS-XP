"""Castalia installer backend (Bible §14).

A pure-Python, unit-testable core that both the Qt5 graphical installer and the
ncurses text installer drive (§12): describe the install as an
:class:`~castalia_installer.model.InstallConfig`, turn it into an inspectable
:class:`~castalia_installer.plan.Plan`, then run it through
:mod:`castalia_installer.engine` behind a confirmation gate.
"""

from .model import DiskInfo, InstallConfig, Partition
from .plan import Plan, Step, build_layout, build_plan
from .engine import ConfirmationRequired, DryRunner, execute
from .probe import parse_lsblk, probe_disks
from .tui import TextInstaller

__all__ = [
    "DiskInfo",
    "InstallConfig",
    "Partition",
    "Plan",
    "Step",
    "build_layout",
    "build_plan",
    "ConfirmationRequired",
    "DryRunner",
    "execute",
    "parse_lsblk",
    "probe_disks",
    "TextInstaller",
]
