"""Turn an :class:`~model.InstallConfig` into an ordered, inspectable plan.

A plan is a plain list of :class:`Step` objects. Nothing here runs commands or
touches a disk — :mod:`engine` does that. Because planning is a pure function
of (config, disk), the exact partitioning, mkfs, copy, fstab and bootloader
steps are asserted directly in the unit tests (§14 non-negotiables as code).
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Callable

from .model import (
    ALIGN_MIB,
    BOOT_MIB,
    FIRST_128_GIB_MIB,
    MODE_ALONGSIDE,
    MODE_SHRINK,
    InstallConfig,
    Partition,
    ShrinkPlan,
    partition_path,
)

MiB = 1024 * 1024

# Where a Castalia system keeps its boot assets, staged there by
# packages/mkdeb.sh and by the desktop ISO hook (see iso/grub/README.md).
GRUB_ASSETS = "/usr/share/castalia/grub"

#: Castalia's GRUB settings go in a drop-in that grub-mkconfig sources after
#: /etc/default/grub, so they win without erasing the source image's own.
GRUB_DROPIN = "/etc/default/grub.d/50-castalia.cfg"

#: Copy the gfxmenu theme into /boot and bake the .pf2 fonts it names. Every
#: line is fail-open and the script always exits 0 — see the call site.
GRUB_THEME_SH = f"""set -u
src={GRUB_ASSETS}/theme
dst=/boot/grub/themes/castalia
[ -d "$src" ] || exit 0
mkdir -p "$dst" || exit 0
cp -a "$src/." "$dst/" 2>/dev/null || exit 0
command -v grub-mkfont >/dev/null 2>&1 || exit 0
ttf=/usr/share/fonts/truetype/dejavu
grub-mkfont -s 14 -o "$dst/dejavu_14.pf2"  "$ttf/DejaVuSans.ttf"      2>/dev/null || :
grub-mkfont -s 12 -o "$dst/dejavu_12.pf2"  "$ttf/DejaVuSans.ttf"      2>/dev/null || :
grub-mkfont -s 20 -o "$dst/dejavu_20b.pf2" "$ttf/DejaVuSans-Bold.ttf" 2>/dev/null || :
exit 0
"""

#: Put the Safe Mode and recovery generators where grub-mkconfig will run
#: them. Fail-open, like everything else in the boot-assets phase: a missing
#: generator must never turn a successful install into a failed one.
GRUB_SAFE_ENTRY_SH = f"""set -u
for gen in 11_castalia_safe 12_castalia_recovery; do
    src={GRUB_ASSETS}/$gen
    [ -f "$src" ] || continue
    install -Dm755 "$src" "/etc/grub.d/$gen" 2>/dev/null || :
done
exit 0
"""

#: Put the hardware probe where runit will find it, and run it once against
#: the machine being installed onto (§6.15: "runs at install and first boot").
#:
#: Installing it means two things: the definition in /etc/sv, and a symlink
#: from the runsvdir, which is what runit reads as "enabled at boot". The
#: runsvdir is looked for in the order runit itself looks, because Debian and
#: Void disagree about where it is, and getting it wrong means a service that
#: is installed and never runs.
#:
#: The probe itself is then run ONCE, here, in the chroot — so the very first
#: boot already has a report to show rather than an empty Hardware Center.
#: --root / inside the chroot is the machine, not the live ISO: the chroot's
#: /sys and /proc are bind-mounts of the real ones (the plan mounts them
#: above), which is the same hardware the installed system will boot on.
#:
#: Fail-open throughout. Not knowing what is in the machine must never be
#: what stops it being installed.
HWPROBE_SH = f"""set -u
src={GRUB_ASSETS.rsplit('/', 1)[0]}/services/castalia-hwprobe
[ -d "$src" ] || exit 0
mkdir -p /etc/sv/castalia-hwprobe/log || exit 0
cp -a "$src/." /etc/sv/castalia-hwprobe/ 2>/dev/null || exit 0
chmod 755 /etc/sv/castalia-hwprobe/run /etc/sv/castalia-hwprobe/log/run \
    2>/dev/null || :
