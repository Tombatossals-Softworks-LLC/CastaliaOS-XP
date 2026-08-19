# branding/ — Identity & design tokens

Original brand and design assets for Castalia Classic. **Everything here is
original or licensed** and has a row in
[`legal/ASSET_PROVENANCE.csv`](../legal/ASSET_PROVENANCE.csv) (CI-enforced).
See [`docs/PROJECT_BIBLE.md` §8](../docs/PROJECT_BIBLE.md#8-visual-design-system)
and [§21](../docs/PROJECT_BIBLE.md#21-branding-and-lore).

## Planned contents

| Path | Purpose |
|------|---------|
| `spec/` | Design tokens: palette, 4px grid, icon grid, window-control geometry (`controls.svg`) |
| `boot/` | Original boot splash (framebuffer-safe @640×480) + text fallback |
| `login/` | LightDM greeter assets (wallpaper, avatars, layout) |
| `sound/` | Original system sounds (ogg) + source project files (§21.4) |
| `logo/` | The Castalia mark (`castalia-mark.svg`, v0.2 "C monogram keep") + Tombatossals Softworks lockup |

## The mark

`logo/castalia-mark.svg` is the **single source of truth** for the logo: a
stylised letter C as a thick azure ring, open to the right, with the Castalia
keep inside it on green hills. Geometry only — no fonts, no rasters, no
filters, and **no `<clipPath>`** (Qt 5's QSvgRenderer, which paints this file
throughout the shell, ignores it; shapes that must stay inside the badge are
bounded by arcs of the badge circle instead).

SVG cannot reference another document's shapes, so the mark is copied into
`boot/splash.svg`, `login/banner.svg` and `presskit/logos/`, and re-drawn in
two other languages — `castalia::drawMark()` (QPainter: the Start orb and a
dozen apps) and `tools/bootbg_gen.py` (pure-Python pixels: the boot menu
background). **`tools/tests/test_brand.py` fails the build when those copies
drift**, so change the master first and let the test tell you what else to
follow up. Embedded copies namespace their gradient ids (`mk-*`) so they
cannot collide with their host document's `<defs>`.

The keep also appears as *scenery* on the wallpapers and in the greeter's
ridge line. That is lore, not the logo, and it is not required to track the
mark's geometry.

## Hard rules

- No Microsoft asset is ever copied, traced, recolored, or sampled (§3).
- Design tokens are defined **before** art is drawn — the palette, grid, and
  control geometry are the contract every theme and icon obeys.
