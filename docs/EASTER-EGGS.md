# Castalia OS — the flourishes (spoilers)

> **Spoiler warning.** This file exists so the hidden things stay *maintained*
> — a flourish nobody documents is a flourish the next refactor deletes by
> accident. If you would rather find them yourself, close the file now.

Castalia's hidden extras follow three house rules:

1. **Nothing hides a feature.** Every egg is decoration. No setting, no file
   and no capability is reachable *only* through one.
2. **Nothing runs without being asked.** They start on a deliberate action,
   they stop on any key or click, and they leave no timer behind (§16).
3. **They obey the same switches as everything else.** `reduceMotion()`
   (`CASTALIA_REDUCE_MOTION=1`, or an offscreen render) turns the animation
   into a single held frame; `CASTALIA_NO_SOUND=1` keeps them quiet.

---

## 1. The Aurora — `↑ ↑ ↓ ↓ ← → ← → B A`

**Where:** the desktop (`castalia-desktop`). Click the wallpaper so the
desktop has the keyboard, then type the sequence.

**What happens:** the screen becomes a night sky and four aurora curtains draw
themselves over it — striated, drifting, tinted with the **active theme's
accent**, with the Castalia keep and the studio name beneath. It plays the
system startup chime, runs for nine seconds and fades out. Any key or click
ends it early.

**Implementation:** `castalia::KonamiDetector` and `castalia::paintAurora()` in
[`shell/libcastalia-ui/Aurora.{h,cpp}`](../shell/libcastalia-ui/Aurora.h). Both
are deliberately widget-free: the detector is a pure state machine (covered by
`castalia-ui-selftest`), and the painter is a pure function of
`(rect, phase, opacity, accent)` — the same phase always paints the same
pixels, which is why a screenshot of it reproduces in CI.

**It lives on the desktop layer**, so it paints where the wallpaper is: open
windows stay in front of it, exactly as they stay in front of the wallpaper.
Show it from a clear desktop. (The screensaver scene below is the full-screen
version.)

**Seeing it without the cheat code:**

```sh
castalia-desktop --theme human --repo . --easter-egg aurora
```

## 2. The Aurora, again — as a screensaver

The same curtains are a **first-class screensaver scene**, not just an egg:
`castalia-salvapantallas --mode aurora`, and "Aurora de Castalia" in the
Control Center's *Salvapantallas* page. One implementation, two homes — which
is the point: the egg is not dead code kept alive by a joke.

## 3. The entry that is on no menu — `tombatossals`

**Where:** the Start Menu search box.

**What happens:** typing `tombatossals` (or `aurora`, or `konami`) into the
search reveals **✦ Aurora de Castalia**, an entry that appears in no category
and no list. Enter launches the aurora screensaver full-screen.

**Implementation:** `CastaliaMenu::isSecretWord()` in
[`shell/panel/src/CastaliaMenu.cpp`](../shell/panel/src/CastaliaMenu.cpp) —
static and pure, so `tools/tests/test_flourishes.py` can pin the word list
against this file. To see the filtered menu without typing:

```sh
castalia-panel --theme human --repo . --screenshot /tmp/p.png \
    --menu-shot /tmp/m.png --menu-query tombatossals
```

---

## Keeping them alive

`tools/tests/test_flourishes.py` fails the build if the code and this document
drift apart: the key sequence, the secret words, the flourish names the desktop
accepts, and the fact that the desktop and the screensaver share **one**
aurora implementation are all asserted against what is written here.
