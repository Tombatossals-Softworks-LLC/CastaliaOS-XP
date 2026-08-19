# Castalia OS — Fact Sheet

| | |
|---|---|
| **Product** | Castalia OS |
| **Current edition** | Castalia OS "Human" (developer preview) |
| **Developer** | Tombatossals Softworks |
| **Creators** | Dave Abellán · Claudio di Castello |
| **Type** | Desktop operating system (independent Linux distribution + original desktop shell) |
| **Release status** | Working developer preview · version 0.1.0 · in active development |
| **Price** | Free · open source (MIT-licensed source) |
| **Languages** | Spanish (default UI); architecture is fully localizable |
| **Platforms** | PC — 64-bit (amd64) and 32-bit (i386, SSE2) |
| **Boots on** | Pentium 4 and up · from **512 MB RAM** · 800×600 · 8 GB disk |
| **Toolkit** | Qt 5.15 LTS + C++17 (shell and all first-party apps) |
| **Base** | Debian stable, de-systemd'd (runit init · eudev · elogind) |
| **First shown** | 2026 |
| **Website** | https://tombatossalssoftworks.com |
| **Press contact** | hello@tombatossalssoftworks.com |

## In one sentence

Castalia OS is an original, XP-class desktop operating system — its own Qt
shell, 46 first-party applications, a seven-theme design system and a managed
Windows/DOS/ScummVM compatibility layer — built to make ten-to-twenty-year-old
PCs feel fast, modern and cared-for, without a single line of Microsoft code
or a byte of Microsoft artwork.

## The numbers

- **38** native Qt/C++ applications, all first-party, all original artwork.
- **7** coherent themes from one set of design tokens — including **Human**
  (the warm flagship), a **Medianoche** dark mode and an accessibility
  **High Contrast** theme that meets WCAG AAA.
- **39** original tangerine icons on a single 48-px grid.
- **9** original mouse pointers, shipped as a real multi-size Xcursor theme.
- **7** original system sounds, synthesised from one spec file and played on
  real events (session start/end, emptying the bin, errors).
- **512 MB RAM** hardware floor — the *entire* desktop, not a cut-down mode.
- **0** Microsoft (or any third-party) code, branding, icons, sounds or
  wallpapers. Every shipped asset is original or libre and tracked in a
  provenance ledger that CI enforces.

## What makes it different

- **It actually boots.** A live ISO brings up a real graphical desktop under
  a real window manager — verified in QEMU, not mocked up.
- **One switch changes everything.** A single theme file drives the Qt
  widgets, the window decorations, the icons, the greeter and the sounds
  together — coherently, and checked by CI for contrast and 16-bit-era
  gradient safety.
- **It respects old hardware as a feature, not a fallback.** No compositor
  required, tiny RAM budget, event-driven (not polling) shell.
- **It's legally clean by construction.** "Familiar, but ours" — the comfort
  of the 2000s desktop, none of the trademarks.
- **You can recover it.** Restore Points, an independent recovery
  environment, and an update centre that snapshots before it touches anything.

## Availability

Castalia OS is a working developer preview. The source, the design bible and
the build system are open; a downloadable live ISO and installable images are
the next milestone. Press are welcome to request a current build or a live
walkthrough at **hello@tombatossalssoftworks.com**.
