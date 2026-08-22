# installer/ — Graphical + text installer

A **Qt5 graphical installer** and an **ncurses text fallback**, both driving the
**same Python backend** so logic is shared and testable. See
[`docs/PROJECT_BIBLE.md` §14](../docs/PROJECT_BIBLE.md#14-installer-and-first-boot-experience).

## Install modes (§14.3)

| Mode | What it does | Status |
|---|---|---|
| `--mode whole-disk` (default) | Writes a fresh partition table over the target. Erases everything. | ✅ |
| `--mode alongside` | Puts /boot, swap and / in **unallocated space**, leaves every existing partition exactly where it is, and does not replace the partition table. GRUB picks up the other OS through `os-prober` (see `iso/grub/README.md`). | ✅ |
| `--mode shrink` | Takes space off an existing NTFS or ext2/3/4 filesystem **first**, then installs into the gap that opens. This is what makes "install next to Windows" true on a normal computer, where there is no free space to begin with. | ✅ |
| manual partitioning | — | ❌ not implemented |

The modes are offered least-destructive-first, which matters because whatever
is at the top is what most people pick: `alongside` when the disk already has
a gap, `shrink` when it does not, `whole-disk` last.

Both refuse themselves rather than improvising. For `alongside`: too small a
gap, no room left in the msdos table for three more primaries, or a /boot that
would land past the first 128 GiB (§6.2). For `shrink`, additionally:

- a filesystem there is no resizer for (anything but NTFS and ext2/3/4) —
  refused by name, never attempted hopefully;
- a filesystem whose used space could not be measured — an installer that
  guesses how full a Windows is is an installer that eats one;
- a shrink that would leave the neighbour with less than **4 GiB or 15% of
  what is in it**, whichever is more. Windows stops working long before it is
  literally full, and the person who agreed to make room did not agree to
  that;
- a mounted filesystem, or an NTFS volume that is dirty — which is what a
  hibernated Windows and one left in Fast Startup look like.

**The order inside a shrink is the whole safety property**: the filesystem is
resized before the partition is, never the other way round. Reversed, the new
partition boundary lands inside a filesystem that still believes it owns the
space past it, and everything out there is gone with no error at the time.
`test_shrink.py` asserts that ordering directly, for both resizers.

Proof it does what it says, on real disks rather than only on paper:

| Test | What it proves |
|---|---|
| `tests/test_alongside.py` | the alongside plan writes no `mklabel`, nothing outside the free region, and never names the existing partition — and `fstab` names only partitions the installer created |
| `tests/test_shrink.py` | every refusal above, and that the filesystem shrinks before the partition |
| `tests/alongside-smoke.sh` | the real engine against a real loopback disk with a real filesystem full of data, checksummed before and after |
| `tests/shrink-smoke.sh` | the same, on a disk with **no free space at all**, against real NTFS, with a file deliberately placed out past where the new boundary falls — the byte a mis-ordered shrink destroys |

§23.7 #3 asks for "verified"; that is what verified looks like.

## Contents

| Path | Status | Purpose |
|------|--------|---------|
| `castalia_installer/model.py` | ✅ | `InstallConfig` + `DiskInfo`, partition sizing, validation — pure data |
| `castalia_installer/plan.py` | ✅ | `build_plan(config, disk)` → an ordered, inspectable list of `Step`s (no side effects) |
| `castalia_installer/engine.py` | ✅ | executes a plan through a `Runner` boundary, behind the §14.5 confirmation gate |
| `castalia_installer/probe.py` | ✅ | disk **and partition** discovery via `lsblk` — what is already on the target, so §14.3 can be honoured (both parsers pure/tested) |
| `castalia_installer/tui.py` | ✅ | the text installer — the guaranteed fallback (§14.5 #5) |
| `castalia_installer/__main__.py` | ✅ | CLI: `--dry-run`, `--copy-only`, `--confirm-erase DISK` |
| `gui/` | ✅ | Qt5 wizard (`castalia-instalador`) driving the backend |
| `tests/` | ✅ | 125 unit tests + three real-disk loopback smokes (install, alongside, shrink) |

## Try it

```sh
# Print the exact, ordered plan for a 40 GiB disk — touches nothing:
PYTHONPATH=. python3 -m castalia_installer \
    --disk /dev/sda --disk-size-mib 40960 --hostname pc-castalia \
    --user dave --dry-run
```

A real run refuses every destructive step unless `--confirm-erase /dev/sda`
names the exact target disk — the code form of non-negotiable #1 below.

```sh
# The text installer (graphics-free fallback, §14.5 #5). On the live ISO this
# is `castalia-instalar-texto`; it drives the same backend and safety gate.
sudo PYTHONPATH=. python3 -m castalia_installer.tui
```

## Proof

* **Unit tests** (`PYTHONPATH=. python3 -m unittest discover -s tests`) assert
  the guided layout (§14.3), the safety gate, UUID fstab generation, chroot
  prefixing, NVMe partition naming, and that the plan issues **no network
  command** (offline-capable, #4).
* **Loopback smoke** (`sudo sh tests/loopback-smoke.sh`) runs the *real* engine
  — parted, mkfs.ext4, mkswap, mount, rsync, UUID fstab — against a loopback
  disk image, then re-mounts it and checks the copied tree, preserved
  permissions, and that the fstab UUIDs match `blkid`. Latest run:
  [`tests/last-loopback-evidence.txt`](tests/last-loopback-evidence.txt). The
  smoke passes `--copy-only`, leaving the chroot phase to the test below.
* **End-to-end install + boot** (`sudo sh tests/qemu-install.sh`) runs the
  *full* engine — including the chroot phase (`grub-install`, `grub-mkconfig`,
  a UUID-root fixup, user creation) — against a loopback disk built from a real
  bootable Debian, then **boots that disk in QEMU** and asserts it reaches
  userspace on its own. Latest run:
  [`tests/last-qemu-install-evidence.txt`](tests/last-qemu-install-evidence.txt)
  (`PASS — Debian GNU/Linux 12 pc-castalia ttyS0`). The definitive proof that
  `castalia-install` produces a bootable system.

## Non-negotiables (§14.5)

1. Never destroy data without an explicit typed confirmation.
2. Always leave a bootable path (preserve existing OS entries on dual-boot).
3. Prove the display works (15-s auto-revert) before committing a mode.
4. Complete a full install **offline**.
5. Fall back to text UI when graphics fail — never a dead end.

The installer always writes **Safe Mode** and **Recovery** GRUB entries (§6.2,
§6.13) and offers the **Migration Assistant** on first boot (§9).
