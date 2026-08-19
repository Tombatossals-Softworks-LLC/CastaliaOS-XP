# Castalia OS — "Human" edition worklog

A running log of the Human-edition work, so anyone picking the project up can
get to speed fast: **what** was built, **how** it was built and verified, and
the **conventions** to keep following. Newest context at the top of each
section.

> **Workflow:** develop on a branch and open a PR; never push to `main`. Every
> push is verified locally against a real Qt 5.15 build first; CI runs 14
> checks on top.

## The ask (original)

Make the project graphically resemble **Ubuntu 7.04 "Human"** (orange/brown),
use **Tangerine/Tango icons**, add animations, make it faster and more useful,
improve the installer, create a login banner — "the GOAT XP-killer". Then:
press kit, keep following the roadmap, and a marketing screencast that looks
like a real user using the software.

## Current state (keep these numbers in sync!)

- **43 first-party apps**, **39 tangerine icons** (48 px grid), **7 themes**,
  **9 Xcursor pointers** (`themes/cursors/src/` → `tools/cursor_gen.py`).
- Themes, in display order (`themeRank` in `shell/libcastalia-ui/Theme.cpp`):
  **human** (flagship default), classic, azul, oliva, plata, medianoche
  (dark), high-contrast (WCAG AAA).
- Human = chocolate titlebars (`#5A473B`→`#3E3028`), sun-orange accent
  (`#F57900`), warm paper surface (`#EFEBE7`), "Human Dawn" wallpaper.
- Default shipped theme is **human** (`packages/mkdeb.sh`,
  `build/hooks/desktop-amd64.sh` write `/etc/castalia/theme.conf` = human;
  openbox rc.xml → `Castalia-human`).

## What the Human edition added, by area

### Theme + art
- `themes/human/theme.conf` — the flagship palette (passes `theme_lint`).
- **Tangerine/Tango icon family**: restyled the original 17 icons (gradient
  fills, tango outlines, orange folders) and added new ones. All original,
  provenance-tracked. Reuse existing icons where possible (e.g. media-player,
  search) to avoid icon-count churn.
- **Per-theme wallpapers** (6): `branding/wallpapers/` — azure-bay (default),
  human-dawn (human), marine-deep (azul), olive-dusk (oliva), silver-morning
  (plata), midnight-keep (medianoche). Wired via the optional
  `[assets].wallpaper` token in each `theme.conf`; SCHEMA.md documents it.
