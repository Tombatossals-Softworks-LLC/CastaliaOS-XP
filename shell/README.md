# shell/ — Castalia desktop shell (Qt5/C++17)

The product's face: a set of **cooperating processes** over Openbox, so a crash
in one piece never blacks out the desktop. See
[`docs/PROJECT_BIBLE.md` §7](../docs/PROJECT_BIBLE.md#7-desktop--shell-architecture)
and [§12](../docs/PROJECT_BIBLE.md#12-programming-languages-and-frameworks).

## Planned contents

| Path | Component | Role |
|------|-----------|------|
| `libcastalia-ui/` | ✅ started | ThemeTokens (C++ token parser) + Mark (native brand art); the widget/style/help contract grows here (§12.3) |
| `libcastalia-sys/` | Shared library | Thin wrappers over udisks/polkit/eudev/runit/apt |
| `session/` | ✅ skeleton | POSIX-sh supervisor: theme chain, Openbox→panel order, restart-on-crash, clean logout |
| `panel/` | ✅ PoC | Taskbar, launch menu, window list, tray host, clock — builds, renders all seven themes offscreen in CI |
| `desktop/` | ✅ PoC | Azure Bay wallpaper (SVG source), selectable icons w/ shadowed labels, right-click menu, per-process Explorer launch; --panel-png composes the full-desktop shot |
| `menu/` | Launch menu | The "Castalia Menu" (corner launcher + search) |
| `panel/src/TrayHost.*`, `panel/src/XEmbedTray.*` | ✅ tray | StatusNotifierItem (D-Bus) **and** the X System Tray Protocol, independent of each other |
| `panel/src/Switcher.*` | ✅ Alt+Tab | Castalia's own switcher — MRU order, family icons; grabs the keys itself, so `openbox-rc.xml` must not bind them |
| `explorer/` | ✅ PoC | Real QFileSystemModel browsing, places sidebar, history, icon/list views, Castalia icon family |

## Contract

Every process is runit-supervised and restarts on crash. The shell must fit the
idle-RAM budget with the compositor **off** and never block the UI thread on
disk/network I/O (§7.12, §16). All UI inherits `libcastalia-ui`.

Toolkit: **Qt 5.15 LTS + C++17**, built with **CMake**.

## Phase 2 status — boots to the graphical desktop

The `live-desktop-amd64` ISO (built by `build/mkiso.sh` with the `build/hooks/desktop-amd64.sh` chroot hook) boots under Xorg + Openbox straight into the Castalia desktop: `castalia-session` starts the WM, then `castalia-desktop` (wallpaper + icons, a `_NET_WM_WINDOW_TYPE_DESKTOP` window) and `castalia-panel` (a `_NET_WM_WINDOW_TYPE_DOCK` window). The shell is compiled in-chroot against the target's Qt (correct ABI). Verified in QEMU — see `docs/evidence/phase2-desktop-live.png`.
