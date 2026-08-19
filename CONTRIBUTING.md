# Contributing to Castalia OS

**Castalia OS** — an original, legally-clean, XP-class desktop OS for early-/
late-2000s PCs (own Qt 5 / C++17 shell, first-party apps, seven-theme design
system, Wine/DOS/ScummVM compat). Built by **Tombatossals Softworks**.

Start with [`docs/PROJECT_BIBLE.md`](docs/PROJECT_BIBLE.md) for what the
product is and why, and [`docs/CASTALIA-HUMAN-WORKLOG.md`](docs/CASTALIA-HUMAN-WORKLOG.md)
for the engineering log — what has been built on the **"Human" edition**, how
it is built and verified, the add-an-app checklist, and the gotchas worth
knowing before you trip over them. Keep the worklog updated as you go.

## Where the project is

- **Flagship/default theme:** **Human** (chocolate + sun-orange, "Human Dawn"
  wallpaper). 7 themes total; **46 first-party apps**; **40** 48-px icons;
  **9** Xcursor pointers; **7** system sounds (wired to real events);
  **4** screensaver scenes. Hidden flourishes are documented in
  [`docs/EASTER-EGGS.md`](docs/EASTER-EGGS.md) — and gated by
  `tools/tests/test_flourishes.py`, so keep the two in sync.
  *(Keep these counts in sync across README + `presskit/` when they change.)*
- The full app/theme/icon inventory and history live in the worklog above.

## Golden rules

- **Verify before pushing.** Qt is installable on a plain Debian/Ubuntu box:
  `sudo apt-get install -y qtbase5-dev libqt5svg5-dev libxcb1-dev qttools5-dev-tools`.
  Then build the shell and run: `ruff`, `python3 -m unittest discover -s tools/tests`,
  `theme_lint`, `provenance`, and `tests/offscreen/render-all.sh`. Build a real
  `.deb` with `packages/mkdeb.sh` and confirm new binaries are inside. (Full
  Openbox e2e and the rsync Restore-Points smoke run in CI, not always locally.)
- **Adding an app?** Follow the 9-step checklist in the worklog — especially:
  add it to BOTH `tests/apps.manifest` AND the `install`/symlink lists in
  `build/hooks/desktop-amd64.sh` (a unit test enforces they match).
- **Original art only.** Nothing Microsoft; every `branding/`+`themes/` asset
  needs a `legal/ASSET_PROVENANCE.csv` row (CI-enforced). `presskit/` is exempt.
- **The logo** is `branding/logo/castalia-logo.png` (the artwork, used at
  ≥32 px). `branding/logo/castalia-mark.svg` is its **vector edition**: used
  below 32 px and embedded in the boot splash, the login banner and
  `tools/bootbg_gen.py`. Everything paints it through `castalia::drawMark()`.
  `tools/tests/test_brand.py` fails when the two drift apart.
- **The default wallpaper** is `branding/wallpapers/valle-de-castalia.jpg`
  (theme token + desktop fallback + greeter must agree; the test pins it).
  Raster wallpapers are always decoded with `QImageReader::setScaledSize()`.
  Ship wallpapers as JPEG/PNG/SVG only — **Qt 5 base has no WebP plugin**.
- **Spanish-first UI**; animations ≤200 ms gated by `castalia::reduceMotion()`.
  English ships as a translation catalogue on top (§7.13); run
  `tools/i18n_build.py --check` before pushing anything that adds strings.
- **Work on a branch, never push to `main`.** Commit messages: a short subject
  and a descriptive body that says *why*, not just what.

## Handy entry points

- Themes/tokens: `themes/<id>/theme.conf` → `tools/theme_export.py` (QSS +
  Openbox). Linter: `tools/castalia_qa/theme_lint.py`.
- Shell: `shell/{libcastalia-ui,panel,desktop,session}`. Apps: `apps/<name>`.
- App manifest (source of truth for QA/packaging): `tests/apps.manifest`.
- Translations: `i18n/castalia_<lang>.ts` → `tools/i18n_build.py`.
- Press kit: `presskit/` (self-contained `index.html`). Marketing screencast:
  `tools/make-screencast.sh` → `presskit/video/`.
- Contact used in all outward material: **hello@tombatossalssoftworks.com** /
  **tombatossalssoftworks.com**.