- Login: `branding/login/banner.svg` (dawn roundel + wordmark) +
  `services/lightdm/lightdm-gtk-greeter.conf` (Human dawn greeter), staged by
  mkdeb + the hook (services/ added to live profiles' `SRC_DIRS`).

### Shell (shell/…)
- `libcastalia-ui`: added `castalia::reduceMotion()` (true under
  `CASTALIA_REDUCE_MOTION=1` or `QT_QPA_PLATFORM=offscreen`) and
  `castalia::themeIcon(repo, name)`. Human leads `themeRank`.
- **Start menu** (`shell/panel/src/CastaliaMenu.cpp`): every entry has its
  48 px icon; the menu rises 6 px + fades in on open (≤150 ms, skipped under
  reduceMotion). Categories: Accesorios, Juegos, Sistema, Compatibilidad,
  **Accesibilidad**.
- **Panel** (`CastaliaPanel.cpp/.h`, `main.cpp`): real quick-launch buttons
  (Explorer, Ayuda) with icons; **minute-aligned clock** (`ClockLabel`,
  clickable → opens **Calendario**); tray **speaker button** (`VolBtn`) →
  opens **Control de volumen** (replaced placeholder dots).
- **Taskbar** (`WindowList.cpp`): **event-driven** via X `PropertyNotify` over
  the xcb fd + `QSocketNotifier` + 30 ms coalescing (was 1 s polling); 5 s
  safety-net timer only.
- **Desktop** (`shell/desktop/src/DesktopWindow.cpp`): icon hover halos +
  accent-coloured selection; **wallpaper precedence** = user override
  (`~/.config/castalia/desktop.conf`) → theme `[assets].wallpaper` → azure-bay,
  with a `QFileSystemWatcher` for **live reload** (no re-login).

### Installer (installer/gui/src/main.cpp)
- Determinate progress (parses the dry-run plan's step count), install-tips
  **slideshow** with crossfade, page **crossfades**, "passwords don't match"
  hint.

### New apps in this wave (all mirror the wine-manager pattern)
1. **castalia-clasicos** (Juegos clásicos) — DOSBox-X / ScummVM launcher.
2. **castalia-calendario** — month view + per-day notes (autosave to
   `~/.local/share/castalia/calendario`); Spanish `QLocale`. Opens from clock.
3. **castalia-teclado** — on-screen keyboard (accessibility); non-focus-stealing
   (`WA_ShowWithoutActivating`, NoFocus keys), types via `xdotool` (XTEST).
4. **castalia-multimedia** — media playlist that delegates to mpv/VLC/mplayer.
5. **castalia-volumen** — mixer over `pactl`/`amixer`; opens from tray speaker.
6. **castalia-buscar** — recursive file search in a **QThread worker**
   (streaming, cancellable, capped at 2000).
7. **castalia-fechahora** (Fecha y hora) — Control-Panel Date & Time applet:
   live Spanish clock, IANA time-zone picker (`QTimeZone`, no process), NTP
   sync + set-timezone over `timedatectl` (honest read-only fallback when
   systemd is absent), and a saved 12/24-hour display pref. Reuses the
   `clock` icon (no new provenance row). Menu: Sistema group.
8. **castalia-usuarios** (Cuentas de usuario) — user-accounts applet: lists
   real human accounts from `/etc/passwd` (never `/etc/shadow`), highlights
   the current session (`getuid()`), shows full name / UID / home / shell /
   groups (`id -Gn` for the session's own supplementary set). Honest single
   write: "Cambiar mi contraseña" hands off to `passwd` in `castalia-terminal`
   (own account only; button disabled if the terminal isn't present). **New
   `users` icon** (two figures; provenance row added; icon count 36→37).
9. **castalia-papelera** (Papelera de reciclaje) — the Recycle Bin, to the
   **freedesktop.org Trash spec**: reads `$XDG_DATA_HOME/Trash/{files,info}`,
   lists each item's original path / deletion date / size, and restores,
   deletes-for-good or empties. Interops with any spec-compliant file manager.
   `--demo` shows a read-only sample bin (no disk touched) for the render/live
   gates. Reuses the `trash` icon (no new provenance). Menu: Sistema group.
10. **castalia-impresoras** (Impresoras) — CUPS printers panel: lists printers
    with status + default (`lpstat -p/-d`), shows the print queue (`lpstat -o`),
    sets the user default (`lpoptions -d`, ~/.cups/lpoptions), cancels a job
    (`cancel`) and refreshes. Honest "sin CUPS" state when lpstat is absent.
    `--demo` shows a sample setup. Reuses the `printer` icon. Menu: Sistema.
11. **castalia-redes** (Conexiones de red) — read-only network status over
    iproute2: interfaces with type/state/IPv4/IPv6/MAC (`ip -o link`, `ip -o
    addr`), host name (`QSysInfo::machineHostName`) and default gateway (`ip
    route`). Never mutates the network. Honest "sin ip" state when iproute2 is
    absent. `--demo` shows sample interfaces. Reuses the `network` icon. Menu:
    Sistema group. (Parser verified against real `ip` output on a live box.)
12. **castalia-acerca** (Acerca de Castalia) — the About box: renders the
    `branding/logo/castalia-mark.svg` (links `Qt5::Svg`), the product name and
    version (compiled from CMake `PROJECT_VERSION` → the canonical `VERSION`,
    so it can't drift), and honest system facts — kernel/arch (`QSysInfo`),
    CPU + RAM (`/proc/cpuinfo`, `/proc/meminfo`), Qt/shell, host name — plus
    studio + contact. Reuses the `computer` icon; pinned near "Centro de
    ayuda" in the menu (no `--demo` needed; renders anywhere).
13. **castalia-predeterminados** (Programas predeterminados) — default-apps
    panel to the freedesktop standard: per category (navegador, correo, editor,
    imágenes, audio, vídeo, archivos) shows the current default (`xdg-mime
    query default`) and a combo of installed handlers (scanned from the
    applications dirs' `.desktop` MimeType). Setting one runs `xdg-mime
    default` → user's own `~/.config/mimeapps.list` (no privilege). Honest
    "sin xdg-utils" state. `--demo` shows a sample. Reuses the `package` icon.
- Control Center gained a **wallpaper picker** on the Pantalla page (SVG
  thumbnails → writes `desktop.conf`; links `Qt5::Svg`).
### Sound scheme (the audible half of the identity)
The WAVs and `tools/sound_gen.py` predate this work; what was missing is
that **nothing ever played them**. Now they are wired to real events:
- **`shell/libcastalia-ui/Sound.{h,cpp}`** — the canonical playback side:
  a `Sound` enum (compile error on a typo, not a silent no-op), `soundId`,
  `soundPath`, `soundsEnabled()` and fire-and-forget `playSound()`. It picks
  `paplay` → `pw-play` → `aplay`, and stays **silent rather than failing**
  when there is no audio stack.
- **Policy** (identical in both implementations): off when
  `CASTALIA_NO_SOUND=1`, off under `QT_QPA_PLATFORM=offscreen` (so the render
  gate never spawns audio), off when `~/.config/castalia/sound.conf` says
  `enabled = false`, off when no player exists.
- **Wired events:** `castalia-session` plays **startup** once the shell is up
  and **shutdown** at logout (with a 1 s grace so it is actually heard before
  everything is killed); **Papelera** plays **empty-trash** on "Vaciar
  papelera" and **error** on its two real failure paths.
- **Gotcha:** the session is POSIX sh and cannot link libcastalia-ui, so it
  carries a `sound_enabled`/`play_sound` pair that mirrors the same policy.
  `tools/tests/test_sound_scheme.py` asserts both honour `CASTALIA_NO_SOUND`
  so the two copies cannot drift on the part that matters.
- **`tools/tests/test_sound_scheme.py`** (13 tests) ties the four places that
  can drift: palette.toml ↔ rendered WAVs (format *and* length vs the declared
  duration) ↔ provenance rows ↔ the C++ enum/ids. The selftest additionally
  checks every enum member resolves to a shipped WAV.
- Verified behaviourally with a fake player on `PATH`: C++ and sh both play
  with a player present, both go silent for the kill switch and for
  `sound.conf`, and C++ is silent offscreen. No packaging change was needed —
  mkdeb/the hook already copy `branding/` wholesale.

### Cursor theme (the pointers)
- **`themes/cursors/src/*.svg`** — 9 original pointers on a 24 px grid (white
  body, chocolate `#3E3028` outline, sun-orange accents): left_ptr, xterm,
  watch, hand2, fleur, sb_h/v_double_arrow, crosshair, question_arrow. Each
  has a provenance row (type `cursor`).
- **`tools/cursor_gen.py`** — builds a **real Xcursor theme**: rasterises each
  SVG at 24/32/48 (**`rsvg-convert` preferred**, ImageMagick as fallback),
  decodes the PNG **in pure Python** (`decode_png_rgba` — no Pillow),
  premultiplies to ARGB32 and writes the
  Xcursor container **in pure Python** (no `xcursorgen` dependency), plus
  `index.theme` and 39 alias symlinks (`default`, `text`, `pointer`, `wait`,
  `ew-resize`, … and the hashed names some toolkits still ask for).
  `--preview` writes the press-kit contact sheet.
  - Hotspots live in `HOTSPOTS` (24-grid) and scale with the size.
  - **Gotcha:** the format writer + PNG decoder are deliberately separate from
    rasterisation so `tools/tests/test_cursor_gen.py` (21 tests) can verify
    them in CI, which has **no image tooling**. Never make the tests rasterise.
  - **Gotcha (cost us a red CI):** `imagemagick` alone **cannot read SVG** on a
    stock GitHub runner — its SVG support is a *delegate* (librsvg /
    `libmagickcore-*-extra`) that the bare package doesn't pull in, so
    `convert file.svg` exits 1. Locally it worked only because this container
    had the delegate. Fix: prefer **`rsvg-convert`** (`librsvg2-bin`) and treat
    ImageMagick as a fallback; CI's distribution job installs `librsvg2-bin`.
    Second lesson: the first version used `capture_output=True` **without
    printing stderr**, so the CI log showed only "exit status 1". Always
    surface a subprocess's stderr in the exception.
  - Verified locally two ways: **real libXcursor** loads all 9 files via
    ctypes (3 images each), and an ffmpeg `-draw_mouse 1` grab of a live Xvfb
    session shows the Castalia arrow actually drawn by X.
- Shipping: `packages/mkdeb.sh` installs to `/usr/share/icons/Castalia-Human`
  + writes `/usr/share/icons/default/index.theme` (`Inherits=Castalia-Human`);
  the ISO hook generates them when the image has ImageMagick, else keeps the
  stock pointers; `castalia-session` exports `XCURSOR_THEME`/`XCURSOR_SIZE`
  only when the theme is really installed. CI's **distribution** job installs
  imagemagick and runs `cursor_gen.py`, so the `.deb` genuinely ships them.
- **`tools/icon_sheet_gen.py`** — reproducible press-kit icon sheet: mirrors
  every `themes/icons/48/*.svg` into `presskit/icons/` and rebuilds
  `icon-family-sheet.png` (title band states the honest count) via ImageMagick.
  Re-run it whenever the icon family changes so the presskit stays in sync.

### Press kit + marketing (presskit/)
- `presskit/` — self-contained media kit: `index.html` (offline single page),
  fact-sheet, press-release, about (short/med/long + features), technical,
  roadmap, team, quotes-and-boilerplate, CREDITS-and-LEGAL, README.
- Assets: logos (SVG+PNG), 21 curated screenshots, 6 wallpapers @2048px,
  37-icon contact sheet, and **`presskit/video/`** — a ~63 s demo screencast
  (MP4 + GIF) of a real driven session.
- Contact everywhere: **hello@tombatossalssoftworks.com** /
  **tombatossalssoftworks.com**. Creators: **Dave Abellán** and
  **Claudio di Castello**; studio **Tombatossals Softworks**. Positioned as
  a **developer preview v0.1.0** (don't overclaim a 1.0).

## How we verify (the local workflow — do this before every push)

Qt/tools are installed in the session via apt (the env allows it):

```sh
# one-time per fresh container:
sudo apt-get install -y -qq --fix-missing qtbase5-dev libqt5svg5-dev libxcb1-dev
# for the screencast only:
sudo apt-get install -y -qq openbox xdotool ffmpeg imagemagick x11-utils wmctrl
# (apt-get update may exit 100 / 404 on some mirrors — the install still works)
```

```sh
# build:
PYTHONPATH=tools python3 tools/theme_export.py
PYTHONPATH=tools python3 tools/cursor_gen.py    # Xcursor theme (needs ImageMagick)
cmake -S shell -B build/out/shell-build -DCMAKE_BUILD_TYPE=Release
cmake --build build/out/shell-build -j"$(nproc)"

# gates (all must pass):
ruff check tools installer recovery
PYTHONPATH=tools python3 -m unittest discover -s tools/tests     # 97 tests
PYTHONPATH=tools python3 -m castalia_qa.theme_lint themes
PYTHONPATH=tools python3 -m castalia_qa.provenance .
PYTHONPATH=tools python3 tools/preview_gen.py                    # regen preview

# render every app × every theme (must be non-empty; count = apps×7 + planes):
sh tests/offscreen/render-all.sh --bindir build/out/shell-build --repo .

# packaging:
for p in build/profiles/*.conf; do sh build/mkiso.sh --edition "$(basename "$p" .conf)" --dry-run >/dev/null; done
sh packages/mkdeb.sh --bindir build/out/shell-build --repo .     # real .deb
dpkg-deb -c build/out/deb/*.deb | grep <new-binary>              # confirm packaged
```

Per-app quick checks:
- Offscreen render: `QT_QPA_PLATFORM=offscreen <bin> --theme human --repo . --screenshot /tmp/x.png` then Read the PNG.
- Xvfb map/alive/SIGTERM smoke (approximates the e2e gate):
  `Xvfb :N &; DISPLAY=:N <bin> --theme human --repo . &` → still alive after 2 s, exits on `kill -TERM`.

**What can't run locally (runs in CI):** the full Openbox **e2e**
(`tests/e2e/apps-live.sh`) needs a WM (installable), and the **Restore Points
snapshot smoke** needs `rsync` (often missing). Those are covered by CI.

## Checklist — adding a new first-party app (do ALL of these)

1. `apps/<name>/src/main.cpp` — copy `apps/wine-manager` / `apps/clasicos`:
   accept `--theme/--repo/--screenshot`; `castalia::applyTheme(&app,…)`;
   header on the titlebar gradient; on `--screenshot`,
   `QTimer::singleShot(150,…,{ w.grab().save(shot); app.quit(); })`.
2. `apps/<name>/CMakeLists.txt` (+ `find_package(Qt5 … Svg)` only if it needs SVG).
3. `apps/CMakeLists.txt` → `add_subdirectory(<name>)`.
4. `tests/apps.manifest` line: `name|apps/<name>/castalia-<bin>|<live-args>|<render-args>`.
5. Start Menu entry in `shell/panel/src/CastaliaMenu.cpp` (label, bin, icon).
6. Icon `themes/icons/48/<icon>.svg` **and** a row in
   `legal/ASSET_PROVENANCE.csv` — **or reuse an existing icon** (no new row).
7. `build/hooks/desktop-amd64.sh`: add an `install -Dm755 …/<bin> …` line
   **and** add `<bin>` to the symlink `for b in …` loop. (The
   `test_apps_manifest` unit test enforces manifest⇔hook equality — it will
   fail if you forget either side.)
8. Bump counts: README (apps; icons if new), all `presskit/*` (apps/icons),
   `tests/e2e/apps-live.sh` "churn of N windows" comment. Add the app to the
   README/about app-suite prose lists.
9. Verify: build, offscreen render, Xvfb smoke, `render-all` count grows by 7,
   the `.deb` contains the binary (and icon). Then commit + push.

## Gotchas we hit (and the fixes)

- **`QSocketNotifier::activated` on Qt 5.15**: signal is
  `activated(QSocketDescriptor,…)`, not `int`. Don't use
  `QOverload<int>::of(...)`. Connect with a **zero-arg lambda**:
  `connect(notifier, &QSocketNotifier::activated, this, [this]{ … });`
  (commit dd05a82).
- **libcastalia-ui selftest** asserts theme order — after making Human lead,
  update `shell/libcastalia-ui/tests/selftest.cpp` to expect `human` first
  and ≥7 themes (commit 3438a84).
- **manifest ⇔ hook** must match exactly (`test_apps_manifest`). Every manifest
  binary needs an `install -Dm755` line in the desktop hook (the two shell
  planes panel/desktop are the only hook-extra allowed).
- **Provenance gate** scans `branding/ themes/ iso/` only — `presskit/` assets
  (PNG/MP4/GIF) are exempt, no rows needed.
- `windowclose` (xdotool) may not close a Qt window; use `pkill -f castalia-<x>`.
- `xdotool search --sync` can hang forever if the window never appears; poll
  with a timeout instead.
- **Unpinned `ruff` in CI drifts red on unchanged code.** `pip install ruff`
  (no version) fetched a newer ruff whose *default* rule set had grown to
  include isort (`I001`), flake8-executable (`EXE001`) and unused-noqa
  (`RUF100`) — 42 "new" errors on code that was green the day before. You
  can't fix it in the source: dropping `# noqa: E402` to satisfy RUF100
  immediately trips E402 (and back). Fix is determinism, not code edits —
  pin the rule *selection* in `ruff.toml` (`select = ["E4","E7","E9","F"]`,
  the classic pyflakes + pycodestyle baseline the tree satisfies) **and** pin
  `ruff==0.15.8` in `ci.yml` + `release.yml` (commit 8048ee6). Rule of thumb:
  every lint/format tool in CI must be version-pinned and config-pinned.

## Marketing screencast

`tools/make-screencast.sh` records a **real** session (Xvfb + Openbox with the
Castalia decorations + shell + apps; xdotool drives it; ffmpeg captures).
Storyboard (refreshed 2026-08-18, ~63 s): Start menu **and typing to search
it** → paint in Pintura → play Buscaminas → **Alt+Tab** through the open
windows → Calendario from the clock, with a note → a **notification toast**
from the real server → the **Centro de redes** listing Wi-Fi → Control Center
wallpaper pick → live re-skin on the bare desktop. Symlink built `castalia-*`
binaries onto `PATH` so the panel's own launches (clock→calendar, tray→volume,
menu items) work. Output:
`build/out/screencast/castalia-os-demo.mp4` + one frame per second, and with
`--gif` a two-pass palette GIF. The names match `presskit/video/`, so a
refresh is a copy rather than a rename.

## Roadmap — done vs. next

**Done in the Human rework:** Human theme + icons + animations + faster event-driven
taskbar + greeter/banner + installer polish + press kit + 6 new apps
(clasicos, calendario, teclado, multimedia, volumen, buscar) + wallpaper
picker with live reload + marketing screencast. Completes the roadmap's
"media / DOSBox-X / ScummVM launcher" trio and advances accessibility beyond
High Contrast.

**Done in the 2026-07-31 audit:** the §7 shell gaps — session/power
dialog (§7.6, and the dead power row it fixes), Start Menu search (§7.3), Run
dialog + the global keyboard map + `castalia-open` (§7.7) — plus the two §9.2
apps §10 still owed: Visor de registros and Servicios del sistema. Four new
apps (39→42), one system-wide theme fix (clipped default buttons), and five
new head-less self-tests wired into CI.

**Done in the 2026-08-14 polish pass:** the wallpaper raster cache
(~15× cheaper desktop repaints) + icon memoisation, the glass panel/menu
(token-derived, High-Contrast-exempt) and the missing Start Menu hover state,
wallpaper crossfade / launch ring / rubber-band selection, the Aurora
(easter egg **and** fourth screensaver scene), and two new gates
(`castalia-desktop --selftest`, `tools/tests/test_flourishes.py`).

**Candidate next steps (all pure-QtWidgets, verifiable), in the order the
audit would take them:**
1. ~~**Notifications (§7.4)**~~ — **shipped** (2026-08-14): `castalia-
   notificaciones`, a real `org.freedesktop.Notifications` server with corner
   toasts, a capped history and per-app mute. Left for 1.0: action buttons, a
   "do not disturb" inhibit, and a tray indicator once the tray hosts anything.
2. ~~**A real system tray (§7.4)**~~ — **both halves shipped**: SNI
   (2026-08-14) and **XEmbed** (2026-08-18). The panel is the
   StatusNotifierWatcher and host *and* the X tray manager, so indicators
   arrive whether the application speaks D-Bus or the 1990s protocol — and a
   session with no bus at all still gets a working tray.
3. ~~**Recent documents (§7.9)**~~ — **shipped for the menu** (2026-08-14):
   `castalia::recent` over the freedesktop XBEL store, the last eight in the
   Start Menu, covered by the search, clearable. Left: Explorer's own
   "Recientes" place, and more apps recording (Pintura, Multimedia).
4. ~~**Alt+Tab switcher restyled (§7.6)**~~ — **shipped** (2026-08-18):
   Castalia's own switcher, not Openbox's restyled — MRU order, family icons,
   the panel owns the key grab. Left for later: thumbnails on compositor-on
   tiers.
5. ~~Remaining §9 apps: **Hardware Center**, **Disk Manager**, **Migration
   Assistant**~~ — **all three shipped** (2026-08-18): `castalia-hardware`,
   `castalia-discos`, `castalia-migrar`. ~~Network Center beyond `redes`'s
   read-only status~~ — **also shipped** (2026-08-18): Wi-Fi connect, DHCP or
   static per connection, and a status light in the panel tray. §9 has no
   unbuilt rows left.
6. Broader localisation beyond the Spanish default.
7. Screencast variants (dark theme, 9:16 vertical, short wallpaper-switch GIF)
   — and a refresh, since the storyboard predates four apps and the search box.

## Conventions (quick reference)

- Spanish-first UI. Original art only; nothing Microsoft; provenance enforced.
- Animations ≤200 ms and gated by `castalia::reduceMotion()`.
- Commit messages: a short subject and a descriptive body that says *why*, not
  just what. Work on a branch and open a PR; never push to `main`.
- Watch CI after opening a PR: the ISO and installer jobs are the slow ones and
  the ones most worth reading when they go red.

## 2026-07-31 — the shell's missing pieces

An audit of the tree against the bible found the shell had **three §7 features
the parity map already claims** but nothing implemented, plus one outright
defect. Working through them here.

### 1. The power row was decorative — now it works (§7.6) ✅

`CastaliaMenu.cpp` built **Bloquear / Cerrar sesión / Apagar** with
`menuItem(...)` and added them to the layout **without a single `connect`** —
three dead buttons, and no way at all to leave the session from the shell.

- **`apps/salir` → `castalia-salir`** (39th app) — the session/power dialog the
  bible specifies: six big tiles (Apagar · Reiniciar · Suspender · Cerrar
  sesión · Bloquear · Cambiar de usuario) with **natively painted glyphs**
  (no per-action assets), a confirmation page for the destructive three, and
  Esc to back out at any point.
- **Honest availability**: every action is *resolved* against what the machine
  really has — `loginctl`/`systemctl`/`poweroff`/`shutdown` for power,
  `/sys/power/state` for suspend (a machine that doesn't declare `mem` doesn't
  get the tile), a real locker (`xsecurelock`, `slock`, `i3lock`, `xtrlock`,
  `xscreensaver-command`, …), `dm-tool` for switch-user. Unavailable tiles are
  disabled and **say why** instead of failing on click.
- **Logout is exact**: `castalia-session` now exports `CASTALIA_SESSION_PID`,
  and the dialog verifies that pid via `/proc/<pid>/comm` before signalling it
  — its existing `TERM` trap does the clean teardown (farewell sound included).
  Falls back to `pkill -TERM -x castalia-session`, then `openbox --exit`.
- **Panel wiring**: "Bloquear" runs `--action bloquear` (non-destructive, no
  dialog); "Cerrar sesión" and "Apagar" open the dialog with `--focus <id>` so
  the confirmation always happens in one place.
- **`--print-actions`** prints the resolved table (`id|si·no|command`) — the
  honest way to see what a given machine can do. It and `--action` run under
  `QCoreApplication`, so **they work with no display** (a `QApplication` would
  abort on a tty).

### 2. Fixed: every default button in the OS clipped long labels

Found while testing the confirmation page: "Sí, cerrar sesión" rendered as
"Sí, cerrar sesió|". Not an app bug — `tools/theme_export.py` gave
`QPushButton:default` a `font-weight: bold`, and **a stylesheet font change is
not accounted for in a widget's size hint**: the button lays out at
normal-weight width, then paints bold. Every default button in every dialog,
in all seven themes, was one long label away from clipping — against §7.11's
"dialogs designed to fit 800×600 with no clipped buttons".

Fix: the default action is now signalled by the accent border **plus a warm
accent tint** (`default_top`/`default_bottom` = surface mixed 10/16 % toward
the accent) and no weight change. Verified with a three-button probe: the
default and non-default renderings of the same label are now the same width.

### 2b. The Start Menu search box (§7.3) ✅

§7.3 asks for a "search box at the top [that] filters apps, settings and recent
documents as you type. Keyboard-first: press launch-key, type, Enter" — and the
§10 parity map already claimed *"Start menu — Equivalent — search built in"*.
There was no search box. Now there is:

- `QLineEdit` above the scrolling roster; filters live over the 40 app entries
  **and the four pinned system items**, hiding any category heading whose
  entries all disappear, with a "Sin resultados." state.
- **Accent-folding** (`searchFold`): the label is lowercased and its combining
  marks stripped, so `diagnos` finds "Diagnóstico del sistema". The category
  name is folded into the needle too, so `juegos` surfaces the games.
- **Enter launches the first match** (and does nothing when the box is empty,
  rather than launching whatever happens to be first). The box clears and
  takes focus on every open.
- Names only, never the disk — §7.8 keeps menu search instant on the floor
  tier. *Recent documents (§7.9) are still missing and are the next piece.*
- Menu height 472 → 508 px for the new row; re-verified it still fits an
  800×600 screen with the panel (§7.11).
- "Salir de Castalia" joined the Sistema group so the power dialog is
  searchable too.
- Live proof: typed `calculadora` + Enter under Xvfb/Openbox → the panel
  spawned `castalia-calc --repo … --theme human` and its window mapped.

### 2c. Run dialog + the global keyboard map (§7.7) ✅

§7.7 asks for two things that did not exist: a **Run** dialog, and a **global
keyboard map** — "the OS is fully operable keyboard-only". The shipped
`rc.xml` had a `<theme>` block and *no `<keyboard>` section at all*.

- **`apps/ejecutar` → `castalia-ejecutar`** (40th app). One field takes four
  kinds of thing, so the routing is the app: `planFor()` is a **pure function**
  turning typed text into a `Plan` (kind · argv · plain-Spanish explanation),
  and the dialog shows that explanation **live under the field**, so you always
  know what Enter will do before you press it.
  - URL (`http/https/ftp/mailto/magnet`, or a bare `www.` host, which gets a
    scheme) → `xdg-open`; existing directory → `castalia-explorer --path`;
    `.exe/.msi/.bat/.com` → Wine (§11); other existing file → `xdg-open`;
    otherwise a command line resolved on `PATH`, with quote-aware splitting.
  - Honest failure: an unknown name says *"No se encontró «x»"* and Aceptar
    stays disabled — it never spawns and hopes.
  - History (20, most-recent-first, de-duplicated) in
    `~/.config/castalia/ejecutar.conf`, with **"Borrar historial"** (P7).
  - `--print-plan TEXT` exposes the same function head-lessly.
- **`shell/session/openbox-rc.xml`** — the rc.xml is now a **file in the repo**
  instead of a heredoc inside the ISO hook, and *both* `packages/mkdeb.sh` and
  the hook install it, so a live image and an installed system cannot diverge.
  Bindings: `⊞R` Run · `⊞L` lock · `⊞E` Explorer · `⊞T` terminal · `⊞F` search
  · `⊞D` show desktop · `⊞Esc` power dialog · `Print` screenshot ·
  `C-A-Supr` monitor · `F1` help · `A-F4`/`A-Tab`/`A-S-Tab`/`A-F7`/`A-F9`
  windows · media keys → the mixer.
- **`shell/session/castalia-open`** — the missing one-liner. Apps take
  `--repo`/`--theme`; the panel fills them from its environment, but a hotkey
  (or a `.desktop` file, or a script) would launch them bare and they would
  come up unthemed and unable to find their icons. `castalia-open <app>`
  resolves both exactly as `castalia-session` does and `exec`s the app.
  Shipped by mkdeb and the hook.
- **Gated, not just written**: `tools/tests/test_openbox_rc.py` (13 tests)
  parses the XML and asserts the §7.7 bindings exist, no key is bound twice,
  the named theme really ships, **every launched binary is in
  `tests/apps.manifest`**, everything goes through `castalia-open`, and both
  shipping paths install the same file (the hook is explicitly forbidden from
  containing its own `<openbox_config>` again).
- **Head-less self-tests, wired into CI** next to the games' `--selftest`:
  `castalia-ejecutar --selftest` (routing, quoting, temp-dir path cases, and
  the invariant that a runnable plan always carries a command) and
  `castalia-salir --selftest` (the six actions in order, available-xor-
  explained, destructive ⇒ confirmation text, and that a bogus
  `CASTALIA_SESSION_PID` is refused).
- Live proof: under a real Openbox started with this rc.xml, `Super+R` spawned
  `castalia-ejecutar --repo /home/user/… --theme classic` and mapped its
  window — the hotkey, the helper and the theme resolution all in one shot.
- **Gotcha that cost a red CI (and would have broken every install):** the
  first version shipped the file to **`/etc/xdg/openbox/rc.xml`**. That path
  is *owned by the openbox package*, so `apt-get install ./castalia-desktop.deb`
  aborts the whole transaction —
  `dpkg: trying to overwrite '/etc/xdg/openbox/rc.xml', which is also in
  package castalia-desktop`. Local `mkdeb` + `dpkg-deb -c` never sees it,
  because the conflict only appears when dpkg unpacks **both** packages.
  Fix: ship to `$REPO/openbox/rc.xml` (our own tree) and have
  `castalia-session` pass `openbox --config-file` — which also *improves*
  §7.7's "all rebindable", since a user's own `~/.config/openbox/rc.xml` now
  takes precedence and we simply stand aside. Pinned by
  `test_never_shipped_into_the_openbox_package_path`. **Rule of thumb: never
  write a file another Debian package owns; put it in the Castalia tree and
  point the tool at it.** Verified by really running
  `sudo apt-get install ./castalia-desktop_*.deb` locally with openbox
  already present (exit 0, `dpkg -S /etc/xdg/openbox/rc.xml` still says
  `openbox`), then booting the *installed* session: it logged
  `openbox config: /opt/castalia/share/castalia/openbox/rc.xml`, ran
  `openbox --config-file …`, and `Super+R` spawned the installed
  `castalia-ejecutar`.

### 2d. The two §9.2 apps the parity map still owed (§10) ✅

`docs/PROJECT_BIBLE.md` §10 lists **Event Viewer → Log Viewer** and
**services.msc → Services Manager** as *"Equivalent"*, and §9.2 gives both an
MVP scope. Neither existed. Both now do, and both were built against the
project's *own* conventions rather than inventing new ones:

- **`apps/registros` → `castalia-registros`** (Visor de registros, 41st app).
  Castalia runs runit, not systemd, so §9.2's "no binary journal" is the whole
  design: it reads plain text from `/var/log` and svlogd's
  `/var/log/<service>/current`, with no daemon in between.
  - **Never loads a whole log**: `tailLines` seeks to `size - 512 KiB` and
    drops the partial first line, so a 400 MB `messages` on a 512 MB machine
    costs one buffer (§16). The view caps at 4000 lines and *says so*
    ("sólo el final del registro").
  - `severityOf` is a **pure classifier**, so the filter, the colour and the
    counters can never disagree; error outranks warning on a line containing
    both. Colours come from the theme, so it stays legible in Medianoche and
    High Contrast.
  - Honest empty state: with no readable logs it says the logs live in
    /var/log and want administrator rights, rather than showing a blank pane.
  - Proven on real data locally: `--file /var/log/dpkg.log` tailed this
    container's own package log, 4000 of 4000 lines, 4 errors.
- **`apps/servicios` → `castalia-servicios`** (Servicios del sistema, 42nd).
  The "plain language, not raw unit files" was **already specified** in
  `services/README.md` — a `service.conf` with name/description/category/
  essential next to each runit `run` script. Nothing read it. This is that
  reader: `/etc/sv/*` for the definitions, a symlink in the runsvdir for
  "al arrancar", `sv status` for whether it is actually up.
  - A service with no `service.conf` still appears, labelled by its directory
    name and marked "sin descripción" — never hidden.
  - Essential services are bold and warn before being stopped; start/stop/
    restart go through **pkexec**, like the Software and Update centres.
  - Layout note: five columns did not fit 800×600, so the description moved
    to a detail line under the table (the `redes`/`usuarios` pattern) instead
    of being clipped (§7.11).
  - **New `services` icon** (three supervised rows with status lamps;
    provenance row added; icon count 38→39).
- Both carry a `--selftest` wired into CI. The services one **caught a real
  bug on its first run**: `sv status` prints `down: cups: 12s, normally up`,
  and the uptime token `12s,` has a trailing comma, so the parser was silently
  reporting 0 s for every stopped service. Punctuation is stripped now.

### 2e. Review round on PR #5 — three real findings

An automated review caught three things the local gates could not, all of
them genuine:

- **P1 — "Cerrar sesión" would have done nothing.** `/proc/<pid>/comm` is
  truncated by the kernel to 15 bytes (`TASK_COMM_LEN`), and
  `castalia-session` is **16** characters, so a running session always
  reports `castalia-sessio`. The pid check compared against the full name and
  therefore *never* matched, and the fallback `pkill -x castalia-session`
  cannot match either — `pgrep` itself warns "pattern that searches for
  process name longer than 15 characters will result in zero matches". On top
  of that, `startDetached` returns true for a `pkill` that matched nothing,
  so the dialog closed as if it had logged you out. Three bugs stacked into
  one silent no-op.
  Fix: identify the session from **`/proc/<pid>/cmdline`** (not truncated) via
  a pure `cmdlineIsSession()`; fall back to `pkill -TERM -f`; and run that
  fallback synchronously so a failure is reported instead of assumed. Proven
  on the installed system: `comm` really is `castalia-sessio`, the dialog
  resolves `SIGTERM <pid>`, and firing it ended the session and tore Openbox
  down (`castalia-session: session ended`).
  **Lesson: never identify a process by `comm` if its name can exceed 15
  bytes — and never let "the command started" stand in for "the command
  worked".**
- **P2 — the log viewer fabricated evidence.** With no readable logs and no
  `--demo`, it still filled the view with `demoLines()` — dated, plausible
  errors — under a header reading "sin registros". Exactly the P10 sin the
  rest of the app was written to avoid. Sample lines are now `--demo`-only;
  the live empty state stays empty and says *why* (unreadable / empty /
  filtered-out are three different messages).
- **P2 — the Run dialog opened Explorer bare.** A directory launched
  `castalia-explorer --path …` with no `--repo`/`--theme`, so on an installed
  system it came up unthemed and unable to find its icons — the very thing
  `castalia-open` exists to prevent. It now routes through `castalia-open`
  when present, with the self-test pinning both branches.

**Gotcha:** after `apt-get install`-ing the `.deb` locally
(worth doing — it is the only way to catch dpkg file conflicts), the
`libcastalia-ui` self-test starts failing `activeThemeId falls back with no
config`. That is not a regression: the package writes
`/etc/castalia/theme.conf`, which `activeThemeId` correctly reads, while the
test only overrides `$HOME`. `apt-get purge castalia-desktop` (remove is not
enough — it is a conffile) restores a clean run.

### 3. Verification notes for this work

- `--print-actions` on this container: apagar/reiniciar via `loginctl`,
  cerrar-sesión via `pkill`, suspender/bloquear/cambiar-usuario correctly
  unavailable with reasons.
- Xvfb + Openbox smoke: maps a window, survives `Return` on a destructive tile
  (goes to the confirmation page and **performs nothing**), `Esc` returns to
  the tiles, exits on SIGTERM.
- `PowerTile` needed an explicit `Return`/`Enter` handler — `QAbstractButton`
  only activates on Space, which would have left the dialog half-usable from
  the keyboard (§7.11).
- Confirmation screenshots were captured with our own `castalia-captura`
  against the live Xvfb display (dogfooding the suite).

## 2026-08-14 — the tray hosts real indicators now (§7.4)

Roadmap #2. The panel had a tray *frame* and hosted nothing; now it is a real
**StatusNotifierItem watcher and host**, so anything speaking SNI —
libappindicator/libayatana, KDE apps, `QSystemTrayIcon` on a D-Bus desktop —
gets an indicator in our panel.

`shell/panel/src/TrayHost.{h,cpp}`: the panel owns
`org.kde.StatusNotifierWatcher`, claims a host name and registers it (apps
check `IsStatusNotifierHostRegistered` before publishing anything). Left click
calls `Activate(x,y)` with the button's screen position, right click
`ContextMenu(x,y)`. Icons resolve `IconName` against our own 48 px family, then
the system theme, then the item's `IconPixmap` (a(iiay), ARGB32 network order,
largest offered) — and if all of that fails, the Castalia mark, because an
indicator you cannot see is an indicator you cannot click. `Passive` items
hide themselves; an item whose application leaves the bus is dropped by a
`QDBusServiceWatcher`.

### Three bugs, all found by running a real SNI app against it

This is the entry to read if you touch D-Bus in this tree again. None of the
three would have shown up in a unit test; all three showed up within minutes of
pointing an actual item at the panel.

1. **Segfault on the first registration.** `QDBusContext` was on the *adaptor*.
   Qt sets the call context on whatever was passed to `registerObject()`, so
   the adaptor's `message()` dereferenced a null context and took the panel
   down. It belongs on `TrayHost`, which exposes `currentCallerService()`.
2. **A deadlock, dressed up as a slow tray.** `refreshItem()` read properties
   with a *blocking* `QDBusInterface::property()` from inside the
   `RegisterStatusNotifierItem` slot — while the calling application was itself
   blocked waiting for our reply. The read sat there for the full 25 s timeout,
   the indicator came up iconless, and every other app queued behind it. All
   property reads are `GetAll` over an async pending call now (3 s timeout),
   which also means a hung indicator cannot freeze the panel.
3. **Registrations still arriving seconds apart.** Even with async reads,
   `addItem()` does bus work of its own — `AddMatch` for the item's change
   signals, watching its bus name — and doing that inside the dispatch keeps
   the caller waiting. The slot now replies immediately and queues `addItem`
   with `Qt::QueuedConnection`. Three items registering simultaneously went
   from "one now, two several seconds later" to all three instantly.

And one cosmetic: inside a stylesheet-drawn `QToolButton` the icons rendered at
about a third of their size — specks on a 24 px tray. They are rasterised to
18 px explicitly now rather than handed over as a scalable icon and hoped for.

### Gates

`castalia-panel --selftest` — the panel's first — pins the pure part:
`splitItemService()`, which turns what an application sends to
`RegisterStatusNotifierItem` into a (service, path) pair. The spec allows a bus
name, a bare object path (service = the sender) or both in one string, and
applications use all three; getting it wrong means an indicator silently never
appears. It runs before any `QApplication`, so CI needs no display.

**Live proof**: a minimal SNI item written for the purpose, three copies of it
registering at once under Xvfb + `dbus-daemon`, all three appearing in the
panel next to the volume icon and the clock, and one disappearing when its
process is killed.

### Still to do for §7.4

**XEmbed for pre-SNI applications** — X window reparenting rather than D-Bus,
and the half that old GTK2-era tray icons need. *(Done 2026-08-18; see the
session below.)*

### Verification

Build → ruff → 182 unit tests → theme_lint → provenance → 322 offscreen
renders (the panel still renders with no bus at all — a bare X session gets no
tray and no error) → ISO dry-runs → `.deb` → five C++ self-tests.

## 2026-08-14 — notifications that someone actually sends, and §7.9 recents

Two follow-ons, both small in surface and large in effect.

### 1. The notification server had nobody talking to it

A server nothing calls is a dead letterbox. `castalia::notify()` is the sending
half — and it lives in **its own static library** (`castalia-notify`) rather
than in `castalia-ui`, because linking QtDBus into all 43 apps to serve the
three that announce anything is not a trade the FLOOR tier should make.

Wired into the moments where the user has genuinely walked away:

| app | says |
|---|---|
| Papelera de reciclaje | "Papelera vaciada — 12 elemento(s) · 412 MB liberados" |
| Captura de pantalla | "Captura guardada" + the path |
| Archivos comprimidos | "Extracción terminada" / "La extracción falló" |

Verified in all three states it can be in: **no session bus** → honest `false`,
**bus but no server** → `false`, **server running** → delivered and in the
history. None of them crash, and none of them block.

### 2. Recent documents (§7.9) — roadmap #3

`castalia::recent` reads and writes **`~/.local/share/recently-used.xbel`**, the
freedesktop store. That is the whole point of using someone else's format: a
file opened in Notas appears in a GTK app's recent list and the other way
round.

- The **Start Menu leads with the last eight**, and they are covered by the
  §7.3 search — which is what that section always promised ("apps *and settings
  and recent documents*") and only half-delivered. Typing `reunion` finds
  `notas de reunión.txt`: the search folds accents, and recents are matched by
  file name *and* by the folder they live in.
- **"Vaciar la lista"** deletes the store outright (P7: a recent list you cannot
  clear is a log nobody asked for).
- Entries whose file has since disappeared are skipped, and `add()` refuses
  folders and non-existent paths.
- Notas, Escritor and the Visor de imágenes record on open.

**Two Qt bugs worth remembering**, both found by rendering the menu rather than
by reasoning about it:

1. Rebuilt entries were invisible. `show()` was being called *before*
   `addWidget()` — and re-parenting hides a widget again, so every rebuilt row
   stayed hidden.
2. With `show()` moved after, they all drew *on top of each other*: rebuilding
   widgets inside a live layout on every menu open is fragile. The section is
   now a **fixed set of eight slots plus the clear row**, created once and
   refilled — no widget churn per open, and only slots holding a document are
   searchable, so an empty slot cannot be revealed by clearing the search box.

The XBEL round trip is covered in `castalia-ui-selftest`: newest-first order,
re-opening moves rather than duplicates, missing files are skipped, folders are
refused, the cap holds, clear() really clears — and a file name containing `&`
survives, which is the classic XML-escaping bug that silently eats an entry.

### Verification

Build → ruff → 182 unit tests → theme_lint → provenance (73 assets) → 322
offscreen renders → ISO dry-runs → a 3.7 MB `.deb` → **four** C++ self-tests
(libcastalia-ui, desktop, notifications, and the two game ones), plus the menu
rendered with a seeded store, both plain and mid-search.

## 2026-08-14 — six wallpapers, de-Microsoft'd names, and the shell learns to speak

Dave committed five more wallpapers (`5b7a6d9`), four of them WebP, and asked
for names that owe Microsoft nothing — then: keep following the roadmap.

### 1. WebP does not survive contact with the shipped stack

**qtbase ships no WebP plugin.** It lives in `qt5-image-formats-plugins`, and
adding a package to the FLOOR install so a *wallpaper* can decode is not a
trade worth making. All four were converted with `dwebp` (lossless decode) and
re-encoded as JPEG; the 3.2 MB PNG went the same way after checking its
wordmark for ringing at 3× zoom. **21 MB of sources → 2.3 MB shipped.**

| shipped | was | size |
|---|---|---|
| `valle-de-castalia.jpg` | `castalia-os-xp-default.png` | 391 KiB (q92) |
| `pradera-de-castalia.jpg` | `castalia-os-xp-bliss.jpg` | 473 KiB (moved, not re-encoded — no generation loss) |
| `castalia-neon.jpg` | `cyberpunk-castalia.webp` | 493 KiB (q90) |
| `nebulosa-de-castalia.jpg` | `nebula-castalia.webp` | 476 KiB (q90) |
| `mar-de-nubes.jpg` | `nebula-castalia-2.webp` | 286 KiB (q90) |
| `castalia-minimal.jpg` | `estilizado-wallpaper-castalia.webp` | 171 KiB (q92) |

**The new default is `valle-de-castalia.jpg`** — Dave's own file was named
"default", so that is what it became: theme token, desktop fallback and greeter
all point at it. The Control Center names all six properly ("Valle de
Castalia", "Mar de nubes", …) instead of deriving a label from the file name.

### 2. `tools/tests/test_asset_hygiene.py` — so neither mistake recurs

Five tests over everything under `branding/` and `themes/`: no image format
qtbase cannot decode (WebP, AVIF, HEIC…), no unexpected file types, no
wallpaper over 2 MB, **no file name borrowing Microsoft's** (bliss, luna, aero,
windows, clippy…), and lowercase-kebab names. Two exemptions, both deliberate:
`README.md`/`SCHEMA.md` keep their shouted convention, and `themes/cursors/`
keeps the X11 spec's names (`left_ptr`, `sb_v_double_arrow` — the underscores
are not ours to change). The naming rule caught nothing new after the renames,
which is the point: it is the ratchet.

### 3. Roadmap #1 — the notification server (§7.4) 🔔

*"Nothing in the shell can currently tell the user anything."* Now it can, and
it speaks the standard, so **anything** on the system reaches the user:
`notify-send`, an apt hook, a third-party app.

**`apps/notificaciones` → `castalia-notificaciones`** (app #43), QtDBus:

- Owns `org.freedesktop.Notifications`; implements `Notify`,
  `CloseNotification`, `GetCapabilities`, `GetServerInformation` and the
  `NotificationClosed` signal. `GetCapabilities` claims only what we really do
  — no `"actions"`, because a caller that believes us would drop its own
  fallback UI and lose the interaction.
- **Toasts** stack in the corner above the panel strut, slide in from the right
  (180 ms, off under reduce-motion), expire on their own and dismiss on click.
  They never take focus (`WA_ShowWithoutActivating`, notification window type).
  Each wears the theme's accent bar — the same language the Start Menu hover
  and the active taskbar button already speak.
- **History**: every notification is appended to a capped TSV
  (`~/.local/share/castalia/notificaciones.tsv`, 200 lines) — greppable from a
  terminal. `--historial` opens the window; the Start Menu entry points there
  (roster entries grew an `args` field so one binary can appear as the right
  *task* rather than starting a second server).
- **Per-app mute** (`~/.config/castalia/notificaciones.conf`): a muted app is
  written to the history **without** a toast. Muting hides the interruption,
  never the record.
- Started by `castalia-session` after the panel, so the first toast can measure
  the strut.

**The layout bug worth remembering:** the first version positioned toasts with
`slotPosition(index, height)`, assuming a uniform height, and called `restack()`
*before* the newcomer's entrance animation — so two toasts of different heights
overlapped, and the newcomer fought its own re-stack. It is one pure function
now, `layoutFor(heights) → positions`, walking newest-to-oldest from the corner
up, and the self-test feeds it `{110, 72, 96}` precisely because equal heights
would have passed the broken version.

`--selftest` (in CI): the spec's timeout rules (−1 → default, 0 → sticky,
clamping), case-insensitive mute matching, history append/cap/newest-first plus
tab-and-newline flattening (a body with a tab would otherwise split a record),
mute round-trip, and the stack geometry.

**Verified against a real bus, not just in tests:** `dbus-daemon --session`,
the server up, then `gdbus call … org.freedesktop.Notifications.Notify` twice
from outside — exactly what `notify-send` does — with the desktop and panel
running. Both toasts appeared stacked in the corner, and
`GetServerInformation` answers *Castalia Notificaciones / Tombatossals
Softworks / 0.1.1 / spec 1.2*.

### 4. Verification

Build → ruff → **182 unit tests** (was 177) → theme_lint → provenance (73
assets) → **322 offscreen renders** (315 + the new app × 7 themes) → ISO
dry-runs → `.deb` (3.6 MB, server binary + all six wallpapers inside) → three
C++ self-tests → the live Xvfb + D-Bus session above.

## 2026-08-14 — the real artwork lands: logo PNG + photographic wallpaper

Dave committed the two assets himself (`7899c34 assets`): the logo as a
**256×280 RGBA PNG** and a **2560×1664 JPEG** for the default desktop. Both are
now wired through the whole system, and the vector mark from the previous
session steps back into a supporting role.

### The logo now has two expressions, and the size decides

| | |
|---|---|
| `branding/logo/castalia-logo.png` | **the artwork.** What the shell paints at **32 px and up**, what the press kit ships, what "the logo" means |
| `branding/logo/castalia-mark.svg` | **the vector edition.** Below 32 px, and inside the boot splash / login banner / boot-menu baker, which need geometry rather than pixels |

The threshold is not a guess. Both were rendered side by side at 16/24/32/48 px
before the line was written: the artwork has a 3 px outline, arrow-slit windows
and two turret cones, and no resampling filter keeps those at 24 px — the rim
goes soft and the windows vanish, while the vector stays crisp. That is exactly
what hand-tuned small icons are for. Progressive halving (256 → 128 → 64 → …)
before the final step got the artwork usable down to 32; one direct
256 → 24 scale was mush.

`castalia::drawMark()` handles all of it, so the ~12 apps that draw the mark did
not change a line. New in `libcastalia-ui`:

- `assetRoot(repo)` — the asset tree, resolved once: the explicit argument, then
  `CASTALIA_REPO`, then `/usr/share/castalia`, then the working directory.
- `markPixmap(size, repo)` — the artwork scaled to fit, cached per (root, size).
  The panel repaints its orb far more often than the file changes.
- `drawMarkVector()` — the geometry on its own; the fallback when there is no
  asset tree (a rescue shell, an odd working directory), so the logo degrades
  to *the same mark* instead of to a hole.

`apps/acerca` stopped loading the SVG itself and stopped linking `Qt5::Svg`
entirely — one less place with an opinion about which expression to use.

### The wallpaper is the default everywhere

`themes/human/theme.conf`, the desktop's own last-resort fallback and the
LightDM greeter all point at it now, so the login screen and the desktop show
the same picture. `tools/tests/test_brand.py` pins that they agree.

**It is decoded scaled, never in full.** `QImageReader::setScaledSize()` is set
to the covering size *before* `read()`, in both the desktop and the Control
Center's thumbnails. Measured on this JPEG:

| | peak RSS | time |
|---|---|---|
| full decode | **+17.6 MB** | 26 ms |
| scaled decode (1181×768) | **+4.7 MB** | 18 ms |

12.9 MB saved on a machine whose entire budget is 512 MB (§16). The desktop's
startup RSS is **22.6 MB with the photograph — identical to the SVG wallpaper
it replaced**, which is the point.

Everything else that touches wallpapers learned that they are not all SVG:
`ControlCenter`'s picker globs jpg/jpeg/png too and draws raster thumbnails;
`preview_gen.py` base64s non-SVG sources; `make-screencast.sh`'s `setwall` takes
a file name with its extension.

One incidental fix: `preview_gen` emitted one CSS rule per theme, so the three
themes that fall back to the default embedded the JPEG's base64 three times —
`docs/preview.html` had ballooned to 2.1 MB. Themes are grouped by wallpaper
now: **889 KiB**.

### Naming — worth a decision

The file is `castalia-os-xp-bliss.jpg`. The image is original and looks nothing
like the Microsoft photograph, but *"bliss"* is the name of the XP default
wallpaper, and this file name ships in the `.deb`, the ISO and the greeter
config of a project whose §3 rule is "nothing Microsoft". The Control Center no
longer derives its label from it (it reads **"Colinas de Castalia"** from a
small name table), but the file name itself is Dave's call. A rename is one
`git mv` plus three string updates, all pinned by the brand test.

### Verification

Build → ruff → **177 unit tests** (was 168) → theme_lint → provenance (68
assets, both new files tracked) → 315 offscreen renders → ISO dry-runs → `.deb`
(1.9 MB now, both assets inside) → both C++ self-tests. Eyeballed: the desktop
with the new wallpaper and orb, the Control Center picker, the About box.

## 2026-08-14 — the new mark: the "C monogram keep"

Dave supplied a new logo: **a stylised letter C with a castle inside it**, on
green hills, blue with a navy outline and red conical roofs. It is now the
official mark everywhere, including the Start orb.

**Redrawn, not embedded.** The reference arrived as a raster; what ships is
original vector geometry redrawn to match it, because §3/§21.2 require the
mark to be original art with a provenance row, and because one 48-grid vector
has to serve a 16 px tray icon and a 1024 px press render from the same file.

### Where the mark lives now (five expressions, one geometry)

| Where | What it is |
|-------|------------|
| `branding/logo/castalia-mark.svg` | **the master** — v0.2, geometry only |
| `castalia::drawMark()` (`Mark.cpp`) | the QPainter re-drawing: the Start orb + ~12 apps, the aurora, the installer, recovery |
| `branding/boot/splash.svg` | the boot splash, mark at 3× |
| `branding/login/banner.svg` (= `presskit/logos/castalia-wordmark-banner.svg`) | the greeter plate: the badge now *is* the dawn roundel, on a sun-orange disc |
| `tools/bootbg_gen.py` | the boot-menu background, evaluated analytically and 2×2 super-sampled in pure Python |
| `presskit/logos/*` | mark SVG (byte-identical to the master) + 1024/512/on-chocolate/banner PNGs, re-rendered |

### The geometry (so it can be reproduced)

Centre (24,24) on the 48-grid. The **C** is an annulus, outer r 22.6, inner
r 13.4, with a 70° wedge removed on the right (arms cut radially at ±35°).
The **keep** is a group at `translate(2.9 1.7) scale(0.9)`: crenellated main
tower, two conical-roofed turrets, arched door and three lit windows. The
**hills** are two quadratic curves that start and end *on the badge circle*
and close along it.

That last part matters: **Qt 5's QSvgRenderer ignores `<clipPath>`**, and it is
what paints this file across the whole shell. The first draft clipped the
hills to the badge and looked perfect in a browser and wrong in the panel.
Bounding the shapes by arcs of the badge circle is the fix — no clip needed.

### The orb

`LaunchButton::paintEvent` paints the mark at the full height of the launch
key (was a small 22 px mark with 8 px of slack), and blooms a soft white halo
behind it on hover. The QSS's left padding (32 px) is the orb's seat.

### Keeping five copies honest

New **`tools/tests/test_brand.py`** (12 tests) pins the numbers that define
the mark in every language it is expressed in: the ring path and the keep's
transform in each SVG copy, the same radii/angles/Bézier control points in
`Mark.cpp` and `bootbg_gen.py`, `presskit/logos/castalia-mark.svg` byte-equal
to the master, namespaced gradient ids in the embedded copies (an unprefixed
`id="ring"` would collide with a host document's `<defs>` and silently repaint
half the artwork), no `<clipPath>`, no fonts/rasters — and that the retired
v0.1 keep is gone from every logo file. Unit tests 154 → **168**.

### Deliberately left alone

The **wallpapers** still carry the keep on a headland, and the greeter's ridge
line still has its silhouette. That is scenery and lore, not the logo; making
six landscape paintings quote the monogram would be a redesign, not a rebrand.
Say the word if you want them redrawn too.

### Verification

Build → ruff → 168 unit tests → theme_lint → provenance (66 assets) →
`render-all` (315 renders) → ISO dry-runs → `.deb` (ships the new mark and
banner) → both C++ self-tests → a live Xvfb run of desktop + panel captured
with our own `castalia-captura`, confirming the orb on a real panel. The mark
was eyeballed at 16/24/32/48 px and at 512 px: it reads as a blue-and-green
badge at 16 px and as the full monogram from 24 px up.

## 2026-08-14 — the glass, the aurora and two real speed-ups

Brief: *audit the repo, make it faster, make it more spectacular, add
animations and easter eggs.* No new apps this round — the work went into the
two planes every session looks at all day (desktop + panel), plus one shared
piece of spectacle that earns its place by being reused.

### 1. Efficiency — two measured wins ⚡

- **The desktop re-rasterised the whole wallpaper on every repaint.**
  `DesktopWindow::paintEvent` called `QSvgRenderer::render()` directly, so
  every icon hover halo, every selection change and every rubber-band drag
  paid for the entire wallpaper again. It is baked once per (source, size)
  into `m_wall` now and blitted afterwards. Measured with a
  QSvgRenderer-vs-QPixmap micro-benchmark on `human-dawn.svg` at 1024×768:
  **4.38 ms → 0.28 ms per frame (~15×)** on this container's modern x86 — the
  gap is far wider on the §16 FLOOR tier the project targets.
  (`bakeWallpaper()`; a size change re-bakes lazily from `paintEvent`.)
- **`castalia::themeIcon()` built a fresh `QIcon` per call.** The Start Menu
  alone asks for ~50, several of them the same SVG. It is memoised per path
  now (function-local `QHash`; QIcon is implicitly shared, the shell is
  single-threaded GUI code).
- **The wallpaper watcher re-rendered on unrelated writes.** It watches the
  whole `~/.config/castalia` directory, so a theme change or a Run-dialog
  history save triggered a full re-render. `reloadWallpaper()` now returns
  early when the *resolved path* has not changed.
- **Menu search froze the roster while filtering.** `applyFilter` toggles ~50
  widgets per keystroke; it is wrapped in `setUpdatesEnabled(false/true)` so
  that is one relayout instead of fifty.

### 2. The glass — the panel and menu look lit now 🎨

All of it is **derived from the tokens**, never hard-coded, and **High
Contrast opts out** (`ThemeTokens::highContrast()`, new): gloss is exactly the
decoration §7.11 says must not cost contrast.

- `CastaliaPanel::paintEvent` lays lighting over the QSS gradient (the QSS
  still owns the base fill — a stylesheet cannot express a specular band that
  stops halfway down): a highlight band across the top half, a lit hairline at
  the top edge, a shadow above the border, and an accent bloom under the
  launch corner. The idiom matters: `style()->drawPrimitive(PE_Widget, …)`
  first, or the stylesheet background disappears.
- `panelQss()` (panel `main.cpp`) grew four-stop **glass gradients**: the
  launch key gets a lit crown and a crease at the half; quick-launch and task
  buttons are glass over the panel; the **focused window's task button is
  inverted (pressed-in) and carries an accent bar** — the first time the panel
  has actually shown which window has focus beyond a slightly lighter fill.
- **Bug found in the audit: Start Menu entries had no hover state at all.**
  `#MenuApp/#MenuPlace/#MenuAllApps` set `background: transparent` and no
  `:hover`, so pointing at an entry showed nothing. Now an accent wash plus a
  bar down the leading edge — and the bar is in the resting rule as
  `transparent`, so no label shifts by 3 px when it lights. (The padding was
  re-cut 10 → 3 px border + 7 px padding for the same reason; the first version
  clipped "Instalar Castalia OS…".)

### 3. Animations (all ≤200 ms except the flourish, all `reduceMotion()`-gated)

- **Wallpaper crossfade** — picking a wallpaper in the Control Center now
  dissolves over 200 ms instead of snapping (the old raster is freed when the
  animation ends).
- **Launch ring** — double-clicking a desktop icon sends one accent ring out
  (420 ms). On the floor tier a process can take a second to map a window; the
  desktop no longer looks like it ignored you.
- **Rubber-band selection** — dragging on bare desktop draws an accent
  selection rectangle and selects what it touches (§7.5). It did not exist.

### 4. Easter eggs 🥚 — `docs/EASTER-EGGS.md`

House rules written down there: nothing hides a feature, nothing runs
unasked, everything obeys `reduceMotion()`/`CASTALIA_NO_SOUND`.

- **The Aurora** — `↑ ↑ ↓ ↓ ← → ← → B A` on the desktop paints a night sky
  with four striated, drifting aurora curtains tinted by the *active theme's
  accent*, the keep and the studio name beneath; nine seconds, any key or
  click ends it. `castalia-desktop --easter-egg aurora` shows it on demand.
- **It is also a real screensaver scene** (`--mode aurora`, and in the Control
  Center picker) — the same `castalia::paintAurora()`. That is the design
  rule: an egg that is only an egg rots, an egg that is also a feature does
  not.
- **The entry on no menu** — typing `tombatossals` (or `aurora`, `konami`) in
  the Start Menu search reveals **✦ Aurora de Castalia**. Enter launches it.
  `castalia-panel --menu-query TEXT` renders the filtered menu for the gates.

New `shell/libcastalia-ui/Aurora.{h,cpp}`: `KonamiDetector` (a pure state
machine — a wrong key that is itself the opening keeps its credit, so
↑↑↑↓↓… still works) and `paintAurora(rect, phase, opacity, accent)`, a **pure
function** — same phase, same pixels, which is why CI can screenshot it.

### 5. New gates (both wired into CI)

- **`castalia-desktop --selftest`** — head-less, offscreen, synthetic events:
  the wallpaper bakes window-sized, an unchanged reload starts no crossfade,
  a band over one icon selects exactly one, a full-screen band takes all, the
  key sequence fires only when complete, and dismissing the aurora leaves no
  overlay behind.
- **`tools/tests/test_flourishes.py`** (10 tests) — pins the eggs against
  `docs/EASTER-EGGS.md`: one shared aurora implementation (desktop *and*
  screensaver call it), the painter stays timer/widget-free, the key sequence
  matches the documented one, every secret word is documented **and collides
  with no shipped app label**, and the Control Center's scene picker cannot
  drift from the screensaver's CLI.
- `castalia-ui-selftest` gained the Konami cases. Unit tests 144 → **154**.

### 6. Considered and rejected

**Openbox `splitvertical` titlebars.** It would give every window in the OS an
XP-style glossy two-tone titlebar for one word in `theme_export.py` — but
Openbox synthesises the extra stops by lightening/darkening ~12 %, which would
blow the §8.2 16-bit-safety budget (`theme_lint` caps the titlebar gradient's
luminance delta at 0.12 precisely so gradients do not band on period display
modes). Left alone deliberately; do not "fix" it without changing the rule.

### 7. Verification

Full local run: build → `ruff` → 154 unit tests → `theme_lint` → `provenance`
→ `render-all` (**315 renders**, unchanged count, no empty frames) → ISO
dry-runs → real `.deb` (binaries confirmed inside) → `castalia-ui-selftest` →
`castalia-desktop --selftest`. Plus a **live Xvfb smoke**: desktop + panel up
on `:78`, captured with our own `castalia-captura`, wallpaper switched live
(olive-dusk) with the desktop still alive and the new wallpaper showing, then
a clean exit on `SIGTERM`. `xdotool`/`openbox` could not be installed in this
container (broken apt deps), so the pointer-driven paths are covered by the
synthetic-event self-test instead of a live drag; the Openbox e2e still runs
in CI.

## Release v0.1.1 — shipping the ISOs (2026-07-26)

The `v0.1.1` tag was pushed at `d918719` (merge of PR #3) but the **Release**
workflow aborted in ~9 s at its very first gate — *"Tag must match the VERSION
file"* — because `VERSION` was still `0.1.0`. No ISOs were ever built, so the
hand-created GitHub release for `v0.1.1` had zero assets. Fix (PR #4):

- Bumped `VERSION` → `0.1.1` so the tag-vs-VERSION gate passes.
- **Version has one source of truth now.** `shell/CMakeLists.txt` reads the
  repo-root `VERSION` file for its `project(... VERSION)`, so `PROJECT_VERSION`
  — which `apps/acerca` compiles into the About box — can't drift (was still
  hard-coded `0.1.0`, i.e. a 0.1.1 build would have shown 0.1.0).
- `release.yml` publish step reworked: derives the tag from `VERSION` (works on
  both a tag push and a `workflow_dispatch`); keeps the Bible §17.3 **draft**
  gate on automatic tag pushes; publishes **live only** on an explicit
  `publish=true` dispatch (that dispatch *is* the human approval). It refreshes
  assets/notes **in place** (never blind-deletes a release, so re-runs are
  safe) and records the exact build commit in the notes.
- The session token can't move/create tags (GitHub 403s every tag push), so
  `v0.1.1` was cut via `workflow_dispatch` (publish=true) off the release
  branch — VERSION=0.1.1 there, so the tag-match gate is skipped — building
  `.deb` + apt repo → live-amd64 ISO (QEMU boot-verified) → live-desktop ISO
  (framebuffer-verified) → publishing the release with both ISOs, the `.deb`,
  the repo tarball, `SHA256SUMS` and the desktop-proof PNG.

Opened as PR #4 into `main` so the VERSION bump + fixes land there and future
tags cut from `main` don't hit the stale-VERSION gate again.

## 2026-08-18 — the XEmbed half of the tray

The SNI half of §7.4 shipped in August; this is the other half, and the older
one. An application that predates D-Bus does not publish an object on a bus —
it hands the panel **its own X window** and asks to be adopted. GTK2-era
applets, Wine, and `QSystemTrayIcon` on a machine with no session bus all
arrive this way, so a Castalia session with no bus at all now still has a
working tray.

`shell/panel/src/XEmbedTray.{h,cpp}` does the freedesktop System Tray
Protocol, in the order it actually happens:

1. own the `_NET_SYSTEM_TRAY_S<screen>` selection on a window of ours;
2. announce it with a `MANAGER` client message to the root window —
   applications that started **before** the panel are sitting there waiting
   for exactly that, and without it the tray stays empty all session;
3. answer `_NET_SYSTEM_TRAY_OPCODE` / `SYSTEM_TRAY_REQUEST_DOCK` by
   reparenting the client's window into a container of ours;
4. `_XEMBED` / `XEMBED_EMBEDDED_NOTIFY` it and map it — an XEmbed-aware
   toolkit waits for that message and never draws without it.

If somebody else already owns the selection we stand down rather than fight
over the icons; on shutdown every client is reparented **back to the root
window** rather than destroyed with us, so restarting the panel does not kill
the application's icon.

### The bug worth writing down: who owns the pixels behind an icon

The first version gave each icon a native `QWidget` slot and let Qt place it in
a `QHBoxLayout` — the obvious thing to do in a Qt panel. Under Xvfb with one
client it looked right. With two, the **first icon turned into a blank cream
box** the moment the second docked.

An XEmbed client sets its own window background to `ParentRelative`: it
deliberately paints nothing behind itself and inherits the **X background
attribute of the window it is embedded in**. Not what Qt paints there — the X
attribute. So the container's background is the only thing behind a legacy
icon, and Qt, which knows nothing about that, resets it whenever it re-lays out
its native children. Icon two arrives → layout runs → icon one's background is
Qt's window colour again → cream box.

The fix is to take the containers away from Qt entirely: they are created with
`xcb_create_window` as children of the panel's top-level window, positioned by
hand, and never handed to a layout. Which then needs one more thing — the tray
frame slides left when the clock grows a digit, and a child widget gets no move
event of its own when an *ancestor* moves, so `XEmbedTray` watches every
ancestor for `Move`/`Resize` and repositions its containers.

And once we owned the background, a flat colour was no longer good enough: the
tray well is a gradient, so a flat patch behind each icon read as a visible
square. Each container now gets a **background pixmap of the exact slice of
panel it covers** — rendered by asking the panel itself to paint that square
(`QWidget::render` into a `QImage`, uploaded with `xcb_put_image`) — so the
icons sit on the real gradient, seam-free.

### Gates

`castalia-panel --selftest` gained the pure parts: `selectionAtomName()` (a
typo there and every legacy tray icon in the session silently goes nowhere),
`widthFor()` (an empty tray must take *no* width, so the well closes up), and
`CastaliaPanel::trayWellColor()`, the fallback colour a `ParentRelative` icon
lands on — invisible in code review, glaring on screen.

**Live proof** (Xvfb, no D-Bus at all, so Qt falls back to XEmbed): a tray
client written for the purpose; three copies docking, drawn on the panel
gradient; killing the middle one closes the row up and re-renders the
survivors' backgrounds in place. Then the same session **with** a bus: an SNI
item and an XEmbed client side by side in the same tray, proving the two halves
are independent.

### Verification

Build → ruff → 182 + 55 + 11 unit tests → theme_lint → provenance → 322
offscreen renders → ISO dry-runs → `.deb` (95 binaries) → ten C++ self-tests.

## 2026-08-18 — Alt+Tab is ours now

Until today Alt+Tab was Openbox's: `openbox-rc.xml` bound it, Openbox drew its
own OSD, and the only thing Castalia could say about it was a handful of
`osd.*` lines in the generated themerc. That is not a switcher we designed; it
is somebody else's switcher wearing our colours.

`shell/panel/src/Switcher.{h,cpp}` replaces it. A centred card in the theme's
surface, a row per window — icon from the shared 48 px family, then the title —
the accent wash and leading bar the launch menu uses for hover, and the
selection **sliding** between rows in 120 ms (skipped under reduce-motion).
Alt+Shift+Tab walks back, Alt+` narrows the list to the windows of the program
you are already in (§7.6), `Esc` abandons the switch, `Enter` takes it.

### Two decisions worth the words

**It lives in the panel, not in a process of its own.** `shell/README.md` had
always planned a `switcher/` binary. But §16 gives the switch **≤120 ms on
FLOOR**, and a Qt process needs ~70 ms before it draws a pixel — most of the
budget spent on starting up, every single press. The panel is already resident
and already speaks EWMH for the taskbar, so the switcher is a window it owns.

**It owns the key binding itself.** X hands a passive key grab to the first
client that asks, and Openbox starts before the panel — so as long as
`openbox-rc.xml` bound `A-Tab`, ours would lose the race silently, with
nothing in any log to explain why Openbox's OSD was still appearing. The
bindings are gone from the rc, replaced by a comment saying why, and
`tools/tests/test_openbox_rc.py` now fails the build if any of them comes
back. The grab covers Caps Lock and Num Lock too: an X grab matches the
modifier state *exactly*, so without the lock combinations a session with Caps
Lock on would simply have no Alt+Tab.

Most-recently-used order is the half of Alt+Tab people actually feel — one
press goes back to where you just were. EWMH does not publish it, so the panel
keeps its own: it already watches `_NET_ACTIVE_WINDOW` on the root window, and
every change moves that window to the front of the list.

### The icon bug the live test found

First version asked `_NET_WM_ICON` first and fell back to our roster. Under
Openbox, every one of our own apps came up wearing the same anonymous blue
square: Qt publishes a default `_NET_WM_ICON` for any application that never
set one, so "what the program publishes" was never empty and the family icons
never got a look in. The order is inverted now — ours first by WM_CLASS,
`_NET_WM_ICON` after (it is the only thing that works for Wine, DOSBox and
anything we did not write), and the new generic `window` icon last.

### Two tables became one

The switcher needs to know which icon a binary wears — which the launch menu
already knew, in a literal buried in its constructor. That is now
`shell/panel/src/AppRoster.{h,cpp}`, read by both, and
`tools/tests/test_app_roster.py` checks every icon name has an SVG and every
binary is in `tests/apps.manifest` — a typo in either used to render a blank
square in silence, and now in two places at once. The EWMH reads the taskbar
and the switcher share went the same way, into `shell/panel/src/Ewmh.{h,cpp}`;
`WindowList` lost a third of its lines to it.

### And a red gate that had nothing to do with Alt+Tab

`tests/e2e/apps-live.sh` had been failing on `notificaciones` since the app
landed: the manifest ran it as `--demo`, whose toasts are `Qt::ToolTip`
windows — override-redirect, never managed, never in `_NET_CLIENT_LIST`, so
the harness's "an EWMH window within 15 s" could not pass however long it
waited. Live args are `--historial` now (the real window, and the same task
its menu entry opens); the render still uses `--demo` for the toast stack.
43 apps pass again.

### Gates

`castalia-panel --selftest` pins the pure parts: `step()` (the cycle wraps
both ways), `orderByMru()` (MRU first, the WM's order after, closed windows
dropped), `decodeIcon()` (including a truncated `_NET_WM_ICON` — that array
comes from other people's programs and a lying width would walk us off the end
of it), and the roster lookup. `--switcher-shot` renders the card in every
theme, so the offscreen gate is 329 renders now and dark and High Contrast are
checked on every commit.

**Live proof** (Xvfb + a real Openbox + three app windows, driven by xdotool
exactly as a person drives it): hold Alt, tap Tab — the card appears with the
three windows in MRU order; tap again — the selection moves; let go — the
focus actually moves. `Esc` leaves the focus where it was; Alt+Shift+Tab walks
the other way; and `_NET_CLIENT_LIST` is the same length before and after,
which is how we know the switcher's own window stays out of the list it draws.

### Verification

Build → ruff → 187 + 55 + 11 unit tests → theme_lint → provenance (74 assets)
→ 329 offscreen renders → live taskbar smoke → 43/43 apps live under Openbox →
session smoke → ISO dry-runs → `.deb` → ten C++ self-tests.

## 2026-08-18 — the live ISO had no way in

Dave put the ISO on a USB stick and got a shell. That is not a hardware
problem; it is four faults in a row, and each one is worth writing down
because each one was invisible from inside the repo.

### 1. The boot menu in the repo was not the boot menu that shipped

`iso/isolinux/isolinux.cfg` held a handsome four-entry vesamenu — Live, safe
graphics, text install, memtest — and `build/mkiso.sh` never read it. It wrote
its own config inline, with **one** entry, `DEFAULT live`, no installer, and a
`MENU TITLE` that said "Castalia OS Classic" whichever edition you had built.
The comment in mkiso.sh even said so ("the graphical vesamenu at
iso/isolinux/isolinux.cfg is the release/on-hardware design"), which is how a
design document quietly becomes a lie: nothing fails when the two disagree.

Now there is one menu: `iso/isolinux/isolinux.cfg.in`, rendered per edition,
with `@TITLE@` / `@APPEND@` / `@INSTALL@` filled in by the build.

### 2. Nothing on the image had ever read `castalia.installer=`

The design menu's text-install entry passed `castalia.installer=text` on the
kernel command line. Grep the whole repo for that string and the only hits
were in the menu itself. Even if that entry had shipped, it would have booted
an ordinary live session and sat there.

`/usr/local/bin/castalia-live-session` reads it out of `/proc/cmdline` now:
`text` execs the TUI installer, `gui` exports `CASTALIA_AUTOSTART_INSTALLER=1`
and `castalia-session` opens the graphical installer three seconds after the
desktop settles — over a desktop the user can already see and try, which is
the entire point of a live image (§14.5).

`tools/tests/test_iso_boot.py` pins the seam that failed: **every
`castalia.installer=` mode the menu offers must have a case in the live
session.** A menu entry and the code behind it can no longer drift apart
silently.

### 3. The live desktop never offered to install itself

§14.1 has said "an *Install Castalia* icon sits on the desktop" since the
first draft. There was no such icon. There is now — the fifth fixed icon, and
only when `castalia::isLiveSession()`: the launcher exports `CASTALIA_LIVE=1`,
and live-boot's own `/run/live/medium` is the second opinion for a session
started some other way. An installed desktop must never show it, so the
desktop self-test asserts **4 icons or 5** depending on that answer, in both
directions.

Rendering it turned up an older bug: two-line labels were clipped. The icon
cell was 96 px tall with a 34 px text box, and two lines need 36 — so
"Lugares de red" had been losing the bottom of "red" this whole time, on every
screenshot we have ever taken. Cell 104, text box 42.

### 4. If X failed, the machine looked dead

The old inittab line was `respawn:castalia-startx`, and `castalia-startx`
ended in `exec startx`. On hardware whose GPU X cannot drive, that is a loop
that prints errors to a log nobody can reach and shows nothing — with the
tty1 getty deliberately removed, there was not even a shell to type into.

Now the launcher runs `startx` **without** exec, and when it returns non-zero
it prints, in Spanish, the three commands that still work — retry the desktop,
install without graphics, reboot — and hands over a root shell on tty1. The
same two commands are in `/etc/issue`, so anyone who lands on a console for
any other reason sees them too. A live image whose graphics fail must still be
installable; that is the §14.5 #5 promise and it now has a path.

### 5. …and the one that actually did it: the shell was never on the image

Everything above was found by reading. This was found by **building an ISO**,
which nobody had done locally before, and it is the fault that produced
Dave's shell prompt.

`shell/CMakeLists.txt` reads the repo-root `VERSION` file — added on
2026-07-26 in the release work, so that the About box could not drift from the
packaged version. The chroot hook copies `SRC_DIRS` into the image and builds
the shell in there. `VERSION` is not a directory, so it was not in `SRC_DIRS`,
so **cmake could not configure at all**:

    CMake Error at CMakeLists.txt:7 (file):
      file STRINGS file "/usr/src/castalia/shell/../VERSION" cannot be read.

And the build did not stop. `stage_hook` ran the chroot inside
`sh -c "cp …; chroot …; rm -f …"` — semicolons — so the exit status of the
whole stage was `rm`'s, which is always 0. The build cheerfully squashed a
rootfs with **no `/opt/castalia` at all** and published it. With no Castalia
binaries, the hook's tty1 rewiring never happened either, so the image kept
the base `agetty --autologin root tty1` and booted to exactly what Dave saw: a
root shell.

`castalia-live-desktop-amd64-0.1.1.iso` was built from `13a52fc`, which is 26
minutes *after* the VERSION change landed. So the ISO on the release page has
never had a desktop on it.

Three fixes, because one is not enough:

1. `VERSION` is in `SRC_DIRS` now (it is a file, and `cp -a` does not care).
2. The hook stage runs under `set -e`, so a chroot that dies fails the build.
3. mkiso.sh **checks the artefact** before squashing: `castalia-panel`,
   `castalia-desktop` and `castalia-session` must exist under `/opt/castalia`,
   and an edition with `INSTALLER="yes"` must also have `castalia-live-session`
   and `castalia-instalador`. An exit code can lie; a missing file cannot.

`tools/tests/test_iso_boot.py` pins all three, including reading
`shell/CMakeLists.txt` for what it needs from outside `shell/` and requiring
every hooked profile to stage it.

### The verification found two more of my own

Rendering the menu the first time put `@TITLE@` and `@APPEND@` in before
splicing the install entries — and those entries carry an `@APPEND@` of their
own, so the ISO shipped an install entry reading `APPEND
initrd=/live/initrd.img @APPEND@ castalia.installer=gui`: an installer that
boots without `boot=live`. The template's comment block also documented the
placeholders *in their own syntax*, so rendering rewrote the documentation
into the middle of the menu and pushed `LABEL install` above `LABEL live`.

Neither was visible in the template, in the build script, or in any unit test
I had written. Both were caught by `tests/iso/menu-check.sh` pulling
isolinux.cfg back out of the finished ISO — the check that looks at the
artefact rather than the intent. The substitution now lives in
`tools/boot_menu.py`, INSTALL first, with `tools/tests/test_boot_menu.py`
pinning the order and the comment-substitution trap.

### And a naming trap worth knowing about

The release publishes **two** ISOs: `castalia-live-desktop-amd64` (the
graphical one) and `castalia-live-amd64`, which is a deliberately lean
boot-proof image — kernel, live-boot, SysVinit, no X at all. Its inittab
autologins root on tty1, so a shell prompt is *exactly* what it is supposed to
do. The menu title now carries the edition's own LABEL ("Castalia Live (amd64
boot proof)"), so the screen says which image you are holding, and the install
entries appear only for editions whose profile sets `INSTALLER="yes"` — the
lean image never offers what it has no binary for.

### Gates

`tools/tests/test_iso_boot.py` (the template is what ships; live is the first
and default entry; every entry boots something the build stages; no
unsubstituted placeholder; install modes match the live session's cases;
editions that promise an installer run the hook that provides one) and
`tools/tests/test_live_session.py` (the launcher is respawned, marks the
session live, does not `exec startx`, falls back to a getty, and names the
same commands in its message and in `/etc/issue`).

`tools/tests/test_boot_menu.py` covers the renderer, including the ordering
bug above.

Then `tests/iso/menu-check.sh` does what none of them can: it pulls
isolinux.cfg back **out of the finished ISO** with xorriso and checks what a
person booting that stick is actually offered. It runs in CI right after the
ISO build.

### Proof

A real `live-desktop-amd64` ISO built locally (527 MB), its menu checked
out of the image (4 entries, live first and default), booted in QEMU to the
serial marker, and captured at 1920×1080: the Castalia desktop, the welcome
window, and **"Instalar Castalia OS"** as the fifth desktop icon. The
`castalia.installer=gui` path was proven separately under Xvfb — the installer
wizard opens over the live desktop, and `castalia-session` logs "boot menu
asked for the installer".

## 2026-08-18 — the last three §9 apps

`castalia-hardware`, `castalia-discos` and `castalia-migrar`. 43 apps → 46,
and §9 has no unbuilt rows left except the Network Center.

### Centro de hardware — read the machine, not a database

The obvious implementation shells out to `lspci`/`lsusb` and parses their
prose. That gives a machine with no `pciutils` an empty window, which on a
FLOOR-tier image is the machine most likely to need the app.

So the inversion: the device *list* comes from **sysfs**, which is always
there — `/sys/bus/pci/devices/*/{class,vendor,device,driver}` — and `lspci`
is used only when present, only to put a human name on a numeric id. A
machine without it reads `Dispositivo 8086:0d57` and still knows the class
and the bound driver, which is the question being asked. USB devices carry
`product` and `manufacturer` strings themselves, so they are named with no id
database at all.

The one screen-worthy detail: the status column answers "is something
driving this", and when the answer is no, the tooltip says that it is often
*normal* — a system bridge does not need a module. Telling a user "sin
controlador" without that sentence just invents a problem for them.

### Administrador de discos — the app is its refusals

It can erase a disk, so the design is not the tree; it is what stands between
a user and their photographs. Format is offered **only** for a device that is
removable, not mounted, not write-protected, and not the medium the session
booted from (`/run/live/medium` is in the system-mount list — erasing the
stick you are running from is a special kind of bad). And then the button
still leads nowhere until the user **types the device path exactly**, the
same gate the installer uses. "¿Estás seguro?" is not a gate.

`formatRefusal()` returns *why*, not a bool, so the tooltip on the disabled
button tells you what to do about it ("Está montado en /media/usb.
Desmóntalo primero") instead of leaving you to guess. `canFormat()` is just
`formatRefusal().isEmpty()`, so the two can never disagree.

### Asistente de migración — the failure mode is silence

Read-only is structural, not a promise: the Windows partition is mounted
`-o ro` and if that fails the app stops rather than retrying without the
flag; rsync is never passed `--delete`; the destination is always the new
home and never the old disk. Those three facts are pinned by the self-test,
including `args.last()` not starting with the source mount.

The subtle part is the folder table. Windows moved the user's folders
(`Documents and Settings\<user>\My Documents` in XP, `Users\<user>\Documents`
after) **and** Spanish installations translated them — `Mis documentos`,
`Mis imágenes`, `Mi música`. A name missing from that table does not error:
it copies nothing and reports success. That is the worst kind of bug for an
app someone runs once, with one copy of their photographs, so the table is a
pure function with a self-test covering XP English, XP Spanish and a
case-insensitive modern profile. The profile list also drops the accounts
Windows creates for itself (`All Users`, `Todos los usuarios`, …) — offering
those as people to migrate is how someone ends up copying 4 GB of nothing.

### One parser, two apps

The Disk Manager and the Migration Assistant both need the block layer, and
the first draft had `lsblk --json` parsed in both. That is how two apps end
up disagreeing about what a partition is, so it moved into
`castalia::blocks` (`shell/libcastalia-ui/Blocks.{h,cpp}`) with its checks in
the libcastalia-ui self-test, and each app kept only its own policy. util-linux
has shipped `size` as a number *and* as a string and `rm` as a bool *and* as
`"0"`; the parser absorbs all of it, because a disk list is not worth losing
to a type change.

### And a screenshot bug that looked like a broken search

Rendering the Start Menu with `--menu-query hardware` came out blank, which
reads as "the new app is not in the roster". It was not: `--menu-shot` grabs
**synchronously**, and hiding two thirds of the roster invalidates the app
column's geometry without running the layout. A live session repaints on the
next event-loop pass and looks perfect; the screenshot captured the old
positions, so a match far down the list — "servicios" has been down there all
along — was still parked below the viewport. `setQuery()` settles the layouts
by hand now. Every press-kit search shot we have taken used a term that
matched in the *first* group, which is why nobody noticed.

### Gates

Three new `--selftest` binaries in CI, all running before any QApplication so
no display is needed: the hardware parsers (PCI class codes, lspci
enrichment, cpuinfo, meminfo), the disk-manager **safety rules** (every
refusal, and the typed gate rejecting `sdb1`, `/dev/sdb` and `si`), and the
migration folder table plus the rsync argument list. The offscreen render
gate is 350 renders now (+21 = three apps × seven themes) and the live suite
is 46/46 under Openbox.

## 2026-08-18 — the Network Center, and §9 is done

`castalia-redes` was an honest read-only status view: interfaces, addresses,
MAC, gateway, straight out of iproute2. §9's MVP asks for more — "wired
DHCP/static, Wi-Fi connect, status tray" — so this is the half that *changes*
things, wrapping NetworkManager through `nmcli` per §6.9.

Three tabs, in the order somebody needs them. **Estado** is the old view,
unchanged and still needing no NetworkManager at all. **Wi-Fi** lists the
networks in range and connects to one. **Configuración** switches a connection
between automática (DHCP) and a manual address, gateway and DNS. When
NetworkManager is not installed the last two tabs **disable themselves and say
why** — an app that offers a button it cannot honour is worse than one that
admits the limitation, and Estado keeps working because reading the truth
about the network never needed NM.

### Two pure functions worth the trouble

**nmcli's terse rows are colon-separated with backslash escapes, and SSIDs
contain colons.** `nmcli -t` emits `*:Mi\:Red:75:WPA2`, and a naive
`split(':')` turns that into a network called "Mi" — which then cannot be
connected to, with an error that makes no sense to anybody. `splitNmcliRow()`
handles the escapes and the self-test pins the case.

**A wrong `nmcli connection modify` argument list does not error.** It
applies. Then the machine has no network and the person who was configuring it
has no way back. So `dhcpArgs()` and `staticArgs()` are pure functions with
tests, including the one that is easy to get wrong: switching back to DHCP
must pass **empty** `ipv4.addresses`/`gateway`/`dns` rather than omitting
them, or the old static values survive a change the user believes they made.
`staticRefusal()` also rejects a maskless address before it is sent —
NetworkManager would accept `192.168.1.42` and quietly make it a `/32`.

Plain language throughout, per §9: a signal is "Excelente", not 92%, and an
open network reads "Abierta (sin contraseña)" rather than nmcli's `--`.

### The status tray, without a resident applet

The MVP's third item is a tray indicator. It is a **panel button** next to
the volume speaker, not a process: the state it shows is three reads of
`/sys/class/net`, on a ten-second very-coarse timer, and a whole extra Qt
process to poll that is not a trade the FLOOR tier should make. Dimmed when
nothing is up (one icon, reduced opacity — no second asset), naming the
interface on hover, opening the Network Center on click.

`anyLinkUp()` skips `lo` deliberately, and the self-test says why: the
loopback is always up, so without that the tray would claim a connection on a
machine with the cable out. "unknown" is not treated as "up" either — we do
not guess on the user's behalf.

### Two design-system fixes it exposed

The tab bar bolded the **selected** tab, but Qt sizes a tab from the
*unselected* font and then draws the selected one bold — so the label
overflowed its own tab and the first one read "Estad". Widening the padding
did not fix it (the tab is sized including padding; the text still overflows),
so the bold is gone: the accent bar and the darker text say "selected"
without touching the metrics, which is the XP-era tab language anyway. That
was wrong in every tabbed app — diagnostics and monitor too.

And the roster gained a `keywords` field. Nobody looks for the Network Center
by typing "centro"; they type **wifi**. The menu search now matches label,
category *and* keywords, so "wifi", "internet", "ip" and "conexiones" all
surface it.

### Gates

`castalia-redes --selftest` (the escape-aware splitter, access points merging
into networks, the plain-language mappings, IPv4/CIDR validation, and both
argument builders including the clear-on-DHCP case) and four checks in the
panel's self-test for the tray light. `--tab estado|wifi|config` was added so
the render gate and the press kit can screenshot a tab a screenshot cannot
click to.

## 2026-08-18 — the screencast, re-cut

The demo was ~43 s and predated seven apps, the menu search, the switcher,
notifications and the tray. It is ~63 s now, with four new beats: **typing to
search the Start menu**, **Alt+Tab** through the open windows, a **real
notification toast**, and the **Centro de redes** listing Wi-Fi in plain
language. Nothing is staged — same Xvfb + Openbox + shell session the CI e2e
captures use, xdotool playing the user, ffmpeg recording.

Two of those beats needed the session to be more real than it was.

**The notification beat needed a bus.** The recording had no `dbus-daemon`, so
there was nothing to send a notification *to*. The script starts a session bus
and the real `castalia-notificaciones` server now, and the beat is an ordinary
`Notify` call — the same one `notify-send` makes. A toast drawn for the camera
would have been a lie.

**And the Alt+Tab beat filmed the wrong switcher.** The first take showed a
grey Openbox OSD with generic window icons instead of Castalia's card. The
cause is the thing the Bible warns about in §7.6, arriving from an unexpected
direction: the screencast wrote its own four-line `rc.xml`, and an rc.xml with
**no `<keyboard>` section makes Openbox fall back to its built-in defaults** —
which bind Alt+Tab. Openbox starts before the panel, so it won the grab. The
recording now uses the **shipped** `shell/session/openbox-rc.xml` (with the
theme name substituted), which is both the fix and the more honest demo: the
video shows the real keyboard map. Second take: our card, the family icons,
and the selection sliding between rows.

Two smaller things while in here. The tool wrote `castalia-demo.*` while the
press kit ships `castalia-os-demo.*`, so refreshing the video needed a rename
nobody remembered — one name now. And `--gif` produces the social GIF with a
generated palette in two passes: a 256-colour GIF quantised on the fly turns
the panel gradient into bands, and the two-pass version is both better looking
and *smaller* (1.7 MB against the old 2.3 MB).

Verified by pulling one frame per second out of the finished MP4 and looking
at every beat — which is how both problems above were found, since a
screencast has no assertions.

## 2026-08-19 — localisation, and the bug that hides in the ordering

Castalia now speaks **English as well as Spanish** — 218 strings, the whole
first-minute surface: the launch menu and every app name in it, the desktop
icons, the taskbar and Alt+Tab, the Run and shutdown dialogs, Escritor, the
sticky notes, and all seven pages of the Welcome/Help centre. Spanish stays the
**source language**: the literals in the C++ are what a Spanish user reads,
nothing is looked up for them, and `castalia_es.qm` deliberately does not exist.

The interesting part was not the translating.

**Spanish stays the default even on an English machine.** `LANG=en_US.UTF-8`
does not change the desktop. Two reasons: the product is Spanish-first, and an
interface that follows an environment variable cannot be screenshotted twice or
gated deterministically — `render-all.sh` would drift with the host. "Seguir el
sistema" is in the picker; it is a choice, not a default. The choice lives in
`~/.config/castalia/locale.conf`, the same flat shape as `theme.conf`, written
by the new **Control Center → Idioma** page. `CASTALIA_LANG` pins one run.

**And then the shutdown dialog came up half translated.** The tiles said
*Apagar* and *Reiniciar* under a header that said *what would you like to do?*.
The catalogue was complete; the strings were marked; the translator was
installed. It was the **ordering**: `main()` calls `resolveActions()` — which
builds the labels — on the line *above* `applyTheme()`, which is where the
translator was being installed. Qt cannot retranslate a widget or a string that
already exists, so everything computed before that line stayed Spanish, silently
and with no error anywhere.

Fixing that one `main()` would have been the wrong fix. The hazard is in all 47
of them, it is invisible, and it comes back the next time somebody hoists a line.
So the loader moved into a **`Q_COREAPP_STARTUP_FUNCTION`**: Qt runs it right
after the `QApplication` constructor, before a single line of anybody's `main()`.
It reads `--repo` straight out of `QCoreApplication::arguments()` (already parsed
at that point; the app's own parser has not run and must not have to). Now the
claim in `Locale.h` — *linking libcastalia-ui is enough* — is actually true, and
`applyTheme()`'s call is just a second, idempotent chance for the case where the
startup guess at the asset root was wrong.

Two smaller finds along the way, both from looking at renders rather than tests:
the panel and desktop **never called `applyTheme()` at all** (they build their
own QSS), so the first English menu came out entirely in Spanish; and the Welcome
centre still told users there were **five themes** and called itself "Castalia
Classic", two flagships ago.

Menu search now indexes the translated label **and** the Spanish source, so an
English desktop still answers to "buscaminas" and the Spanish keyword lists keep
working in either language.

Toolchain: `tools/i18n_build.py {extract,release,--check}` over lupdate/lrelease;
`i18n/castalia_en.ts` is the reviewed catalogue; the `.qm` is installed to
`/usr/share/castalia/i18n` by `packages/mkdeb.sh` and by the ISO hook (which
needed `qttools5-dev-tools` in the chroot and `i18n` added to `SRC_DIRS` — the
same class of omission that once shipped an ISO with no `/opt/castalia`).

`tools/tests/test_i18n.py` (14 cases) fails the build on: a declared language
with no catalogue, any untranslated string, a translation that lost a `%1` or an
`&`, a catalogue nobody installs, a profile that does not stage `i18n/`, and a
GUI entry point that never installs a translator. It does **not** claim to judge
translation quality — that needs a human who speaks the language.

Verified: 229 unit tests, every C++ self-test, theme_lint (7 themes), provenance
(74 assets), `render-all.sh` (350 renders), a real `.deb` with
`usr/share/castalia/i18n/castalia_en.qm` inside it, and both languages rendered
side by side from nothing but the config file.
