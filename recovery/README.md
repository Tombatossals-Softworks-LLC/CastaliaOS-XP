# recovery/ — Restore Points and the recovery boot environment

Two halves of the same promise (P8, "Recoverable"; Bible §9, §18 Phase 5,
§23.7 #4): a way to snapshot the system before something risky, and somewhere
to run the restore from when the system will not start.

## Restore Points (`castalia_recovery/`)

Space-efficient hardlinked rsync snapshots that work on plain ext4 — no btrfs
required, because the FLOOR machine will not have it. `/home` is **never**
captured: a Restore Point reverts the *system*, and rolling back somebody's
documents to fix a bad update would be a far worse bug than the one it fixed.

    castalia-restore create [--label L] [--reason R]
    castalia-restore list
    castalia-restore restore ID --confirm
    castalia-restore prune

`snapshot_argv`/`restore_argv` are pure functions and unit-tested; a real
tmpdir smoke (`tests/snapshot-smoke.sh`) proves the whole loop — snapshot,
break the system, restore, verify. Restoring auto-takes a `pre-restore` point
first, so a restore is itself reversible.

## The recovery boot environment (`boot/`)

| File | Installed as | What it is |
|---|---|---|
| `castalia-recovery-console` | `/usr/lib/castalia/recovery/…` | the menu, POSIX sh |
| `init-premount` | `/etc/initramfs-tools/scripts/init-premount/castalia-recovery` | runs the console when `castalia.recovery=1` is on the cmdline |
| `initramfs-hook` | `/etc/initramfs-tools/hooks/castalia-recovery` | puts the console and its tools into every initrd |
| `../../iso/grub/12_castalia_recovery` | `/etc/grub.d/12_castalia_recovery` | the "Recuperación" menu entry |

The menu offers: restore a point, check and repair the disk, repair the boot
menu, open a shell, reboot.

### Three decisions, and why

**It runs at `init-premount`, not `init-bottom`.** init-bottom runs after
initramfs-tools has already mounted the real root — and if that mount fails,
initramfs-tools drops to its own bare emergency shell and never reaches
init-bottom at all. "The root filesystem will not mount" is the central case a
recovery environment exists for, so the console has to run *before* that,
mount the root itself, and be the thing that handles the failure.

**There is no separate `initrd-recovery.img`.** The Phase 5 deliverables list
asks for a "recovery initramfs" and the obvious reading is a second image.
This is the same initrd gated on a kernel argument, which is better on three
counts: a separate image has to be rebuilt on every kernel update or it
silently rots into an image that cannot boot the kernel it is offered next to;
it doubles what /boot has to hold on a machine where /boot is 1 GiB; and it is
a second code path exercised only in the emergency, which is the worst place
to discover it drifted.

**The console does not reimplement restoring.** It `chroot`s into the mounted
system and runs the installed, tested `castalia-restore`. If the thing that
restores a point were not the same code that took it, one of them would be
wrong and nobody would find out which until it mattered. A test asserts the
console contains no `rsync` and no `--link-dest`.

### How it is tested

`tests/test_console.py` executes the console as a program, piping menu choices
at it with `CASTALIA_RECOVERY_DRYRUN=1`, and asserts the exact commands it
would run — `fsck` on the device and only after an `umount`, `grub-install` on
the disk rather than the partition, the kernel filesystems bound and unbound
around `update-grub`. `tools/tests/test_bootbg.py` runs the GRUB generator for
real against a stub library, on every failure path. And
`installer/tests/qemu-install.sh` installs a real system and then checks the
recovery console is inside the initrd the recovery entry actually boots —
because an entry that offers recovery and then performs an ordinary boot of
the broken system is a promise kept badly, at the moment somebody is relying
on it.
