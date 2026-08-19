# tools/ — Developer & build tooling

Utilities used by the build and CI. See
[`docs/PROJECT_BIBLE.md` §17.2](../docs/PROJECT_BIBLE.md#17-build-system-and-repository-structure)
and [§3.9](../docs/PROJECT_BIBLE.md#3-legal-and-branding-strategy).

## Implemented

| Tool | Status | Purpose |
|------|--------|---------|
| `castalia_qa/provenance.py` | ✅ | **Fails the build** if any asset under `branding/` or `themes/` lacks a row in `legal/ASSET_PROVENANCE.csv` (also catches stale rows and missing fields) |
| `castalia_qa/theme_lint.py` | ✅ | Validates theme bundles against `themes/SCHEMA.md` **and enforces the §8 design rules**: 16-bit-safe gradient luminance delta (≤0.12), WCAG contrast minima (AA, AAA for high-contrast), 4 px grid metrics |
| `castalia_qa/color.py` | ✅ | Shared WCAG relative-luminance/contrast math |
| `preview_gen.py` | ✅ | Generates `docs/preview.html` — the interactive design-system mockup — from the shipped tokens, icons and wallpaper |
| `theme_export.py` | ✅ | **theme.conf → Qt QSS + Openbox themerc** (§6.16 made mechanical); output to `build/out/themes/` (gitignored build artifacts) |
| `sound_gen.py` | ✅ | Deterministic sound-palette renderer: `branding/sound/palette.toml` → WAVs + the `docs/sound-preview.html` board (§21.4) |
| `tests/` | ✅ | Unit suite for all of the above (run: `PYTHONPATH=tools python3 -m unittest discover -s tools/tests`) |

Usage:

```sh
PYTHONPATH=tools python3 -m castalia_qa.theme_lint themes
PYTHONPATH=tools python3 -m castalia_qa.provenance .
```

```sh
PYTHONPATH=tools python3 tools/preview_gen.py          # design-system mockup
PYTHONPATH=tools python3 tools/theme_export.py         # QSS + Openbox themes
PYTHONPATH=tools python3 tools/sound_gen.py --board    # WAVs + sound board
```

## Planned

| Tool | Purpose |
|------|---------|
| `asset-bake` | Rasterizes SVG icons to PNG at 16/24/32/48 px (old machines render pre-baked PNG faster than live SVG) |
| `license-check` | Verifies third-party SPDX licenses against `legal/THIRD_PARTY.md` |
| `help-gen` | Builds the offline Help Center content from `docs/` |

Language: primarily Python 3 (tooling only — never a resident desktop
dependency, §12).

- `make-screencast.sh` — record the marketing demo screencast: a real
  Xvfb + Openbox + shell session driven by xdotool (Start menu, Pintura,
  Buscaminas, Calendario from the clock, live wallpaper switch) captured with
  ffmpeg. Needs Xvfb/openbox/xdotool/ffmpeg; build the shell + export themes
  first. Output under `build/out/screencast/`.
