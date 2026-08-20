# installer/ — Graphical + text installer

A **Qt5 graphical installer** and an **ncurses text fallback**, both driving the
**same Python backend** so logic is shared and testable. See
[`docs/PROJECT_BIBLE.md` §14](../docs/PROJECT_BIBLE.md#14-installer-and-first-boot-experience).

## Install modes (§14.3)

| Mode | What it does | Status |
|---|---|---|
| `--mode whole-disk` (default) | Writes a fresh partition table over the target. Erases everything. | ✅ |
| `--mode alongside` | Puts /boot, swap and / in **unallocated space**, leaves every existing partition exactly where it is, and does not replace the partition table. GRUB picks up the other OS through `os-prober` (see `iso/grub/README.md`). | ✅ |
| shrink an existing partition to make room | — | ❌ not implemented; `alongside` is offered only when there is already free space |
| manual partitioning | — | ❌ not implemented |

`alongside` refuses itself rather than improvising: too small a gap, no room
left in the msdos table for three more primaries, or a /boot that would land
past the first 128 GiB (§6.2) each mean the mode is not offered at all.

Proof it does what it says: `installer/tests/test_alongside.py` asserts the
plan (no `mklabel`, nothing written outside the free region, the existing
partition never named), and `installer/tests/alongside-smoke.sh` runs the real
engine against a real loopback disk carrying a real filesystem full of data,
and checksums it before and after. §23.7 #3 asks for "verified"; that is what
verified looks like.

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
| `tests/` | ✅ | 49 unit tests + the real-disk loopback smoke |

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