for d in /etc/service /var/service /service /etc/runit/runsvdir/default; do
    [ -d "$d" ] || continue
    ln -sfn /etc/sv/castalia-hwprobe "$d/castalia-hwprobe" 2>/dev/null || :
    break
done
command -v castalia-hwprobe >/dev/null 2>&1 || exit 0
castalia-hwprobe --quirks /usr/share/castalia/hwprobe/quirks.json \
    >/dev/null 2>&1 || \
    echo "castalia: the hardware probe did not complete; the Hardware" \
         "Center will read the machine live instead" >&2
exit 0
"""

#: Rebuild the initrd so it carries the recovery console.
#:
#: This has to run in the chroot AFTER the recovery hook is in place and
#: BEFORE grub-mkconfig, because the recovery menu entry points at an initrd
#: that has to already be able to honour castalia.recovery=1. An entry that
#: boots an initrd without the console in it is a recovery option that drops
#: the user into a normal boot of the system they are trying to recover.
#:
#: Fail-open, but loudly: if update-initramfs is not there (a target that is
#: not initramfs-tools based), the recovery entry will simply do nothing, and
#: that is better than refusing to finish an install over it.
RECOVERY_INITRD_SH = """set -u
command -v update-initramfs >/dev/null 2>&1 || {
    echo "castalia: no update-initramfs; recovery console not baked in" >&2
    exit 0
}
[ -x /etc/initramfs-tools/hooks/castalia-recovery ] || {
    echo "castalia: recovery hook missing; skipping initrd rebuild" >&2
    exit 0
}
update-initramfs -u -k all || update-initramfs -u || :
exit 0
"""

# Directories never copied from the live root onto the target.
COPY_EXCLUDES = (
    "/dev/*", "/proc/*", "/sys/*", "/run/*", "/tmp/*", "/mnt/*", "/media/*",
    "/lost+found", "/target", "/target/*", "/swapfile",
    "/var/cache/apt/archives/*.deb", "/live", "/run/live",
)


@dataclass
class Step:
    """A single install action.

    Exactly one of *argv* / *write* is set. *argv* is a command to run;
    *write* is a ``(path, render)`` pair where ``render(ctx)`` returns the
    file's text (computed at execute time so it can use probed UUIDs).
    """

    title: str
    argv: list[str] | None = None
    destructive: bool = False
    chroot: bool = False
    write: tuple[str, Callable[[dict], str]] | None = None
    sensitive: bool = False  # redact args when logging (passwords)
    stdin_key: str | None = None  # feed a secret (by key) to this step's stdin
    stdin_text: str | None = None  # feed fixed text to this step's stdin

    def describe(self) -> str:
        if self.write is not None:
            return f"write {self.write[0]}"
        assert self.argv is not None
        if self.sensitive:
            shown = "<redacted>"
        else:
            # An inline `sh -c` script is many lines of shell. Printed as-is
            # it swamps the plan it is one line of, so it is folded onto one
            # line here — the plan is a table of contents, and the scripts
            # themselves are constants in this file, under their own names.
            shown = " ".join(
                f"<{len(a.strip().splitlines())}-line shell script>"
                if "\n" in a else a
                for a in self.argv)
        prefix = "[in target] " if self.chroot else ""
        return prefix + shown


@dataclass
class Plan:
    config: InstallConfig
    partitions: list[Partition]
    steps: list[Step] = field(default_factory=list)

    @property
    def boot(self) -> Partition:
        return next(p for p in self.partitions if p.role == "boot")

    @property
    def swap(self) -> Partition:
        return next(p for p in self.partitions if p.role == "swap")

    @property
    def root(self) -> Partition:
        return next(p for p in self.partitions if p.role == "root")


def build_layout(cfg: InstallConfig, disk_size_mib: int) -> list[Partition]:
    """Guided whole-disk layout: /boot, swap, / (msdos, old-BIOS safe)."""
    swap_mib = cfg.resolved_swap_mib()
    start = ALIGN_MIB
    boot_end = start + BOOT_MIB
    # /boot must live inside the first 128 GiB (§6.2).
    assert boot_end <= FIRST_128_GIB_MIB
    swap_end = boot_end + swap_mib
    parts = [
        Partition(1, "boot", start, boot_end, "ext4", "castalia-boot"),
        Partition(2, "swap", boot_end, swap_end, "swap", "castalia-swap"),
        Partition(3, "root", swap_end, disk_size_mib, "ext4", "castalia-root"),
    ]
    return parts


def build_alongside_layout(cfg: InstallConfig) -> list[Partition]:
    """Lay /boot, swap and / inside the free region, touching nothing else.

    Same three partitions as the guided layout, placed in the gap the probe
    found instead of across the whole disk, and numbered on from whatever the
    existing table already uses. Root takes the remainder of the region — not
    the remainder of the disk, which is the difference between installing
    beside somebody's Windows and installing over it.
    """
    swap_mib = cfg.resolved_swap_mib()
    start = cfg.free_start_mib
    boot_end = start + BOOT_MIB
    # §6.2 again: a vintage BIOS has to be able to read the kernel, and free
    # space late on a big disk is exactly where that stops being true.
    if boot_end > FIRST_128_GIB_MIB:
        raise ValueError(
            f"/boot would start past the first 128 GiB ({start} MiB), where "
            f"an old BIOS may not be able to read it (§6.2)")
    swap_end = boot_end + swap_mib
    i = cfg.first_index
    return [
        Partition(i, "boot", start, boot_end, "ext4", "castalia-boot"),
        Partition(i + 1, "swap", boot_end, swap_end, "swap", "castalia-swap"),
        Partition(i + 2, "root", swap_end, cfg.free_end_mib, "ext4",
                  "castalia-root"),
    ]


#: Refuse to resize a filesystem that is mounted. Both resizers check this
#: themselves; this exists so the *reason* reaches the user as a sentence
#: rather than as a tool's exit code, and so the plan says out loud that the
#: check happens before anything is written.
NOT_MOUNTED_SH = """set -u
dev=$1
if findmnt -S "$dev" >/dev/null 2>&1; then
    echo "castalia-install: $dev is mounted; unmount it before resizing" >&2
    exit 1
