"""Execute a :class:`~plan.Plan`, safely.

The engine talks to the outside world only through a :class:`Runner`, so the
whole thing runs under test with a fake runner that records calls and never
touches a disk. The §14.5 non-negotiables are enforced here:

* **#1** destructive steps run only after an explicit typed confirmation of the
  exact target disk (``confirm_disk``); otherwise they are refused;
* **#2/#4** the plan carries no network steps, so an install completes offline
  and never removes a bootable path it didn't create.
"""

from __future__ import annotations

import subprocess
from dataclasses import dataclass, field

from .model import partition_path
from .plan import Plan


class ConfirmationRequired(RuntimeError):
    """Raised when a destructive step is reached without confirmation."""


class StepFailed(RuntimeError):
    """A command in the plan failed. Carries what the command printed."""


class Runner:
    """Side-effect boundary. Real installs use :class:`SubprocessRunner`."""

    def run(self, argv: list[str], *, input_text: str | None = None) -> str:
        raise NotImplementedError

    def write_file(self, path: str, content: str, mode: int = 0o644) -> None:
        raise NotImplementedError


@dataclass
class DryRunner(Runner):
    """Records everything, executes nothing. UUIDs are deterministic fakes."""

    calls: list[list[str]] = field(default_factory=list)
    writes: list[tuple[str, str]] = field(default_factory=list)
    inputs: list[str | None] = field(default_factory=list)

    def run(self, argv: list[str], *, input_text: str | None = None) -> str:
        self.calls.append(argv)
        self.inputs.append(input_text)
        if argv[:1] == ["blkid"]:
            # ...blkid -s UUID -o value /dev/sdaN  -> a stable fake UUID
            dev = argv[-1]
            return f"UUID-{dev.rsplit('/', 1)[-1]}"
        return ""

    def write_file(self, path: str, content: str, mode: int = 0o644) -> None:
        self.writes.append((path, content))


class SubprocessRunner(Runner):
    """Really runs commands and writes files (used on the live system)."""

    def run(self, argv: list[str], *, input_text: str | None = None) -> str:
        proc = subprocess.run(
            argv, input=input_text, capture_output=True, text=True,
        )
        if proc.returncode != 0:
            # Output goes into the exception rather than being swallowed.
            # Without this a failing step surfaces as a bare CalledProcessError
            # and a traceback, and whatever parted or ntfsresize said about
            # WHY — which is the only useful thing on the screen — is thrown
            # away. Someone whose install just stopped halfway is owed the
            # tool's own words.
            detail = (proc.stderr or proc.stdout or "").strip()
            raise StepFailed(
                f"{' '.join(argv)} exited {proc.returncode}"
                + (f":\n{detail}" if detail else ""))
        return proc.stdout.strip()

    def write_file(self, path: str, content: str, mode: int = 0o644) -> None:
        import os

        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(content)
        os.chmod(path, mode)


def probe_uuids(runner: Runner, plan: Plan) -> dict[str, str]:
    """Read the UUID of each partition the plan created (role -> UUID).

    The indices come from the plan, not from a constant. They used to be
    hard-coded 1/2/3, which is right only when the layout starts at the front
    of an empty disk. An alongside install numbers on from whatever the
    existing table already uses, so on a disk with one partition Castalia's
    root is partition 4 — and the old code would have read partition 3's UUID
    and written it into the new system's fstab as ``/``. The freshly installed
    machine would then mount somebody else's partition as its root, or fail to
    boot; either way it is the existing OS that pays for it.
    """
    cfg = plan.config
    uuids: dict[str, str] = {}
    for part in plan.partitions:
        dev = partition_path(cfg.target_disk, part.index)
        uuids[part.role] = runner.run(
            ["blkid", "-s", "UUID", "-o", "value", dev]
        )
    return uuids


def execute(
    plan: Plan,
    runner: Runner,
    *,
    confirm_disk: str | None = None,
    secrets: dict | None = None,
    log=lambda _msg: None,
) -> None:
    """Run *plan* through *runner*.

    ``confirm_disk`` must equal the plan's target disk for any destructive
    step to run — the code embodiment of "never destroy data without an
    explicit, unambiguous confirmation" (§14.5 #1).
    """
    cfg = plan.config
    ctx: dict = {"config": cfg, "uuids": {}}
    destructive_ok = confirm_disk == cfg.target_disk
    secrets = secrets or {}

    for step in plan.steps:
        if step.destructive and not destructive_ok:
            raise ConfirmationRequired(
                f"refusing destructive step without confirming {cfg.target_disk}: "
                f"{step.title}"
            )
        log(step.title)
        if step.write is not None:
            path, render = step.write
            if not ctx["uuids"]:
                ctx["uuids"] = probe_uuids(runner, plan)
            runner.write_file(path, render(ctx))
            continue
        assert step.argv is not None
        argv = step.argv
        if step.chroot:
            argv = ["chroot", cfg.mount_root, *argv]
        # A step may consume a secret on stdin (e.g. the password fed to
        # chpasswd) so it never appears in argv, the plan text, or the log.
        input_text = step.stdin_text
        if step.stdin_key:
            input_text = f"{cfg.username}:{secrets.get(step.stdin_key, '')}\n"
        runner.run(argv, input_text=input_text)
