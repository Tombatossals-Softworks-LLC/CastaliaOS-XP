# Castalia OS — Vision & roadmap

## What we want to achieve

**A second, dignified life for the PCs the industry gave up on.** There are
tens of millions of capable early- and late-2000s computers sitting idle
because nothing modern will run well on them. Our goal is simple to state and
hard to earn: on that hardware, Castalia OS should boot in seconds, look
beautiful, run real applications, connect to the modern world where it safely
can, and never lose someone's data. A computer a family retired should become
a computer a family reaches for — for a child's homework, a workshop's records,
a maker's bench, a spare machine that just works.

We also want to prove a quieter point: that you can build something that feels
as comfortable as the desktops people loved in the 2000s **without borrowing a
single thing you weren't given** — no trademarks, no traced icons, no ripped
sounds. Original, legally clean, and lovingly engineered, top to bottom.

## Where it is today (v0.1.0 — developer preview)

Already real and verified:

- **Boots to its own graphical desktop** from a live ISO, under a real window
  manager, verified in QEMU.
- **The full shell:** desktop plane, EWMH taskbar/panel, categorised Start
  Menu, Control Center, file manager.
- **46 native applications** across Accessories, Games, System and
  Compatibility — every one rendered in all seven themes in CI and driven
  end-to-end under a real X server.
- **Seven-theme design system** from a single token file, with the new
  **Human** flagship as the default, enforced by a design-rule linter.
- **A themed installer** (graphical + text) over a loopback-proven backend.
- **A working Windows-compatibility demo** — a real Win32 program under Wine.
- **The full system-tools trio:** Software Center, Update Center (snapshots
  first) and Recovery Center, over a unit-tested Restore Points engine.
- **A shippable `.deb` + apt overlay repo** and a tag-driven release pipeline.

## The roadmap

We work in phases; the early ones are done, the rest are the plan.

**Done / in place**
- Phase 0 — research & the first native pixels (the panel PoC).
- Phase 1 — a bootable base ISO.
- Phase 2 — the graphical desktop.
- Phase 3 — the Control Center.
- Phase 5 — a real, tested installer (GUI + text).
- Phase 6 — Wine runs an original Win32 app.
- Phase 7 — a real EWMH taskbar.
- Phase 8 — Restore Points.
- **The "Human" edition** — the warm flagship theme, tangerine icon family,
  animated shell, dawn login greeter, and a faster event-driven taskbar.

**Next up (short term)**
- A downloadable live ISO and installable images for the general public.
- The media, DOSBox-X and ScummVM launchers to round out compatibility.
- Wi-Fi and printing polish across more real-world hardware.
- More original wallpapers and a finished cursor theme.

**On the way to 1.0**
- 1.0 hardening: stability, performance and hardware-certification passes on
  real FLOOR machines.
- A curated software catalogue and smoother Add/Remove experience.
- Broader localisation beyond the default Spanish UI.
- Accessibility beyond High Contrast (keyboard, magnification, screen-reader
  paths).

**Longer horizon**
- Editions for the full 32-bit (SSE2) and 64-bit fields; a legacy non-SSE2
  edition as a stretch goal.
- A native (non-Wine) answer for more of the everyday app set.
- A community around original, legally-clean software for old hardware.

## How you can help tell the story

We're a small independent studio, and press coverage is how projects like this
find the people who need them — the retro-computing and Linux communities,
teachers with a lab of old PCs, repair cafés, and anyone with a good machine
and nothing to run on it. If you're writing about Castalia OS, we'll get you a
build, a live demo or any imagery you need. Just email
**hello@tombatossalssoftworks.com**.
