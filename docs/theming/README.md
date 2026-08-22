# Theming Guide

*Last verified on version 0.2.0.*

How to make a Castalia theme, and what the linter will not let you get away
with. The bundle format itself is specified in
[`themes/SCHEMA.md`](../../themes/SCHEMA.md); this is the practical guide.

## One file changes everything

A Castalia theme is **one** `theme.conf` (TOML, hand-editable, commented) that
drives Qt/QSS, GTK, Openbox window decorations, the icon tint, the cursor
theme, the LightDM greeter and the sound palette. That is §6.16's decision and
it is the whole reason a theme switch in Castalia looks coherent instead of
looking like six settings that happen to agree.

You do not write QSS. `tools/theme_export.py` generates it from the bundle.

## Making one

```sh
cp -r themes/classic themes/mytheme
$EDITOR themes/mytheme/theme.conf        # set [meta] id = "mytheme"
PYTHONPATH=tools python3 -m castalia_qa.theme_lint themes
PYTHONPATH=tools python3 tools/theme_export.py --out /tmp/themes
```

`[meta] id` **must** equal the directory name. Everything else is colours,
metrics and sound choices.

## What the linter enforces (§8)

`castalia_qa.theme_lint` runs in the `gates` tier and on every commit. It is
not advisory.

**Contrast.** Body text against its background must reach **4.5:1**, and
**7:1** in a bundle that declares `high_contrast = true`. This is the rule that
rejects most first attempts, and it is the one worth defending: Castalia is for
old machines, which means old monitors, which means a panel that has drifted
yellow over twenty years. A palette that is legible on a calibrated 2026
display and not on a 2004 CRT has failed at the actual job.

**The 4 px grid.** `base_unit` must be 4, and every metric must be a multiple
of it. Mixed spacing is what makes a desktop look assembled rather than
designed.

**Both titlebar states.** Active *and* inactive gradients and text. A theme
that only styles the focused window leaves every other window looking like a
different operating system.

**800×600.** `panel_height_800` exists because the FLOOR machine is 800×600
and a 36 px taskbar there is a meaningful fraction of the screen.

## Icons

Icons are 48 px SVGs in `themes/icons/48/`, shared across themes and tinted
per theme rather than redrawn. Keep them on the same grid, keep the strokes
consistent, and remember they are also rendered at 24 px in the taskbar and at
16 px in menus — a detail that vanishes at 16 px is a detail that should not
be there.

## Sounds

The sound palette is generated, not sampled: `tools/sound_gen.py` synthesises
it from the parameters in the bundle. That is a provenance decision (§3.9) as
much as an aesthetic one — a generated sound has an unambiguous origin, and a
sampled one has to be proven.

## Provenance (§3.9) — this is a release blocker

Every asset that ships must be original or licensed, and must be recorded in
`legal/ASSET_PROVENANCE.csv`. `castalia_qa.provenance` runs in CI and fails
the build on an asset with no ledger entry.

This is not paperwork. Castalia is deliberately reminiscent of a commercial
product from the 2000s, and being *reminiscent of* is legal while *containing
pieces of* is not. Nothing may be traced, recoloured, or derived from that
product's artwork, sounds, fonts or icons. If you cannot say where something
came from, it cannot ship.

## Contributing a theme

Open a pull request with the bundle. It needs to pass `theme_lint`, have a
provenance entry for any new asset, and include a screenshot of the desktop at
800×600 — because that is the size the promise is made about.
