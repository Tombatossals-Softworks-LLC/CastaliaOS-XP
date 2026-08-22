"""The text installer (Bible §14.2 fallback, §14.5 #5 "never a dead end").

A plain line-based installer for VESA-only / broken-GL machines. It drives the
same backend as the Qt GUI, so the logic is identical and shared. I/O goes
through injected ``inp``/``out``/``secret`` callables, and so does disk
probing, which makes the whole flow unit-testable with scripted input, a
scripted partition table, and a :class:`DryRunner` — no disk, no terminal.

"Never a dead end" is the requirement this file exists for, and for a long
time it met the letter of it and not the point: it could only erase the whole
disk. A machine that falls back to the text installer because its graphics
are broken is exactly the machine most likely to have a Windows on it that
somebody needs to keep. So this offers the same three install modes the
backend has, in the same least-destructive-first order, and refuses in the
same words.
"""

from __future__ import annotations

from typing import Callable

from .engine import ConfirmationRequired, Runner, StepFailed, execute
from .model import (
    MIN_ALONGSIDE_MIB,
    MODE_ALONGSIDE,
    MODE_SHRINK,
    MODE_WHOLE_DISK,
    DiskInfo,
    InstallConfig,
    PartitionInfo,
    available_modes,
    largest_free_region,
    max_freeable_mib,
    min_shrink_mib,
    plan_shrink,
    shrink_candidates,
)
from .plan import build_plan

#: What each mode is called on screen, and the one line that says what it
#: does to the disk. The second half matters more than the first: somebody
#: choosing here is deciding what happens to everything already on the
#: machine, and "instalar junto a" does not say that on its own.
MODE_LABELS = {
    MODE_ALONGSIDE: (
        "Instalar junto al sistema actual",
        "usa el espacio libre que ya hay; no toca ninguna partición"),
    MODE_SHRINK: (
        "Hacer sitio encogiendo una partición",
        "reduce un sistema de archivos existente y se instala en el hueco"),
    MODE_WHOLE_DISK: (
        "Usar el disco entero",
        "BORRA todo lo que haya en el disco"),
}


