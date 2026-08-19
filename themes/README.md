# themes/ — Castalia theme system

The seven original themes and the engine that applies them coherently across
GTK, Qt/QSS, Openbox decorations, icons, cursors, the LightDM greeter, boot
splash, and sounds — from a single `theme.conf`. See
[`docs/PROJECT_BIBLE.md` §6.16](../docs/PROJECT_BIBLE.md#6-system-architecture)
and [§8](../docs/PROJECT_BIBLE.md#8-visual-design-system).

## Contents

| Path | Status | Purpose |
|------|--------|---------|
| `SCHEMA.md` | ✅ | The `theme.conf` bundle format + the CI-enforced design rules |
| `human/` | ✅ palette | **Castalia Human** (shipped default) — chocolate + sun orange, "Human Dawn" wallpaper |
| `classic/` | ✅ palette | **Castalia Classic** — sandstone + sea azure |
| `azul/` | ✅ palette | **Castalia Azul** — deeper marine blue |
| `oliva/` | ✅ palette | **Castalia Oliva** — olive/terracotta |
| `plata/` | ✅ palette | **Castalia Plata** — silver/graphite |
| `medianoche/` | ✅ palette | **Castalia Medianoche** — full-desktop dark mode |
| `high-contrast/` | ✅ palette | **Castalia High Contrast** — accessibility (AAA) |
| exporters | ✅ | `tools/theme_export.py` generates **Qt QSS + Openbox themerc** per theme from the tokens (`build/out/themes/`, CI-smoked) |
| `icons/` | ✅ | The tangerine/tango icon family — 32 original gradient SVGs on the 48 px grid (16/24/32 px render from the same sources) |
| `engine/` | planned | On-device apply logic (one switch swaps QSS + GTK + Openbox + greeter + sounds together) |
| `cursors/` | planned | Original/libre cursor theme |
| `fonts/` | planned | Libre/original UI + mono fonts (never Microsoft fonts) |

All themes share **one** geometry/grid/control spec; only the palette (and
optionally the `[assets].wallpaper`) changes (§8.2). Gradients are original
and 16-bit-safe — **enforced in CI** by `tools/castalia_qa/theme_lint.py`
(luminance delta ≤ 0.12, WCAG contrast minima, 4 px grid).
