# About Castalia OS — descriptions & features

Ready-to-use copy at three lengths, plus the feature list and the philosophy.
Quote any of it freely.

## Description — short (≈40 words)

Castalia OS is an original, XP-class desktop operating system for early- and
late-2000s PCs. Its own Qt shell, 46 first-party apps and a seven-theme design
system make a 512 MB machine feel fast and modern — with no Microsoft code or
artwork anywhere.

## Description — medium (≈90 words)

Castalia OS is an independent, legally-clean desktop operating system built
from scratch for the millions of Pentium 4- to Core 2-era PCs that modern
software abandoned. It pairs a de-systemd'd Debian base with an entirely
original Qt/C++ desktop shell, 46 native applications, and a seven-theme
design system driven from a single set of tokens. Its flagship "Human" edition
brings a warm mid-2000s look — chocolate titlebars, sun-orange accents, a
tangerine icon family. A managed compatibility layer runs old Windows, DOS and
ScummVM software. It boots, runs and recovers happily in 512 MB of RAM.

## Description — long (≈180 words)

Castalia OS is an original desktop operating system for early- and late-2000s
PCs — Pentium 4, Pentium D, Core Solo/Duo, Core 2 Duo, and contemporary AMD
Athlon/Sempron/Turion machines — built to make them feel fast, modern and
cared-for again. It is a curated Linux distribution on a de-systemd'd Debian
base (runit init, eudev, elogind), with a **custom Qt 5 / C++17 desktop shell**
— file manager, panel, Start Menu, Control Center — and **46 first-party
applications**, every one original and drawn with original artwork.

A single theme file drives everything coherently: the widgets, the window
decorations, the 39-icon tangerine family, the login greeter and the sounds
all change together, across **seven themes** — including the warm **Human**
flagship, a **Medianoche** dark mode, and a WCAG-AAA **High Contrast** mode —
with continuous-integration checks enforcing colour contrast and gradient
safety. A managed compatibility layer runs original Windows programs under
Wine in per-app sandboxes, with a Classic Games launcher for DOSBox-X and
ScummVM. Restore Points, an
independent recovery environment and a snapshot-first update centre make it
safe to trust with someone's only computer. It is **not** a Windows clone and
contains **no** Microsoft code, branding or assets.

## Key features

**The desktop**
- Original Qt 5 / C++17 shell: desktop plane, EWMH taskbar/panel, categorised
  Start Menu, Control Center, file manager (Castalia Explorer).
- Event-driven taskbar (updates the instant a window opens/closes/renames —
  no per-second polling) and a minute-aligned clock, for a light idle load.
- Short, tasteful animations (menu rise-and-fade, icon hover halos, installer
  transitions) — all ≤ 200 ms and switchable off for reduced motion or the
  lowest-end hardware.
- A Control Center wallpaper picker (six original per-theme wallpapers) that
  the desktop applies live, with no re-login.

**The look — one system, seven themes**
- Human (flagship, warm), Classic (sandstone + azure), Azul, Oliva, Plata,
  Medianoche (dark) and High Contrast (accessibility, WCAG AAA).
- All derived from a single `theme.conf`; a CI linter enforces WCAG contrast
  minima and 16-bit-safe gradients, so no theme can ship broken.
- 39 original tangerine icons on one 48-px grid; 9 original mouse pointers
  as a real Xcursor theme; original wallpapers; an
  original login banner and greeter.

**The applications (all first-party, all original art)**
- *Accessories:* Explorer, Notas (plain text), Escritor (rich text →
  HTML/ODT), Pintura (paint), Calculadora, Character Map, Clock (analog +
  stopwatch + alarm), Calendar (month view + per-day notes, opens from the
  panel clock), Media Player (playlist that delegates to mpv/VLC/mplayer),
  Magnifier, Sticky Notes, Screenshot, Image Viewer, Archive Manager.
- *Games:* Buscaminas (Minesweeper-class) and Solitario — clean-room, original
  art, each with an automated rules self-test.
- *System:* File Search (recursive, background-threaded), Terminal (own
  VT100 emulator), System Monitor, Volume Control
  (PulseAudio/ALSA; opens from the tray speaker), Diagnostics (with a
  real CPU/RAM/disk/graphics/network benchmark suite), Software Center,
  Update Center (auto-Restore-Point first), Recovery Center.
- *Compatibility:* Windows application manager (Wine, per-app prefixes) and a
  Classic Games launcher for DOSBox-X (MS-DOS) and ScummVM (adventure games).
- *Accessibility:* On-screen keyboard (types via XTEST without stealing focus)
  and the Magnifier.
- Plus the Control Center, a Welcome/Help center, a screensaver and the
  graphical installer.

**Trust & recovery**
- Restore Points engine (unit-tested), independent recovery environment,
  safe-mode path.
- Guided graphical installer + text-mode fallback; the installer refuses every
  destructive step until you type the exact target disk, and installs fully
  offline with no online account.

**Engineering discipline**
- No Microsoft (or other third-party) code, branding, icons, sounds or
  wallpapers. Every shipped asset is original or libre and tracked in a
  provenance ledger that CI enforces.
- Four CI pipelines: per-commit checks (lint, unit tests, the design/legal
  gates, every app rendered in every theme, a live end-to-end run of the whole
  suite under a real window manager, and a .deb build-install-run gate),
  nightly full-ISO + QEMU boot, an on-demand desktop-ISO proof, and a
  tag-driven release pipeline.

## Philosophy

> *Familiar, but ours. Retro, but alive. Beautiful, but light.*

Three convictions drive Castalia OS:

1. **Old hardware deserves good software.** A computer that still turns on is
   not waste. The floor — 512 MB of RAM, a Pentium 4 — is a first-class
   target, not a degraded mode.
2. **Comfort without theft.** The 2000s desktop was comfortable for a reason.
   We keep the comfort and owe nothing to anyone's trademarks: every asset is
   original or freely licensed, by construction and under CI enforcement.
3. **A computer you can trust.** For many people the old PC is the *only* PC.
   So the system is recoverable by design — snapshots before updates, Restore
   Points, an independent recovery path — and honest about what it is.
