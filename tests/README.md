# tests/ — QA & certification suites

Testing is a first-class subsystem: the FLOOR promise is only credible if it is
tested. See [`docs/PROJECT_BIBLE.md` §19](../docs/PROJECT_BIBLE.md#19-qa-and-hardware-certification)
and the enforced budgets in [§16](../docs/PROJECT_BIBLE.md#16-performance-budgets).

## One command

```sh
sh tests/run.sh          # quick: lint + unit + design/legal/pipeline gates
sh tests/run.sh full     # + shell build, all-theme renders, live E2E
sh tests/run.sh iso      # + REAL ISO build & QEMU boot (root, slow)
```

`tests/run.sh` runs the same tiers CI runs (see the script header for the
tier list); `make -C build test-all` is the same thing.

## Contents

| Path | Status | Purpose |
|------|--------|---------|
| `run.sh` | ✅ | The tiered one-command QA runner (lint/unit/gates/build/render/e2e/iso) — what CI runs, locally |
| `apps.manifest` | ✅ | The canonical app list: one row per shipped app with its live-run and offscreen-render flags. The render gate, the live E2E and the `.deb` packaging all iterate this file, so an app cannot ship untested (kept in sync with the ISO hook by a unit test) |
| `offscreen/render-all.sh` | ✅ | Renders every manifest app (plus panel, Start Menu and desktop planes) in all six themes via `QT_QPA_PLATFORM=offscreen`; any crash/hang/empty frame fails (§8, §19) |
| `e2e/apps-live.sh` | ✅ | **End-to-end app suite:** real Xvfb + Openbox + panel + desktop, then every manifest app must map an EWMH window, stay alive, and tear down on SIGTERM; the panel must survive all 25 windows' churn (§9, §19) |
| `e2e/session-smoke.sh` | ✅ | **End-to-end session:** runs the REAL `castalia-session` entry point (the one the ISO's `startx` uses) and asserts WM up → desktop+dock mapped → demo window → supervision (a killed panel respawns) → clean SIGTERM logout (§6.6, §7.1) |
| `live/desktop-smoke.sh` | ✅ | Runs the real shell under a genuine Xvfb + Openbox session and asserts the panel's EWMH taskbar (`_NET_CLIENT_LIST`) reflects exactly the open app windows — proves live window management, not just offscreen rendering (§7.2) |
| `qemu/boot-smoke.sh` | ✅ | Boots a Castalia ISO headless in QEMU (FLOOR defaults: 1 vCPU, 512 MB, TCG) and asserts it reaches userspace by matching a serial-console marker (§19.1) |
| `qemu/screenshot.py` | ✅ | Boots a *graphical* ISO with a real VGA and captures the framebuffer to PNG via the QEMU monitor (`screendump`) — proves the desktop renders (§18 Phase 2) |
| `../installer/tests/qemu-install.sh` | ✅ | End-to-end installer proof: runs the full `castalia-install` (partition → GRUB in chroot → user) to a loopback disk from a real bootable Debian, then boots that disk in QEMU and asserts userspace — proves the installer produces a bootable system (§14) |
| `ui/` | planned | 800×600 + 1024×768 screenshot/layout diffs (no clipped controls) |
| `compat/` | planned | Wine/DOSBox/ScummVM app-profile regression tests |
| `unit/` | via `tools/tests/` | Per-app/library unit tests (also run in each package build) |

## Boot smoke test

```sh
# build a bootable live ISO, then boot it headless and assert userspace
sh build/mkiso.sh --edition live-amd64
sh tests/qemu/boot-smoke.sh build/out/iso/castalia-live-amd64.iso
```

The `live-amd64` edition is a lean, genuinely bootable image (kernel +
live-boot + SysVinit, the §6.4 fallback init) used to prove the Phase 1
pipeline end to end. It boots via isolinux → live-boot → SysVinit with an
autologin root console on `ttyS0`, so the boot is capturable headlessly.

## Live desktop smoke test

```sh
# build the shell, then drive it through a real WM and assert the taskbar
cmake -S shell -B build/out/shell-build -DCMAKE_BUILD_TYPE=Release
cmake --build build/out/shell-build -j"$(nproc)"
sh tests/live/desktop-smoke.sh --shot /tmp/live-desktop.png
```

This is the integration counterpart to the offscreen render smoke. It starts
a real X server (Xvfb), the actual Openbox window manager Castalia ships, the
real `castalia-panel`, and three genuine first-party windows, then reads the
window manager's live `_NET_CLIENT_LIST` and asserts the taskbar model — the
managed windows that are neither the dock (panel) nor the desktop (wallpaper)
— is exactly those three. It exercises the same EWMH code path the panel uses
at runtime (`shell/panel/src/WindowList.cpp`), so a regression in live window
tracking fails CI, not just a pixel diff.

## Reference machines

- **FLOOR:** QEMU 1 vCPU cap, 512 MB, std VGA, IDE, 800×600 (TCG, no KVM).
- **TARGET:** QEMU 2 vCPU, 2 GB, 1024×768.

## Release blockers (§19.4)

Never ship with an open `sev:data-loss`, `sev:brick`, `type:legal`, or a FLOOR
budget regression on `tier:floor`.