def _mib(value: int) -> str:
    """MiB as something a person reads. GiB above a gigabyte, MiB below."""
    if value >= 1024:
        return f"{value / 1024:.1f} GiB"
    return f"{value} MiB"


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
        secret: Callable[[str], str] | None = None,
        probe_partitions: Callable[[str], list[PartitionInfo]] | None = None,
        probe_used: Callable[
            [list[PartitionInfo]], dict[str, int]] | None = None,
    ):
        self.disks = disks
        self.inp = inp
        self.out = out
        self.runner = runner
        self.source_root = source_root
        self.mount_root = mount_root
        self.ram_mib = ram_mib
        # A password typed into a terminal must not echo. Injected so a test
        # can script it; the default is the real getpass.
        if secret is None:
            import getpass

            secret = getpass.getpass
        self.secret = secret
        if probe_partitions is None or probe_used is None:
            from . import probe as _probe

            probe_partitions = probe_partitions or _probe.probe_partitions
            probe_used = probe_used or _probe.probe_used
        self.probe_partitions = probe_partitions
        self.probe_used = probe_used

    # ---- prompts ----------------------------------------------------------

    def _ask(self, prompt: str, default: str = "") -> str:
        hint = f" [{default}]" if default else ""
        val = self.inp(f"{prompt}{hint}: ").strip()
        return val or default

    def _ask_int(self, prompt: str, default: int, lo: int, hi: int) -> int:
        """An integer inside [lo, hi]. Re-asks rather than guessing.

        Silently clamping an out-of-range answer would be the installer
        deciding how much of somebody's Windows to take, having been told a
        different number.
        """
        while True:
            raw = self._ask(prompt, str(default))
            try:
                value = int(raw)
            except ValueError:
                self.out(f"  «{raw}» no es un número.")
                continue
            if lo <= value <= hi:
                return value
            self.out(f"  Tiene que estar entre {lo} y {hi}.")

    # ---- steps ------------------------------------------------------------

    def _choose_disk(self) -> DiskInfo | None:
        self.out("\nDiscos disponibles:")
        for i, d in enumerate(self.disks, 1):
            gib = d.size_mib / 1024.0
            self.out(f"  {i}) {d.path}  {gib:.1f} GiB  {d.model}")
        choice = self._ask("Elige un disco por número", "1")
        try:
            index = int(choice)
            if index < 1:
                raise IndexError(index)
            return self.disks[index - 1]
        except (ValueError, IndexError):
            self.out("Selección no válida.")
            return None

    def _choose_mode(self, disk: DiskInfo,
                     existing: list[PartitionInfo],
                     used: dict[str, int]) -> str | None:
        modes = available_modes(disk, existing, used)
        if not modes:
            self.out(f"\nNo se puede instalar en {disk.path}.")
            return None
        if existing:
            self.out("\nEn este disco ya hay:")
            for part in existing:
                self.out(f"  {part.describe()}")
        self.out("\n¿Cómo quieres instalar?")
        for i, mode in enumerate(modes, 1):
            title, what = MODE_LABELS[mode]
            self.out(f"  {i}) {title}")
            self.out(f"     {what}")
        # The default is the first offered, which available_modes has already
        # ordered least-destructive-first. Whatever is default here is what a
        # tired person at 2am will press Enter on.
        choice = self._ask("Elige una opción por número", "1")
        try:
            index = int(choice)
            if index < 1:
                raise IndexError(index)
            return modes[index - 1]
        except (ValueError, IndexError):
            self.out("Selección no válida.")
            return None

    def _configure_alongside(self, cfg: InstallConfig, disk: DiskInfo,
                             existing: list[PartitionInfo]) -> bool:
        region = largest_free_region(disk, existing)
        if region is None:
            self.out("No hay espacio libre suficiente en este disco.")
            return False
        cfg.free_start_mib = region.start_mib
        cfg.free_end_mib = region.end_mib
        cfg.first_index = max((p.index for p in existing), default=0) + 1
        self.out(f"\nSe instalará en el hueco libre de "
                 f"{_mib(region.size_mib)}. No se toca ninguna partición.")
        return True

    def _configure_shrink(self, cfg: InstallConfig, disk: DiskInfo,
                          existing: list[PartitionInfo],
                          used: dict[str, int]) -> bool:
        candidates = shrink_candidates(disk, existing, used)
        if not candidates:
            self.out("Ninguna partición puede ceder espacio suficiente.")
            return False
        if len(candidates) == 1:
            chosen = candidates[0]
        else:
            self.out("\n¿De qué partición sacamos el espacio?")
            for i, part in enumerate(candidates, 1):
                room = max_freeable_mib(part, used[part.path])
                self.out(f"  {i}) {part.describe()} — puede ceder "
                         f"{_mib(room)}")
            pick = self._ask_int("Elige una por número", 1, 1, len(candidates))
            chosen = candidates[pick - 1]

        room = max_freeable_mib(chosen, used[chosen.path])
        floor = min_shrink_mib(chosen)
        self.out(f"\n{chosen.describe()}")
        self.out(f"  En uso ahora:      {_mib(used[chosen.path])}")
        self.out(f"  Puede ceder hasta: {_mib(room)}")
        self.out("  (se reserva siempre sitio de sobra para que el sistema")
        self.out("   que ya está ahí siga funcionando)")
        if floor > MIN_ALONGSIDE_MIB:
            # The counter-intuitive constraint, so say it out loud rather
            # than letting the range look arbitrary: this partition reaches
            # past the first 128 GiB, so taking too LITTLE would leave /boot
            # somewhere an old BIOS cannot read it (§6.2).
            self.out(f"  Hay que liberar al menos {_mib(floor)}: esta")
            self.out("  partición llega más allá de los primeros 128 GiB y")
            self.out("  el arranque tiene que quedar por debajo de ese punto.")
        want = self._ask_int("¿Cuántos MiB liberamos para Castalia?",
                             room, floor, room)
        try:
            shrink = plan_shrink(chosen, used[chosen.path], want)
        except ValueError as exc:
            self.out(f"  error: {exc}")
            return False
        cfg.shrink = shrink
        cfg.free_start_mib = shrink.freed.start_mib
        cfg.free_end_mib = shrink.freed.end_mib
        cfg.first_index = max((p.index for p in existing), default=0) + 1
        self.out(f"\n{chosen.path} pasará de {_mib(chosen.size_mib)} a "
                 f"{_mib(shrink.new_size_mib)}, dejando "
                 f"{_mib(shrink.freed_mib)} para Castalia.")
        return True

    def _confirm_text(self, cfg: InstallConfig, disk: DiskInfo,
                      existing: list[PartitionInfo]) -> None:
        """What is about to happen, in the words that matter."""
        self.out("")
        if cfg.mode == MODE_WHOLE_DISK:
            self.out(f"ATENCIÓN: se borrará TODO el contenido de {disk.path}.")
            for part in existing:
                self.out(f"  se pierde: {part.describe()}")
        elif cfg.mode == MODE_SHRINK:
            assert cfg.shrink is not None
            self.out(f"Se modificará {cfg.shrink.partition.path} para hacer "
                     f"sitio. Sus datos se conservan.")
            for part in existing:
                if part.path != cfg.shrink.partition.path:
                    self.out(f"  se conserva: {part.describe()}")
            self.out("ATENCIÓN: haz una copia de seguridad antes de "
                     "redimensionar. Ninguna operación sobre particiones "
                     "es gratis.")
        else:
            self.out("No se modificará ninguna partición existente.")
            for part in existing:
                self.out(f"  se conserva: {part.describe()}")

    def _ask_password(self) -> str:
        """Twice, and never echoed. Empty means "leave it unset"."""
        while True:
            first = self.secret("Contraseña para tu usuario "
                                "(vacío = sin contraseña): ")
            if not first:
                return ""
            again = self.secret("Repítela: ")
            if first == again:
                return first
            self.out("  Las contraseñas no coinciden.")

    # ---- the flow ---------------------------------------------------------

    def run(self) -> int:
        self.out("== Instalador de Castalia OS (modo texto) ==")
        if not self.disks:
            self.out("No se encontró ningún disco donde instalar.")
            return 2

        disk = self._choose_disk()
        if disk is None:
            return 2

        existing = self.probe_partitions(disk.path)
        # Measuring how full a filesystem is walks the whole volume, so it is
        # done once, here, only for the disk actually chosen.
        used = self.probe_used(existing) if existing else {}

        mode = self._choose_mode(disk, existing, used)
        if mode is None:
            return 2

        hostname = self._ask("Nombre del equipo", "castalia")
        username = self._ask("Nombre de usuario", "usuario")
        full_name = self._ask("Tu nombre", "Usuario de Castalia")

        cfg = InstallConfig(
            target_disk=disk.path, hostname=hostname, username=username,
            full_name=full_name, ram_mib=self.ram_mib, mode=mode,
            source_root=self.source_root, mount_root=self.mount_root,
        )
        if mode == MODE_ALONGSIDE and not self._configure_alongside(
                cfg, disk, existing):
            return 2
        if mode == MODE_SHRINK and not self._configure_shrink(
                cfg, disk, existing, used):
            return 2

        problems = cfg.validate(disk)
        if problems:
            for p in problems:
                self.out(f"  error: {p}")
            return 2

        password = self._ask_password()
        plan = build_plan(cfg, disk.size_mib, set_password=bool(password))
        self.out("\nSe realizarán estos pasos:")
        for i, step in enumerate(plan.steps, 1):
            mark = " (destructivo)" if step.destructive else ""
            self.out(f"  {i:2}. {step.title}{mark}")

        self._confirm_text(cfg, disk, existing)
        confirm = self._ask(f"Escribe {disk.path} para confirmar")
        try:
            execute(plan, self.runner, confirm_disk=confirm,
                    secrets={"password": password},
                    log=lambda m: self.out(f"  … {m}"))
        except ConfirmationRequired:
            self.out("Confirmación incorrecta — instalación cancelada.")
            return 3
        except StepFailed as exc:
            # The tool's own words, not a traceback. Someone whose install
            # stopped halfway is owed the reason on the screen they are
            # looking at — there is no log viewer here yet.
            self.out(f"\n✘ Ha fallado un paso: {exc}")
            self.out("El sistema no está instalado. Nada más se ha "
                     "modificado.")
            return 4

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

    if os.geteuid() != 0:
        print("El instalador necesita privilegios de root.")
        return 2
    installer = TextInstaller(
        probe_disks(), input, print, SubprocessRunner(),
        ram_mib=(ram_kib // 1024) or 2048,
    )
    return installer.run()


if __name__ == "__main__":  # pragma: no cover
    raise SystemExit(main())