fi
exit 0
"""


def build_shrink_steps(shrink: ShrinkPlan) -> list[Step]:
    """The steps that take space off an existing filesystem (§14.3, §14.5).

    **The order of the last two is the entire safety property of this
    feature.** The filesystem is shrunk first and the partition second. Do it
    the other way round and the partition boundary lands in the middle of a
    filesystem that still believes it owns the space beyond it; everything
    past the new end is then unreadable, which is to say gone. There is no
    recovery step for that and no warning before it — it simply works until
    the day somebody opens a file that lived at the far end of the volume.

    Everything before the shrink is there to make it refusable:

    1. *not mounted* — a live filesystem cannot be resized coherently;
    2. *check* — ``ntfsresize --info`` refuses a dirty volume, which is what a
       hibernated Windows or one left in Fast Startup looks like. That refusal
       is a feature: resizing under it would leave Windows resuming into a
       disk that changed shape beneath it;
    3. *rehearse* — ``--no-action`` does the whole computation and writes
       nothing, so an impossible shrink fails here rather than halfway
       through the real one.

    The filesystem is given one megabyte less than the partition will have, so
    the boundary is always outside it rather than exactly on it. Rounding in
    the resizers is in units of clusters and blocks, not megabytes, and the
    cushion means we never have to be right about which way they round.
    """
    part = shrink.partition
    dev = part.path
    tool = part.shrink_tool
    fs_size_mib = shrink.new_size_mib - 1
    steps = [
        Step(f"Check {dev} is not mounted",
             ["sh", "-c", NOT_MOUNTED_SH, "sh", dev]),
    ]
    if tool == "ntfsresize":
        fs_bytes = str(fs_size_mib * MiB)
        steps += [
            # --info on a dirty volume exits non-zero and says so; that is
            # the hibernation/Fast-Startup guard, and it must run first.
            Step(f"Check the filesystem on {dev} (and refuse if it is dirty)",
                 ["ntfsresize", "--info", "--force", dev]),
            Step(f"Rehearse the shrink of {dev} (writes nothing)",
                 ["ntfsresize", "--no-action", "--size", fs_bytes, dev]),
            Step(f"Shrink the filesystem on {dev} to {fs_size_mib} MiB",
                 ["ntfsresize", "--force", "--size", fs_bytes, dev],
                 destructive=True),
        ]
    else:
        steps += [
            # -f because resize2fs refuses without a recent check, -p to fix
            # what is safely fixable without asking. If it cannot, we stop.
            Step(f"Check the filesystem on {dev}",
                 ["e2fsck", "-f", "-p", dev]),
            Step(f"Rehearse the shrink of {dev} (writes nothing)",
                 ["resize2fs", "-P", dev]),
            Step(f"Shrink the filesystem on {dev} to {fs_size_mib} MiB",
                 ["resize2fs", dev, f"{fs_size_mib}M"], destructive=True),
        ]
    disk = _disk_of(dev)
    steps += [
        # sfdisk, not parted. `parted -s ... resizepart` looks like the
        # obvious tool and cannot be used: shrinking raises "Shrinking a
        # partition can cause data loss, are you sure?", and script mode
        # answers that prompt NO and exits 1. There is no documented flag
        # that changes it. The loopback smoke found this the only way it can
        # be found — by running it on a disk.
        #
        # sfdisk -N edits one entry and nothing else, and giving it only a
        # size leaves the start sector exactly where it was. That last part
        # is the safety property: a partition that keeps its start cannot
        # have moved onto anything, whatever else went wrong.
        Step(f"Shrink partition {part.index} to {shrink.new_size_mib} MiB",
             ["sfdisk", "--force", "-N", str(part.index), disk],
             stdin_text=f"size={shrink.new_size_mib}MiB\n",
             destructive=True),
        Step("Re-read the partition table", ["partprobe", disk],
             destructive=True),
    ]
    # Verify afterwards, not just before. A resizer that returned 0 having
    # produced a filesystem that no longer checks is the failure this is for,
    # and finding it now — while the user is still at the installer, before
    # anything has been written into the freed space — is the difference
    # between "we stopped" and "we told you months later".
    verify = ([tool, "--info", "--force", dev] if tool == "ntfsresize"
              else ["e2fsck", "-f", "-p", dev])
    steps.append(
        Step(f"Verify the shrunk filesystem on {dev} still checks out",
             verify))
    return steps


def _disk_of(part_path: str) -> str:
    """``/dev/sda1`` -> ``/dev/sda``; ``/dev/nvme0n1p2`` -> ``/dev/nvme0n1``."""
    stripped = part_path.rstrip("0123456789")
    if stripped.endswith("p") and stripped[:-1] and stripped[-2].isdigit():
        return stripped[:-1]
    return stripped


def render_fstab(ctx: dict) -> str:
    """Produce /etc/fstab from probed UUIDs (ctx['uuids'][role])."""
    u = ctx["uuids"]
    lines = [
        "# /etc/fstab — generated by the Castalia installer",
        "# <file system>  <mount>  <type>  <options>  <dump>  <pass>",
        f"UUID={u['root']}  /      ext4  errors=remount-ro  0  1",
        f"UUID={u['boot']}  /boot  ext4  defaults           0  2",
        f"UUID={u['swap']}  none   swap  sw                 0  0",
    ]
    return "\n".join(lines) + "\n"


def render_default_grub(ctx: dict) -> str:
    """Produce /etc/default/grub.d/50-castalia.cfg — the boot identity (§6.2).

    These are the knobs ``grub-mkconfig`` reads, and its output is the only
    menu an installed machine ever sees. Setting them here, rather than
    shipping a hand-written ``grub.cfg``, is what makes the menu survive a
    kernel update: the entries are regenerated against whatever kernel is
    actually on /boot, instead of pointing at a path that used to exist.

    A DROP-IN, not ``/etc/default/grub`` itself. grub-mkconfig sources the
    main file and then every ``/etc/default/grub.d/*.cfg``, so Castalia's
    settings win without deleting the ones the image it was installed from
    had already made. That is not a detail: the first version of this wrote
    the main file, and the install-and-boot test caught it wiping the serial
    console the source image had configured — the machine came up and could
    not be heard. Anything Castalia does not name here is left alone, and
    **GRUB_CMDLINE_LINUX is deliberately not named**: it is where an image
    puts ``console=``, and it belongs to whoever built that image.
    """
    return "\n".join([
        "# Castalia OS — GRUB settings (Bible §6.2).",
        "# A drop-in: grub-mkconfig sources /etc/default/grub first and then",
        "# this, so these win while everything else in that file survives.",
        "# Written by the installer; update-grub re-reads it on every kernel",
        "# update. Edit and re-run update-grub to change the menu.",
        "",
        "# §6.2: short timeout, last-booted remembered.",
        "GRUB_DEFAULT=saved",
        "GRUB_SAVEDEFAULT=true",
        "GRUB_TIMEOUT=4",
        "GRUB_TIMEOUT_STYLE=menu",
        "",
        'GRUB_DISTRIBUTOR="Castalia OS"',
        'GRUB_CMDLINE_LINUX_DEFAULT="quiet splash"',
        "",
        "# The gfxmenu theme (iso/grub/theme). GRUB ignores GRUB_THEME when the",
        "# file is not there, so a build without the theme still boots plainly.",
        "GRUB_GFXMODE=1024x768,800x600,640x480,auto",
        "GRUB_GFXPAYLOAD_LINUX=keep",
        "GRUB_THEME=/boot/grub/themes/castalia/theme.txt",
        "",
        "# §14.3: an OS that was already on this machine gets its own entry.",
        "# This is the boot-menu half of dual-boot; the disk half is",
        "# --mode alongside and --mode shrink.",
        "GRUB_DISABLE_OS_PROBER=false",
        "",
    ])


def render_hostname(ctx: dict) -> str:
    return ctx["config"].hostname + "\n"


def render_hosts(ctx: dict) -> str:
    host = ctx["config"].hostname
    return (
        "127.0.0.1\tlocalhost\n"
        f"127.0.1.1\t{host}\n"
        "::1\tlocalhost ip6-localhost ip6-loopback\n"
    )


def build_plan(
    cfg: InstallConfig,
    disk_size_mib: int,
    *,
    configure_target: bool = True,
    set_password: bool = False,
) -> Plan:
    """Full guided install plan for *cfg* against a disk of *disk_size_mib*.

    ``configure_target=False`` stops after the copy + identity files, omitting
    the chroot phase (bootloader, timezone, user). That subset needs only a
    real block device — not a bootable rootfs — so the loopback smoke can
    exercise the genuine destructive path (partition/format/mount/copy/fstab)
    against a disk image on any machine.
    """
    # A shrink install IS an alongside install, with one act before it: the
    # neighbour gives up its tail and the gap that opens is the region the
    # layout goes into. Everything after that point is identical, and it is
    # identical on purpose — the property that matters ("nothing is written
    # outside the free region") is one property, tested once, not two.
    alongside = cfg.mode in (MODE_ALONGSIDE, MODE_SHRINK)
    parts = (build_alongside_layout(cfg) if alongside
             else build_layout(cfg, disk_size_mib))
    plan = Plan(cfg, parts)
    disk = cfg.target_disk
    mnt = cfg.mount_root
    boot = plan.boot
    swap = plan.swap
    root = plan.root
    bpart = partition_path(disk, boot.index)
    spart = partition_path(disk, swap.index)
    rpart = partition_path(disk, root.index)

    s = plan.steps.append

    # 0. Make the room, if that is what was asked for. This is the only part
    # of any install that modifies a filesystem the user already had, so it
    # is first, it is separable, and it verifies itself before the installer
    # is allowed to write a single byte into what it freed.
    if cfg.mode == MODE_SHRINK:
        assert cfg.shrink is not None, "shrink mode without a shrink plan"
        for step in build_shrink_steps(cfg.shrink):
            s(step)

    # 1. Partition table + partitions (DESTRUCTIVE — gated in the engine).
    if not alongside:
        # Guided whole-disk: a fresh label, which is what erases the machine.
        s(Step(f"Create MS-DOS partition table on {disk}",
               ["parted", "-s", disk, "mklabel", "msdos"], destructive=True))
    else:
        # Alongside: the existing table stays exactly as it is. There is no
        # mklabel here and there must never be one — it is the single command
        # that would turn "install next to Windows" into "install over
        # Windows", and it is worth this many words to say so.
        s(Step(f"Keep the existing partition table on {disk} "
               f"(installing into {root.start_mib - BOOT_MIB - swap.size_mib}"
               f"–{root.end_mib} MiB)",
               ["parted", "-s", disk, "print"]))
    s(Step("Create /boot partition",
           ["parted", "-s", disk, "mkpart", "primary", "ext4",
            f"{boot.start_mib}MiB", f"{boot.end_mib}MiB"], destructive=True))
    s(Step("Mark /boot bootable",
           ["parted", "-s", disk, "set", str(boot.index), "boot", "on"],
           destructive=True))
    s(Step("Create swap partition",
           ["parted", "-s", disk, "mkpart", "primary", "linux-swap",
            f"{swap.start_mib}MiB", f"{swap.end_mib}MiB"], destructive=True))
    # Whole-disk takes everything left. Alongside stops dead at the end of the
    # free region, because what comes after it is somebody else's partition —
    # EXCEPT when the region runs to the end of the disk, where "100%" is both
    # the same span and the only spelling parted accepts. Naming the last MiB
    # explicitly is one byte past the last addressable one and parted refuses
    # it, which is how the loopback test found this.
    root_end = "100%"
    if alongside and root.end_mib < disk_size_mib:
        root_end = f"{root.end_mib}MiB"
    s(Step("Create root partition",
           ["parted", "-s", disk, "mkpart", "primary", "ext4",
            f"{root.start_mib}MiB", root_end],
           destructive=True))
    s(Step("Re-read the partition table",
           ["partprobe", disk], destructive=True))

    # 2. Filesystems.
    s(Step("Format /boot (ext4)",
           ["mkfs.ext4", "-F", "-L", boot.label, bpart], destructive=True))
    s(Step("Initialise swap",
           ["mkswap", "-L", swap.label, spart], destructive=True))
    s(Step("Format root (ext4)",
           ["mkfs.ext4", "-F", "-L", root.label, rpart], destructive=True))

    # 3. Mount the target and activate swap.
    s(Step("Mount root", ["mount", rpart, mnt]))
    s(Step("Create /boot mount point", ["mkdir", "-p", f"{mnt}/boot"]))
    s(Step("Mount /boot", ["mount", bpart, f"{mnt}/boot"]))
    s(Step("Enable swap", ["swapon", spart]))

    # 4. Copy the live system onto the target.
    rsync = ["rsync", "-aHAXS", "--numeric-ids"]
    for ex in COPY_EXCLUDES:
        rsync += ["--exclude", ex]
    rsync += [f"{cfg.source_root}/", f"{mnt}/"]
    s(Step("Copy the system to disk", rsync))

    # 5. System identity files.
    s(Step("Write /etc/fstab", write=(f"{mnt}/etc/fstab", render_fstab)))
    s(Step("Write /etc/hostname",
           write=(f"{mnt}/etc/hostname", render_hostname)))
    s(Step("Write /etc/hosts", write=(f"{mnt}/etc/hosts", render_hosts)))

    # 6. Bind mounts for chroot, then bootloader + user in the chroot.
    if configure_target:
        for d in ("dev", "dev/pts", "proc", "sys"):
            src = "/" + d
            s(Step(f"Bind-mount /{d}",
                   ["mount", "--rbind", src, f"{mnt}/{d}"]))
        s(Step("Install GRUB to the disk",
               ["grub-install", "--target=i386-pc", "--recheck", disk],
               destructive=True, chroot=True))
        # Castalia's boot identity, applied through the hooks grub-mkconfig
        # actually reads. All of it has to land BEFORE the menu is generated.
        s(Step("Write the Castalia GRUB settings (boot identity, §6.2)",
               write=(f"{mnt}{GRUB_DROPIN}", render_default_grub)))
        # The theme and the Safe Mode generator travel with the system — the
        # .deb and the ISO hook both stage them into /usr/share/castalia/grub
        # — so this only has to move them into place. Neither is needed to
        # boot, and both are fail-open: a missing theme must never turn a
        # successful install into a failed one.
        s(Step("Install the Castalia GRUB theme",
               ["sh", "-c", GRUB_THEME_SH], chroot=True))
        s(Step("Install the Safe Mode and recovery boot entries (§6.2)",
               ["sh", "-c", GRUB_SAFE_ENTRY_SH], chroot=True))
        s(Step("Bake the recovery console into the initrd (§18 P5)",
               ["sh", "-c", RECOVERY_INITRD_SH], chroot=True))
        s(Step("Install and run the hardware probe (§6.15)",
               ["sh", "-c", HWPROBE_SH], chroot=True))
        s(Step("Generate GRUB config",
               ["grub-mkconfig", "-o", "/boot/grub/grub.cfg"], chroot=True))
        # Make the installed system boot by root FS UUID, not the install-time
        # device path: grub-mkconfig can bake in the current device name (e.g.
        # the loopback /dev/loopNp3, or /dev/sda3 that may re-order), which then
        # fails to boot as a different device. Rewrite any root=/dev/… to
        # root=UUID=… using the UUID of whatever is mounted at / in the target.
        s(Step("Make GRUB boot by UUID (device-independent)",
               ["sh", "-c",
                'u=$(blkid -s UUID -o value "$(findmnt -no SOURCE /)"); '
                '[ -n "$u" ] && sed -i '
                '"s@root=/dev/[A-Za-z0-9]*@root=UUID=$u@g" '
                '/boot/grub/grub.cfg || true'],
               chroot=True))
        s(Step("Set the timezone",
               ["ln", "-sf", f"/usr/share/zoneinfo/{cfg.timezone}",
                "/etc/localtime"], chroot=True))
        s(Step(f"Create user {cfg.username}",
               ["useradd", "-m", "-s", "/bin/bash", "-c", cfg.full_name,
                cfg.username], chroot=True))
        s(Step("Add user to sudo/audio/video groups",
               ["usermod", "-aG", "sudo,audio,video,netdev,plugdev",
                cfg.username], chroot=True))
        # Set the password INSIDE the chroot, while /target is still mounted
        # (fed on stdin so it never touches argv/logs). Must precede unmount.
        if set_password:
            s(Step(f"Set the password for {cfg.username}",
                   ["chpasswd"], chroot=True, sensitive=True,
                   stdin_key="password"))

    # 7. Unmount everything (leave the disk clean & bootable).
    s(Step("Deactivate swap", ["swapoff", spart]))
    unmounts = ("sys", "proc", "dev/pts", "dev", "boot", "") if configure_target \
        else ("boot", "")
    for d in unmounts:
        target = f"{mnt}/{d}".rstrip("/")
        s(Step(f"Unmount {target}", ["umount", "-lf", target]))

    return plan
