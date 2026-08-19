"""The text installer (Bible §14.2 fallback, §14.5 #5 "never a dead end").

A plain line-based installer for VESA-only / broken-GL machines. It drives the
same backend as the Qt GUI, so the logic is identical and shared. I/O goes
through injected ``inp``/``out`` callables, which makes the whole flow
unit-testable with scripted input and a :class:`DryRunner`.
"""

from __future__ import annotations

from typing import Callable

from .engine import ConfirmationRequired, Runner, execute
from .model import DiskInfo, InstallConfig
from .plan import build_plan


class TextInstaller:
    def __init__(
        self,
        disks: list[DiskInfo],
        inp: Callable[[str], str],
        out: Callable[[str], None],
        runner: Runner,
        *,
        source_root: str = "/run/live/rootfs/filesystem.squashfs",
        mount_root: str = "/target",
        ram_mib: int = 2048,
    ):
        self.disks = disks
        self.inp = inp
        self.out = out
        self.runner = runner
        self.source_root = source_root
        self.mount_root = mount_root
        self.ram_mib = ram_mib

    def _ask(self, prompt: str, default: str = "") -> str:
        hint = f" [{default}]" if default else ""
        val = self.inp(f"{prompt}{hint}: ").strip()
        return val or default

    def run(self) -> int:
        self.out("== Instalador de Castalia OS (modo texto) ==")
        if not self.disks:
            self.out("No se encontró ningún disco donde instalar.")
            return 2

        self.out("\nDiscos disponibles:")
        for i, d in enumerate(self.disks, 1):
            gib = d.size_mib / 1024.0
            self.out(f"  {i}) {d.path}  {gib:.1f} GiB  {d.model}")
        choice = self._ask("Elige un disco por número", "1")
        try:
            disk = self.disks[int(choice) - 1]
        except (ValueError, IndexError):
            self.out("Selección no válida.")
            return 2

        hostname = self._ask("Nombre del equipo", "castalia")
        username = self._ask("Nombre de usuario", "usuario")
        full_name = self._ask("Tu nombre", "Usuario de Castalia")

        cfg = InstallConfig(
            target_disk=disk.path, hostname=hostname, username=username,
            full_name=full_name, ram_mib=self.ram_mib,
            source_root=self.source_root, mount_root=self.mount_root,
        )
        problems = cfg.validate(disk)
        if problems:
            for p in problems:
                self.out(f"  error: {p}")
            return 2

        plan = build_plan(cfg, disk.size_mib)
        self.out("\nSe realizarán estos pasos:")
        for i, step in enumerate(plan.steps, 1):
            mark = " (destructivo)" if step.destructive else ""
            self.out(f"  {i:2}. {step.title}{mark}")

        self.out(f"\nATENCIÓN: se borrará todo el contenido de {disk.path}.")
        confirm = self._ask(f"Escribe {disk.path} para confirmar")
        try:
            execute(plan, self.runner, confirm_disk=confirm,
                    log=lambda m: self.out(f"  … {m}"))
        except ConfirmationRequired:
            self.out("Confirmación incorrecta — instalación cancelada.")
            return 3

        self.out("\n✔ Instalación completada. Reinicia para usar Castalia OS.")
        return 0


def main() -> int:  # pragma: no cover - thin real-I/O wrapper
    import os

    from .engine import SubprocessRunner
    from .probe import probe_disks

    ram_kib = 0
    try:
        with open("/proc/meminfo", encoding="utf-8") as fh:
            for line in fh:
                if line.startswith("MemTotal:"):
                    ram_kib = int(line.split()[1])
                    break
    except OSError:
        pass

    installer = TextInstaller(
        probe_disks(), input, print, SubprocessRunner(),
        ram_mib=(ram_kib // 1024) or 2048,
    )
    if os.geteuid() != 0:
        print("El instalador necesita privilegios de root.")
        return 2
    return installer.run()


if __name__ == "__main__":  # pragma: no cover
    raise SystemExit(main())
