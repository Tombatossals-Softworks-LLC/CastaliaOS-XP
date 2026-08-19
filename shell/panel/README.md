# castalia-panel — Phase 0 proof of concept

The **first native code of Castalia OS**: a C++17/Qt 5.15 taskbar panel that
proves the token pipeline end-to-end (Bible §18 Phase 0, §7.2, §6.16):

```
themes/<id>/theme.conf ──► tools/theme_export.py ──► castalia.qss ─┐
        │                                                          ▼
        └────────► ThemeTokens (C++ parser) ──► panel styling ──► pixels
```

## What it demonstrates

- **Token parsing in C++** (`ThemeTokens`) — reads the same `theme.conf` the
  linter validates and the exporters consume; panel height, radius and every
  color come from the tokens (High Contrast correctly renders 32 px).
- **Generated QSS loading** — the stylesheet produced by
  `tools/theme_export.py` styles the widgets; panel/menu-specific rules are
  derived from the tokens at runtime.
- **Native artwork** (`Mark.cpp`) — the keep mark and the avatar are painted
  with QPainter; **zero SVG/image plugin dependency** on the FLOOR tier.
- **§7.2 layout** — launch button → quick launch → window list → tray →
  clock; the launch menu (§7.3) opens above the panel with pinned apps,
  places, and the power row.
- **Budget honesty** — prints startup ms and RSS on every run
  (measured here: ~11 ms startup, ~25 MB RSS offscreen; §16 shell budget is
  ≤ 60 MB for the *whole* shell).
- **Headless CI verification** — `--screenshot`/`--menu-shot` render PNGs
  under `QT_QPA_PLATFORM=offscreen`, so every theme's panel is
  pixel-verifiable without X.

## Build & run

```sh
PYTHONPATH=tools python3 tools/theme_export.py          # generate the QSS first
cmake -S shell -B build/out/shell-build -DCMAKE_BUILD_TYPE=Release
cmake --build build/out/shell-build -j4
QT_QPA_PLATFORM=offscreen ./build/out/shell-build/panel/castalia-panel \
    --theme classic --repo . --screenshot panel.png --menu-shot menu.png
```

Binary size: ~84 KB stripped. PoC scope only — the production panel
(runit-supervised, SNI tray, real window list via EWMH) grows from here
per §18 Phase 2.
