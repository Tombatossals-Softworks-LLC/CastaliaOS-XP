# Castalia OS — Project Bible & Technical Roadmap

> **Product family:** Castalia Classic
> **Publisher:** Tombatossals Softworks
> **Document type:** Engineering design bible / technical roadmap
> **Status:** v0.9 — Foundational (pre-Phase 0 sign-off)
> **Audience:** Founding engineer(s), contributors, packagers, QA, technical writers
> **Last revised:** 2026-07-08

---

## About this document

This is the authoritative design document for **Castalia OS**, an original,
legally-clean, XP-*class* desktop operating system distribution targeting
early- and late-2000s x86 PCs. It is written so that a senior systems engineer
can create the repository, stand up the build, and begin implementation
immediately. Where a decision is genuinely open, this document presents the
options, weighs them, and **chooses one** — it does not hand-wave.

**Assumption-marking convention.** Statements that depend on facts not yet
verified on target hardware are prefixed with **`[ASSUMPTION]`**. Statements
that are firm product decisions are prefixed with **`[DECISION]`**. Open items
requiring a spike/prototype are prefixed with **`[SPIKE]`**.

**Naming note.** Internally the project is *Castalia OS*; the shipping product
line is *Castalia Classic*. "XP-class" is used throughout as a *category*
descriptor (like "IBM-PC-compatible"), never as branding. See §3.

---

## Table of contents

1. [Executive Summary](#1-executive-summary)
2. [Product Philosophy](#2-product-philosophy)
3. [Legal and Branding Strategy](#3-legal-and-branding-strategy)
4. [Target Hardware Matrix](#4-target-hardware-matrix)
5. [Base OS Comparison](#5-base-os-comparison)
6. [System Architecture](#6-system-architecture)
7. [Desktop / Shell Architecture](#7-desktop--shell-architecture)
8. [Visual Design System](#8-visual-design-system)
9. [Built-in Applications](#9-built-in-applications)
10. [XP Feature Parity Map](#10-xp-feature-parity-map)
11. [Compatibility Strategy](#11-compatibility-strategy)
12. [Programming Languages and Frameworks](#12-programming-languages-and-frameworks)
13. [Package and Update Model](#13-package-and-update-model)
14. [Installer and First Boot Experience](#14-installer-and-first-boot-experience)
15. [Security Model](#15-security-model)
16. [Performance Budgets](#16-performance-budgets)
17. [Build System and Repository Structure](#17-build-system-and-repository-structure)
18. [Development Roadmap](#18-development-roadmap)
19. [QA and Hardware Certification](#19-qa-and-hardware-certification)
20. [Documentation Set](#20-documentation-set)
21. [Branding and Lore](#21-branding-and-lore)
22. [Risks and Brutal Reality](#22-risks-and-brutal-reality)
23. [Final Recommendation](#23-final-recommendation)

---

## 1. Executive Summary

**Castalia Classic** is an original desktop operating system distribution built
on a Linux base, designed to be the definitive, comfortable, *XP-class*
replacement for early-2000s and late-2000s PCs. It targets Intel Pentium 4,
Pentium D, Core Solo/Duo, Core 2 Duo, and contemporary AMD Athlon /
Athlon 64 / Sempron / Turion machines — hardware that Microsoft, Google, and
mainstream Linux desktops have effectively abandoned, but which still boots,
still works, and still deserves a coherent, beautiful, safe, and *maintainable*
software environment.

**What it is:**

- A real, cohesive OS **product** — an installable distribution with its own
  shell (*Castalia Explorer*), its own control surface (*Castalia Control
  Center*), its own theme, icon, sound, and boot identity, and its own
  update/recovery story.
- A **local-first, offline-capable** desktop: no cloud dependency, no forced
  accounts, no telemetry by default.
- A **compatibility platform**: native lightweight Linux apps first, plus
  curated **Wine**, **DOSBox-X**, and **ScummVM** integration for selected
  Windows and DOS-era software, delivered through a friendly, managed UI.
- A **fast, recoverable** system that boots quickly on 512 MB–2 GB machines,
  survives power loss and bad updates, and can be repaired by an ordinary
  human with the on-machine tools and offline documentation.

**What it is NOT:**

- It is **not** Windows, not "WinXP", not a Microsoft clone, and contains **no
  Microsoft code, branding, icons, sounds, wallpapers, boot screens, or copied
  UI assets** (see §3).
- It is **not** a from-scratch kernel or a ReactOS-style Win32
  reimplementation. That road is a multi-decade, multi-team effort that has
  never produced a shippable daily-driver, and it is a trap for a small team
  (see §22).
- It is **not** "a theme pack on top of XFCE." Themes are the *surface*; the
  product is the shell, the control surface, the compatibility layer, the
  update/recovery system, and the coherence that ties them together.

**Who it is for:**

- Owners of P4-through-Core-2 machines who want a safe, pleasant, still-updated
  system instead of an unsupported, exploit-ridden Windows XP install.
- Retro-computing enthusiasts, offline/air-gapped users, kiosks, hobby labs,
  schools and makers in cost-constrained settings, and the nostalgia audience
  who want the *feel* of the XP era without its 2026 security liabilities.
- People who value a computer they **own and can repair** — no subscriptions,
  no accounts, no surveillance.

**Why it should exist:** Hundreds of millions of these machines were built.
Many still run. XP is a security corpse; Windows 10/11 will not install; modern
GNOME/KDE assume GPUs and RAM these machines never had. There is a real,
underserved niche for a *designed, opinionated, beautiful, honest* OS that
treats old hardware as a first-class target rather than a legacy afterthought.
No current project occupies exactly this position with a cohesive XP-class UX
and a legally-clean, original identity.

**The one-line thesis:** *Ship a curated, glibc Linux distribution with a
custom Qt shell, an XP-class UX, and a managed Wine/DOS compatibility layer —
lightweight at v1, progressively replacing generic components with original
Castalia ones — rather than cloning an OS.*

---

## 2. Product Philosophy

The soul of Castalia Classic is a short list of non-negotiable principles. Every
design decision in this document is traceable to one of them. When two
principles conflict, the earlier one wins.

| # | Principle | What it means in practice |
|---|-----------|---------------------------|
| P1 | **Respect old hardware** | The minimum target (P4, 512 MB, GMA/AGP GPU, 800×600) is a *first-class* citizen, tested every release — not a "best effort" tier. No feature ships if it makes the minimum tier worse. |
| P2 | **Fast** | Cold boot to usable desktop within budget (§16). Menus open in <150 ms. Idle desktop RAM within budget. Perceived speed beats feature count. |
| P3 | **Familiar but original** | The *workflow* and *discoverability* of the XP era — bottom taskbar, a single launch menu, a spatial file manager, a clear Control Center — reproduced with **entirely original** names, art, and code. Comfort without copying. |
| P4 | **Beautiful but lightweight** | A cohesive, premium visual language (§8) that runs with the compositor **off**. Beauty is achieved with restraint (clean gradients, crisp icons, good typography), not with GPU-hungry effects. |
| P5 | **Repairable by humans** | Plain-text config, readable logs, documented layout, a recovery environment, and offline help. A knowledgeable user can fix the machine without the internet. No opaque binary state that only a vendor tool can touch. |
| P6 | **Local-first** | The computer is complete and useful with the network cable unplugged. Cloud is optional and additive, never required. |
| P7 | **No forced accounts, no telemetry** | No online account to log in. No data collection by default. Any diagnostics are opt-in, local, and inspectable. |
| P8 | **Recoverable** | Bad update, power loss, full disk, or a botched config must be survivable. Restore points, a safe-mode session, and a recovery boot entry are core features, not add-ons. |
| P9 | **Clear settings** | One obvious place for each setting, plain language, no dead ends. If a setting exists, it is discoverable and reversible. |
| P10 | **Honest** | The product tells the truth about what works, what is emulated, and what will never work (anti-cheat, kernel Windows drivers, modern DirectX). No dark patterns, no overpromising. |

**Design tenets derived from the principles:**

- **Progressive originality.** v1 may lean on proven upstream components
  (Openbox, PCManFM-Qt patterns, network-manager, CUPS). Each release replaces
  a generic component with a cohesive Castalia one *only when* the replacement
  is at least as good and as light. We never regress P1/P2 for the sake of
  "made in-house."
- **Boring where it counts.** Kernel, libc, Xorg, filesystems, and the package
  core are deliberately conservative and well-trodden. Innovation budget is
  spent on UX, cohesion, and compatibility — not on reinventing plumbing.
- **Defaults are the product.** Most users never change a setting. The default
  theme, default apps, default security posture, and default power profile are
  chosen with the same care as a commercial product's out-of-box experience.
- **Every megabyte and millisecond is a design decision.** Performance budgets
  (§16) are enforced in CI, not aspired to.

---

## 3. Legal and Branding Strategy

> **This section is a hard constraint, not a guideline.** A single infringing
> asset can sink the project. When in doubt, the answer is "create it
> original." Nothing described here is legal advice; before a public release,
> a real IP attorney reviews the name, marks, and asset provenance.

### 3.1 The core rule

Castalia Classic reproduces the *ideas, ergonomics, and feel* of an era. It
copies **no protected expression**. Trademarks protect *names and logos*;
copyright protects *specific creative expression* (icons, sounds, wallpapers,
exact UI art, code). Ideas, layouts, and workflows (a taskbar at the bottom, a
launch menu at the corner, a two-pane file manager) are **not** protectable and
are fair to emulate. Our entire strategy lives in that gap: **emulate the
uncopyrightable workflow, originate all protected expression.**

### 3.2 Naming rules

**[DECISION]** Product and component names must be original and must not be
"confusingly similar" to Microsoft marks.

| Allowed | Forbidden |
|---------|-----------|
| Castalia OS, Castalia Classic, Castalia Explorer, Castalia Control Center | Windows, WinXP, XP, Windows Classic, "Windows-compatible OS" |
| "XP-class" as a *category* adjective (rare, in prose only) | "XP" or "Windows" in any product, package, menu, or file name |
| "Compatible with selected Windows® applications via Wine" (see §3.7) | "Runs Windows", "Windows inside", "a better Windows" |
| Original component names: *Restore Points*, *Software Center*, *Hardware Center* | "Control Panel", "My Computer", "My Documents", "Windows Update", "Add/Remove Programs" as literal product names |

- The **legal entity / studio** is *Tombatossals Softworks*; it appears in
  About boxes, boot credit, and copyright lines.
- Component names use plain descriptive words (*Explorer* is a common English
  word and a generic UI metaphor; it is acceptable, but we lead with
  *Castalia Explorer* to avoid any single-word collision).
- Version code-names use a neutral, original scheme (see §21) — never Windows
  code-names.

### 3.3 Visual rules

- **No Luna.** No blue/green rounded XP taskbar, no green Start orb, no XP
  title-bar gradient reproduction, no "Bliss"-style hill wallpaper. Our default
  theme (§8) uses an **original** color story (Mediterranean stone/azure) and
  **original** window-control geometry.
- Window controls (min/max/close), title bars, scrollbars, and buttons are
  drawn from our own vector spec. They may be *flat-with-a-hint-of-depth* like
  the era generally, but must not trace or recolor Microsoft's specific art.
- No screenshots of Windows anywhere — not in docs, not in marketing, not in
  the installer.

### 3.4 Iconography rules

- Every icon is drawn originally, from a documented grid and palette (§8), by
  us or under a license we can legally ship (see §3.9). No Microsoft icon is
  copied, traced, recolored, or "reinterpreted at the pixel level."
- Metaphors are fine (a folder looks like a folder; a gear means settings; a
  disk means storage). **Specific artwork is ours.**
- We do **not** reuse icon sets that are themselves XP clones (e.g. some
  "XP-style" Linux icon themes are legally tainted). All third-party art is
  provenance-checked (§3.9).

### 3.5 Sound rules

- All system sounds (startup, shutdown, error, notify, device-connect) are
  **original compositions** by us or explicitly licensed (CC0/CC-BY with
  attribution recorded). No Microsoft startup/shutdown jingle, no "tada",
  no XP error dings — not sampled, not "recreated by ear from memory."
- The sound palette (§21) has its own musical identity (a short, warm,
  Mediterranean-tinged motif) so it is recognizably *Castalia*, not a pastiche.

### 3.6 Theme rules

- Bundled themes (Classic, Azul, Oliva, Plata, Medianoche, High Contrast) are original
  color systems with original gradients. We may evoke *the general era*
  (subtle vertical gradients, a colored title bar) but never reproduce a
  specific Microsoft theme's exact colors, geometry, or art.
- The theme engine ships **no** "Luna", "Royale", "Zune", or "Classic Windows"
  presets.

### 3.7 Documentation & compatibility wording

- **Truthful, narrow, trademark-respectful.** Permitted:
  *"Castalia Classic can run many Windows® applications through the Wine
  compatibility layer. Wine is not a Microsoft product and results vary by
  application."*
- `Windows®` is written with the ® and a footnote: *"Windows is a registered
  trademark of Microsoft Corporation. Tombatossals Softworks and Castalia OS
  are not affiliated with, endorsed by, or sponsored by Microsoft."*
- Never imply endorsement, affiliation, or that Castalia *is* Windows.
- Compatibility claims are **per-application and evidence-based** (from our
  compat DB, §11), never blanket ("runs all your Windows software" is
  forbidden — it is both false and legally reckless).

### 3.8 Trademark-safe language cheat-sheet

| Say this | Not this |
|----------|----------|
| "Launch menu" / "Castalia Menu" | "Start menu" |
| "Taskbar" (generic, acceptable) | "the Windows taskbar" |
| "Software Center" | "Add/Remove Programs" |
| "Hardware Center" | "Device Manager" |
| "Update Center" | "Windows Update" |
| "Restore Points" | "System Restore" |
| "Files" / "Castalia Explorer" | "Windows Explorer" |
| "Compatible with selected Windows® apps via Wine" | "Windows compatible" (bare) |

### 3.9 Asset provenance & the "clean room" discipline

- **Provenance ledger.** Every shipped asset (icon, sound, font, wallpaper,
  code snippet) has a row in `/legal/ASSET_PROVENANCE.csv`: source, author,
  license, license URL, and the commit that added it. CI fails the build if a
  binary asset lands without a ledger row (§17).
- **Fonts.** Ship only fonts with OFL/GPL-FE/permissive licenses (e.g. DejaVu,
  Liberation, Noto, an original display face). Never ship Microsoft fonts
  (Tahoma, Segoe, etc.). The UI font is an **original or libre** family chosen
  for legibility at 800×600 (§8, §21).
- **No decompilation, no asset extraction.** No one on the project extracts
  art/sound from Windows media, "for reference" or otherwise. Reference is
  *from memory of the workflow*, never from copied files.
- **Third-party code** is tracked in `/legal/THIRD_PARTY.md` with SPDX
  identifiers; license compatibility is checked in CI. GPL obligations
  (source offer, notices) are satisfied by the packaging pipeline (§13, §17).

### 3.10 How we get "XP-class" without infringing — summary

We reproduce **behavior and ergonomics** (bottom taskbar, corner launch menu,
tray clock, a friendly Control Center, spatial file browsing, "it just works"
discoverability) and originate **all expression** (name, logo, icons, sounds,
wallpapers, theme art, boot/login screens, code). The result *feels* like the
comfortable era it evokes while being unmistakably, defensibly *Castalia*.

---

## 4. Target Hardware Matrix

Castalia ships in a small number of **build profiles** rather than one
one-size-fits-all image. This keeps the minimum tier honest and lets us tune
defaults per class of machine.

### 4.1 Build editions

| Edition | Arch / ISA baseline | Kernel/init tuning | Default desktop mode | Intended machine |
|---------|--------------------|--------------------|----------------------|------------------|
| **Castalia Classic 32** | i686 + **SSE2 required** (`-march=pentium4 -mtune=generic` class) | No compositor by default; conservative I/O | Openbox + Castalia panel, effects off | Pentium 4 / Pentium D / Athlon XP-64 / Sempron |
| **Castalia Classic 64** | x86-64 (`x86-64` baseline, i.e. SSE2) | Compositor optional/auto | Full Castalia shell, effects auto | Core 2 Duo / Athlon 64 X2 / Turion 64 and up |
| **Castalia Legacy 32 (special)** | i686 **without SSE2** (Pentium III / early Athlon / Pentium M pre-SSE2) | Minimal; text-first; strict | Openbox only, no compositor, minimal apps | **[DECISION-with-caveat]** see §4.6 |

**[DECISION]** The mainstream 32-bit edition **requires SSE2**. SSE2 is present
on all Pentium 4, Pentium D, Athlon 64, and later. Requiring it lets us build
faster, smaller binaries and match how the wider ecosystem (browsers, Wine,
Mesa) is trending. Non-SSE2 machines are served only by the *Legacy* special
build (§4.6), which is explicitly lower-support.

### 4.2 CPU / ISA tiers

| Tier | Examples | Support level |
|------|----------|---------------|
| **T1 Recommended** | Core 2 Duo, Core Duo, Athlon 64 X2, Turion 64 X2 | Full: 64-bit edition, all features tested each release |
| **T2 Primary-minimum** | Pentium 4 (Prescott/Northwood), Pentium D, Athlon 64, Sempron (SSE2) | Full on 32-bit edition; the *floor* the product promises |
| **T3 Secondary/best-effort** | Pentium M (SSE2 models), early Atom, low-end Athlon 64 | Runs, tested opportunistically, may disable heavy optionals |
| **T4 Legacy-special** | Pentium III, pre-SSE2 Athlon, Pentium M pre-SSE2, Via C3/C7 | *Legacy* build only; community-tested; no guarantees (§4.6) |
| **Unsupported** | 486/early Pentium (no CMOV/no PAE), non-x86 | Out of scope for v1 |

### 4.3 RAM tiers

| RAM | Experience | Notes |
|-----|-----------|-------|
| **256 MB** | Text install + minimal Openbox only; **not** the graphical target | Below graphical floor; document as "expert/minimal" |
| **512 MB** | **Graphical minimum.** Shell + one light app at a time. Compositor off. Browser is heavy — use a light one (§16). | Enforced idle-RAM budget (§16) |
| **1 GB** | **Comfortable.** Shell + a couple of apps + a modest browser. | Recommended floor for daily use |
| **2 GB** | **Excellent.** Compositor optional-on, Wine apps, media playback. | Sweet spot for Core 2 tier |
| **4 GB+ (PAE / 64-bit)** | Headroom; multiple Wine prefixes, VMs/emulators. | 32-bit edition uses PAE to see >4 GB where the chipset allows |

### 4.4 GPU tiers

| Tier | Hardware | Driver | 2D | 3D | Compositor default |
|------|----------|--------|----|----|--------------------|
| **G1** | Intel GMA 900/950/3100/X3100/4500 (i915) | `xf86-video-intel` / modesetting + Mesa classic/crocus | Good | Basic GL (fixed-fn / GL2) | Off (32) / auto (64) |
| **G2** | NVIDIA GeForce 6/7/8/9 (pre-Fermi) | **nouveau** (open) | Good | GL2-ish, variable | Off by default; user-enable |
| **G3** | ATI Radeon 9000–X1000 / HD 2000–4000 | `radeon` (open) | Good | GL2/GL3 partial | Off by default; user-enable |
| **G4** | VESA/framebuffer only (unknown GPU) | `modesetting`/`fbdev`/`vesa` | Usable | None | Off (forced) |
| **G5** | Legacy NVIDIA/ATI needing proprietary blobs | *Optional* non-free repo where legal (§13) | — | — | Case-by-case |

**[DECISION]** Xorg only for v1 (per project preference and old-GPU reality).
Wayland is deferred indefinitely (§6). The compositor is **picom**, disabled by
default on 32/G1–G4, auto-enabled only when the GPU advertises adequate GL and
RAM ≥ 2 GB (detected at first boot, §14).

### 4.5 Storage, audio, network, laptop tiers

| Domain | Supported | Notes |
|--------|-----------|-------|
| **Storage** | PATA/IDE, SATA (AHCI + legacy IDE mode), USB mass storage, CF/SD via readers; ext4 default | Old BIOS/MBR primary; UEFI optional (§6). Disk floor 8 GB, recommended 16 GB plus |
| **Optical** | IDE/SATA CD/DVD for live-boot on machines without USB-boot | isolinux El-Torito ISO (§14) |
| **Audio** | Intel HDA, AC'97, common PCI codecs, USB audio | ALSA + PipeWire-lite or bare ALSA on 512 MB (§6) |
| **Network — wired** | Realtek 8139/8169, Intel e100/e1000, VIA, nForce, Broadcom (open) | **First-class.** Ethernet is the guaranteed path |
| **Network — Wi-Fi** | Where an in-tree/open driver exists (ath5k/ath9k, rtl8187, b43 best-effort, iwlwifi w/ firmware) | **Best-effort.** Firmware from optional non-free repo where legal (§13) |
| **Input** | PS/2 keyboard+mouse, USB 1.1/2.0 HID, synaptics touchpads | PS/2 must work with no config |
| **USB** | 1.1 (UHCI/OHCI) + 2.0 (EHCI); mass storage, HID, printers, audio | USB 3.0 where present, no guarantee on this hardware |
| **Printers** | CUPS + Gutenprint + PostScript/PCL; network + USB | §6 printing stack |
| **Laptops** | Lid/brightness/battery via ACPI; suspend best-effort | See below |

### 4.6 Power management & suspend/resume expectations

- **[DECISION]** ACPI battery, lid, and brightness are supported and tested on
  the T1 laptop reference. **Suspend-to-RAM (S3)** is **best-effort** and
  machine-specific on this vintage; it is exposed but the installer's hardware
  probe (§14) marks it *"tested/untested/known-broken"* per detected model from
  a shipped quirks table. **Hibernate (S4)** is **off by default** (swap-sizing
  and resume reliability on old BIOSes are poor) and gated behind an "advanced"
  toggle with a clear warning.
- CPU frequency scaling via `cpufreq` (ondemand/schedutil) is on by default on
  laptops; a *Performance / Balanced / Power-saver* profile picker lives in the
  Control Center (§9).

### 4.7 The non-SSE2 Legacy question — decided

**[DECISION]** Non-SSE2 i686 hardware (Pentium III, pre-SSE2 Athlon, some
Pentium M) is **excluded from the mainstream editions** and served, *if at all*,
by a separate, minimal **Castalia Legacy 32** build:

- Rationale (brutal): SSE2 is assumed by modern Wine, Mesa, Firefox-ESR-class
  browsers, and increasingly by distro toolchains. Supporting non-SSE2 doubles
  the build/test matrix, forbids `-msse2`, and forces an ancient browser and
  older Wine — for a shrinking sliver of machines. That cost is not justified
  in the mainstream product.
- What Legacy is: a text-first + Openbox minimal image, no compositor, curated
  micro-apps, no bundled modern browser (offline help + a minimal browser
  only), community-maintained, "here be dragons." It exists so the *ideas* of
  Castalia reach the oldest hardware without dragging down the main line.
- **[SPIKE]** Legacy is a Phase-8+ stretch, not a v1 commitment. If it proves
  to double maintenance cost, it is cut without apology (§22).

### 4.8 Known-problematic / unsupported hardware

| Hardware | Status | Reason |
|----------|--------|--------|
| Winmodems / softmodems | Unsupported | Proprietary DSP blobs, dead ecosystem |
| GPUs needing modern proprietary-only drivers | Best-effort/unsupported | No open driver; blobs may not build on old X |
| Fakeraid (BIOS "RAID") on old chipsets | Discouraged | Fragile; recommend plain AHCI/IDE |
| Exotic Wi-Fi needing out-of-tree blobs | Best-effort | Firmware/legal/maintenance burden |
| Anything requiring Secure Boot signing | N/A for v1 | Target machines are BIOS/MBR; UEFI+SB deferred (§6) |
| Machines with <256 MB RAM | Unsupported | Below any usable floor |
| Non-CMOV / non-PAE CPUs (pre-P6) | Unsupported | Toolchain + kernel baselines |

---

## 5. Base OS Comparison

This is the highest-leverage decision in the project. It is evaluated
brutally. The scoring axes, per the brief, are: 32-bit outlook, old-hardware
suitability, package availability, build reproducibility, security-update path,
desktop-stack availability, driver support, maintenance burden, developer
experience, long-term viability, licensing, and risk.

### 5.1 The two questions that decide everything

Two facts collapse most of the option space:

1. **libc.** Our headline feature is *running old Windows software via Wine*
   and *broad old-hardware/driver support*. Both are dramatically better on
   **glibc** than on **musl**. Wine, Mesa's classic/legacy drivers, many
   prebuilt helper libraries, and countless third-party binaries assume glibc.
   musl is superb for containers and minimalism; it is a **self-inflicted
   wound** for an XP-*compatibility* desktop. → **Prefer glibc.**

2. **Product model.** We are shipping a *general-purpose desktop that users
   install software onto and receive security updates for* — not a fixed
   firmware image. That rules out build-system-as-distro approaches
   (Buildroot/Yocto) for the *base*, because they produce appliances without a
   real on-device package manager or a security-update stream. → **Need a
   distribution with a package manager and a security channel.**

Everything below is scored against those two anchors.

### 5.2 Candidate scorecard

Scores are 1 (poor) – 5 (excellent) **for our specific goals**, not in the
abstract. "32-bit" weighs heavily; "Wine/desktop" weighs heavily.

| Base | 32-bit outlook | Old-HW fit | Pkg availability | Reproducible build | Security updates | Desktop stack | Drivers | Maint. burden* | Dev exp. | Longevity | License | Risk |
|------|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| **Debian stable (glibc)** | 5 | 5 | 5 | 4 | 5 | 5 | 5 | 4 | 4 | 5 | Free | **Low** |
| **Devuan / antiX (Debian, no systemd)** | 5 | 5 | 5 | 4 | 5 | 5 | 5 | 4 | 4 | 5 | Free | **Low** |
| **Void Linux (glibc, runit)** | 3 | 4 | 3 | 4 | 3† | 4 | 4 | 3 | 4 | 3 | Free | Med |
| **Alpine (musl)** | 3 | 4 | 3 | 5 | 4 | 3 | 3 | 3 | 4 | 4 | Free | Med-High‡ |
| **Buildroot** | 4 | 5 | 1 | 5 | 1 | 2 | 3 | 2 | 3 | 3 | Free | High‡ |
| **Yocto** | 4 | 4 | 2 | 5 | 2 | 3 | 4 | 1 | 2 | 4 | Free | High‡ |
| **LFS/BLFS** | 5 | 5 | 1 | 1 | 1 | 3 | 4 | 1 | 2 | 2 | Free | High |
| **ReactOS (fork)** | 4 | 3 | 1 | 2 | 1 | 2 | 1 | 1 | 2 | 2 | Free | **Extreme** |
| **Haiku (fork)** | 3 | 3 | 2 | 3 | 2 | 3 | 2 | 1 | 2 | 3 | Free | **Extreme** |

\* *Maintenance burden* is scored as **higher = better** (5 = least burden for
our small team). † Void is rolling: "security updates" arrive fast but the
target moves under you, which is its own risk for a stability product.
‡ Risk here is *mission-fit* risk, not code quality.

### 5.3 Per-candidate honest read

- **Debian stable / Devuan / antiX (glibc)** — *The pragmatic winner.* The best
  32-bit (i386) longevity in the ecosystem, the **largest** package archive (so
  we repackage almost nothing), a **real LTS security channel**, the deepest
  old-hardware/driver coverage, and mature Xorg for GMA/nouveau/radeon. antiX
  and MX **already prove** Debian runs beautifully on P4/512 MB with SysVinit or
  runit. The only "downside" — it can feel like generic Debian — is irrelevant,
  because our differentiation is the **shell, control surface, compatibility
  layer, and identity on top**, not the plumbing. **Devuan/antiX lineage**
  additionally gives us Debian's archive *without systemd*, matching our
  fast-boot/repairable ethos.

- **Void Linux (glibc, runit)** — *The strong alternative.* Lighter base, very
  fast boot (runit), a genuinely nice package manager (XBPS), rolling so Wine/
  Mesa are always current. But: **rolling** conflicts with "stable retro
  appliance"; the archive is far smaller than Debian's (we'd repackage more);
  32-bit x86 support, while present, has been openly discussed as a candidate
  for reduction, which is an **existential risk** for a 32-bit-centric product;
  and there is no LTS security track. Excellent engineering, wrong risk profile
  for our *floor* promise. **Kept as the documented fallback base.**

- **Alpine (musl)** — Superb minimalism and reproducibility; postmarketOS shows
  full desktops are possible. But musl degrades our two headline features (Wine
  fidelity, binary/driver compatibility), 32-bit is second-class, and the
  desktop/Wine ecosystem is thinner. We would spend our scarce engineering
  budget fighting libc instead of building UX. **Rejected as base.** *(We may
  still use Alpine/musl internally for tiny utility containers — not the OS.)*

- **Buildroot** — A cross-compile image builder for embedded appliances. **No
  on-target package manager, no security stream, no user-installable software**
  in the normal sense. Wonderful for a fixed kiosk firmware; categorically the
  wrong model for "an OS people install programs on." **Rejected as base.** May
  be used later to build the tiny **recovery initramfs** (§6) where an appliance
  image is exactly right.

- **Yocto** — Buildroot's heavyweight cousin, aimed at product lines with a
  dedicated build team. Multi-hour builds, steep BitBake learning curve, and
  the same appliance model problem. For a small team this is a **maintenance
  black hole**. **Rejected as base.**

- **LFS / BLFS** — Priceless as *education and reference* (every engineer on the
  project should read it), but not a maintainable *product* base: no package
  manager, no security channel, everything hand-built. **Use as learning/
  reference only**, exactly as the brief suggests.

- **ReactOS (fork)** — Reimplements the Windows NT kernel + Win32 from scratch,
  clean-room. After 25+ years it remains alpha, crashes on real workloads, has
  thin driver support, and is legally the *most* exposed posture (it lives or
  dies on clean-room rigor). Forking it to make a daily driver is a
  multi-team-decade effort we cannot staff. **Rejected.** (Its *lessons* inform
  §22.) We get Win32 compatibility far more cheaply via **Wine on Linux**.

- **Haiku (fork)** — Beautiful, cohesive, spiritually aligned with "designed OS
  for its own sake." But it is a *different* OS with a small driver base, a
  small app ecosystem, no glibc/Linux driver reuse, and **no XP-class Win32
  compatibility path**. Forking it abandons our biggest asset (the Linux driver
  + Wine ecosystem). **Rejected** — admired, but wrong tool.

### 5.4 Decision

**[DECISION] Base = Debian stable, de-systemd'd, Devuan/antiX lineage.**

- **Concretely:** Track **Debian stable** for the package archive and security
  updates; use the **Devuan** infrastructure (or antiX/MX packaging patterns)
  to run **without systemd**; init = **runit** (primary) with SysVinit as the
  compatibility fallback (§6). Architecture ports: **i386 (SSE2 mainstream) +
  amd64**. A signed **Castalia overlay repository** (§13) layers our shell,
  apps, themes, defaults, and metapackages on top; we do **not** fork the whole
  archive.
- **Why this and not the user's Alpine/Buildroot preference:** Alpine's musl and
  Buildroot's appliance model each directly damage the two features the product
  exists to deliver (Wine fidelity + broad old-hardware support) or remove the
  update/package model a desktop needs. Debian-glibc maximizes both at minimum
  engineering cost, with the best 32-bit longevity available. This is the
  honest, defensible call.
- **Fallback on file:** If Debian's non-systemd path ever becomes untenable,
  **Void Linux (glibc, runit)** is the pre-vetted alternative base; the shell/
  apps are written to be base-agnostic (§12) so a base swap is costly but not
  fatal.
- **What we borrow, not fork:** antiX/MX are studied as the *reference* for
  "Debian that flies on a P4." We learn from their init, live-system, and
  low-RAM tuning rather than reinventing it.

---

## 6. System Architecture

Layered from silicon up. Each layer names a **[DECISION]** default and, where
relevant, the fallback. The guiding rule: *boring, proven plumbing; originality
spent on UX.*

### 6.1 Architecture at a glance

```
┌──────────────────────────────────────────────────────────────────┐
│  Castalia Shell: Explorer (files) · Panel/Taskbar · Menu · Tray   │  Qt5/C++
│  Castalia Control Center + first-party apps (§9)                  │
├──────────────────────────────────────────────────────────────────┤
│  Compatibility: Wine (per-prefix) · DOSBox-X · ScummVM · (86Box)  │
├──────────────────────────────────────────────────────────────────┤
│  Session: Openbox WM · picom (optional) · Castalia session mgr    │
│  Display: Xorg (modesetting/intel/nouveau/radeon) + Mesa          │
│  Audio: ALSA + PipeWire-lite (or bare ALSA @512MB)                │
│  Services: NetworkMgr · CUPS · Samba · udisks · polkit · elogind  │
├──────────────────────────────────────────────────────────────────┤
│  Init/supervision: runit (SysVinit fallback) · eudev              │
│  Package/update: dpkg/apt + Castalia overlay repo · Restore Points│
├──────────────────────────────────────────────────────────────────┤
│  Kernel: Linux LTS (i686-SSE2 / x86-64) · firmware (opt non-free) │
│  Bootloader: GRUB2 (BIOS+opt UEFI) · isolinux (live)              │
└──────────────────────────────────────────────────────────────────┘
```

### 6.2 Bootloader

- **[DECISION]** **GRUB2** for installed systems (BIOS/MBR primary; UEFI/GPT
  optional, no Secure Boot in v1). **isolinux/syslinux** for the live ISO
  (El-Torito, robust on old optical/USB BIOSes).
- Boot menu ships fixed, human-readable entries: **Castalia Classic**,
  **Safe Mode** (no compositor, `vga=` framebuffer, single core, minimal
  services), **Recovery** (initramfs recovery shell, §6.13), and **Memory
  test** (memtest86+). Timeout short; last-booted remembered.
- **[ASSUMPTION]** Some very old BIOSes have >137 GB / LBA quirks and USB-boot
  gaps; the ISO stays hybrid (USB *and* CD bootable) and the installer keeps
  `/boot` within the first 128 GB by default.

### 6.3 Kernel

- **[DECISION]** **Linux LTS** (a long-term-support series, e.g. a 6.1/6.6-class
  LTS — pinned per release, tracked from Debian's kernel where possible for
  security). Two builds: **i686 with SSE2 + PAE** and **x86-64**.
- Config tuned for old hardware: IDE/PATA + AHCI, old NIC/GPU/audio modules,
  `CONFIG_HZ` and preemption tuned for desktop responsiveness on 1–2 cores,
  no exotic modern-only options, aggressive module-ization to keep the image
  small and probe-driven.
- Firmware: free firmware in-image; a clearly-labeled **optional non-free
  firmware** package set (Wi-Fi, some GPUs) where redistribution is legal
  (§13). Never required for the guaranteed (wired) path.
- **[DECISION]** Prefer Debian's maintained kernel to inherit its security
  cadence rather than hand-rolling, *unless* a hardware gap forces a custom
  config; custom configs live in `/packages/kernel` and are CI-built.

### 6.4 Init system & service supervision

- **[DECISION]** **runit** as PID 1 / service supervisor: tiny, fast, trivially
  auditable (a service is a `run` script), instant restart-on-crash, and it
  makes our **fast-boot** and **repairable** principles concrete. **SysVinit**
  is the documented fallback for any board where runit misbehaves.
- **eudev** for device management (systemd-udev fork, no systemd dependency).
- **elogind** for seat/session/lock/idle and polkit integration (again, the
  logind API without systemd).
- Services are described in `/services` as runit `run` scripts + a Castalia
  metadata file (display name, description, category) that the **Services
  Manager** app (§9) reads to present them in plain language.

### 6.5 Filesystem layout

- **[DECISION]** Default filesystem **ext4** (reliable, fast, low-overhead,
  excellent on old spinning/PATA disks and CF cards). Journaling on.
- Recommended tier may opt into **btrfs** at install for cheap snapshot-based
  Restore Points; **[DECISION]** default stays ext4 with **rsync/hardlink
  Restore Points** (Timeshift-rsync model) so recovery works on *any* FS and
  any disk, including the 512 MB/8 GB floor. (See §13.)
- Standard FHS with Castalia additions:

```
/usr, /etc, /var, /home … (standard FHS)
/opt/castalia/            first-party shell + apps (self-contained where useful)
/etc/castalia/            system defaults: theme, panel layout, power profile
  ├─ theme.conf           active theme + accent
  ├─ panel.conf           taskbar/menu layout
  └─ policy.d/            security/power/update policy fragments
/var/lib/castalia/
  ├─ restore-points/      Restore Point metadata + rsync trees / btrfs refs
  ├─ compat-db/           Wine/DOS app profiles + compatibility ratings
  └─ hwprobe/             detected-hardware cache + quirks results
~/.config/castalia/       per-user shell/app settings (INI/TOML, human-editable)
~/Documents ~/Pictures ~/Music ~/Downloads ~/Desktop  (XDG user dirs)
~/Applications/           per-user Wine app launchers (see §11)
```

- **Human-first config:** every Castalia config file is plain INI/TOML with
  comments, editable by hand, and re-readable by the app without corruption
  (P5). No binary registries.

### 6.6 User / session management

- Local accounts only (no online identity, P7). Standard `/etc/passwd`,
  `shadow`, groups; PAM for auth.
- **Login manager:** **[DECISION]** a light greeter — **LightDM** with an
  original Castalia greeter theme (§8) — chosen for low RAM and easy theming.
  Autologin optional (single-user home machine) with a clear security note.
- **Fast user switching equivalent:** multiple X sessions via elogind seats +
  LightDM "switch user" (VT switch). Honestly scoped in §10 (works, but on
  512 MB two graphical sessions is tight — the UI warns).
- Session manager: a small Castalia component that starts Openbox, the panel,
  the tray, restores the user's theme/power profile, and handles logout/lock.

### 6.7 Display stack

- **[DECISION]** **Xorg** (per project preference and old-GPU reality).
  Drivers: `modesetting` default, with `intel`/`nouveau`/`radeon` where they
  test better per GPU (§4.4); Mesa with classic/crocus/legacy drivers; `vesa`/
  `fbdev` fallback. **Wayland deferred indefinitely** (old GPUs, `Xorg`-only
  driver realities, and Wine/X11 assumptions).
- **Compositor:** **picom**, *off by default* on the 32/low-GPU tiers, auto-on
  only when GL + RAM thresholds are met (§4.4, §14). The desktop must be fully
  usable and attractive **with the compositor off** (P4).
- Resolution handling: sane EDID probe, safe 1024×768 fallback, and a **guided
  Display Test** at first boot before committing a mode (§14) so a bad mode
  never leaves the user at a black screen.

### 6.8 Audio stack

- **[DECISION]** **ALSA** as the foundation; **PipeWire** (with a minimal
  configuration, WirePlumber) as the user-level sound server on ≥1 GB tiers for
  per-app volume + easy device switching; **bare ALSA + dmix** on the 512 MB
  floor to save RAM. A single **Sound Mixer** app (§9) abstracts whichever is
  active. No PulseAudio legacy stack unless a spike shows PipeWire is too heavy
  on the floor tier (**[SPIKE]**).

### 6.9 Network stack

- **[DECISION]** **NetworkManager** (with the *non*-GNOME, `nmcli`/tray path) —
  best coverage for the messy reality of old Ethernet + best-effort Wi-Fi,
  with a friendly **Network Center** front-end (§9). `connman` is the fallback
  if NM proves too heavy on the floor (**[SPIKE]**).
- Wired DHCP must work with zero configuration out of the box. Static IP, DNS,
  proxy, and VPN (basic) are exposed in the Network Center.
- **Samba client** (`cifs-utils` + gvfs/kio-less mounting via a Castalia mount
  helper) for Windows shares → surfaced as **Network Places** in Explorer
  (§7, §10).

### 6.10 Printing stack

- **[DECISION]** **CUPS** + **Gutenprint** + **foomatic** + PostScript/PCL;
  USB and network (IPP/JetDirect) printers; driverless IPP-Everywhere where
  available. **Printer Manager** app (§9) wraps CUPS admin in plain language.
- Scanners: **SANE** best-effort (secondary), exposed only if a device is
  detected.

### 6.11 Storage / mounting system

- **[DECISION]** **udisks2** + **polkit** for user-initiated mounting;
  removable media (USB sticks, CD/DVD, SD) auto-appear in Explorer with an
  **Autoplay policy** that is **off/ask by default** (P7, §10 — no silent
  autorun, ever). `ntfs-3g` for reading Windows disks (migration, §9);
  ext4/vfat/exfat/iso9660/udf supported.
- Disk Manager app (§9) wraps partitioning (parted/gdisk), formatting, SMART
  health, and mount options behind a careful, confirmation-heavy UI.

### 6.12 Package management & update system

- Covered in depth in §13. Summary: **dpkg/apt** core + **signed Castalia
  overlay repo**; **Software Center** and **Update Center** are Qt front-ends;
  updates are transactional-ish and always preceded by an automatic **Restore
  Point** on the system tier; channels stable/testing/nightly.

### 6.13 Recovery system

- **[DECISION]** A dedicated **Recovery** GRUB entry boots a small,
  self-contained environment (a Buildroot- or Debian-`live`-built initramfs,
  §17) that does **not** depend on the installed root being healthy. It offers:
  fsck/repair, mount + chroot, **Restore Point rollback**, password reset,
  bootloader reinstall, log export to USB, and a network-off "safe shell."
- **Safe Mode** (a normal boot with compositor off, minimal services, generic
  video) is separate and lighter — for "my last change broke the desktop"
  rather than "my disk is damaged."
- The **Recovery Center** app (§9) exposes the same rollback/repair actions
  from *within* a healthy system, so users learn the tools before they need
  them (P8).

### 6.14 Logging system

- **[DECISION]** Plain-text logs (no binary journal): **syslog** (busybox-
  syslogd or rsyslog-light) to `/var/log`, plus runit's per-service logs via
  `svlogd` with automatic rotation. **Log Viewer** app (§9) tails, filters, and
  categorizes them in plain language (P5). No systemd-journal.

### 6.15 Hardware detection & driver management

- **[DECISION]** Probe-driven: `eudev` + a Castalia **hwprobe** service that
  runs at install and first boot, matches PCI/USB IDs against a shipped
  **quirks/driver map** (`/var/lib/castalia/hwprobe`), selects Xorg driver +
  audio + NIC + suspend policy, and records results for the **Hardware Center**
  (§9). Microcode + firmware are applied where present/legal.
- Driver "management" is intentionally modest: Linux drivers are mostly in-tree
  and auto-loaded; the Hardware Center *shows* what was detected/loaded and lets
  the user toggle known-quirky options (e.g. force `vesa`, disable a flaky
  Wi-Fi module) rather than pretending to be a Windows-style driver installer.

### 6.16 Theme system

- **[DECISION]** A single **Castalia theme engine** driving: GTK (for any GTK
  apps we ship/allow), **Qt/QSS** (our shell + apps), Openbox window
  decorations, the icon theme, cursor theme, LightDM greeter, Plymouth-less
  boot splash, and sounds — all from **one** `theme.conf` + a theme bundle
  format (§8, §9 Theme Manager). One switch changes everything, coherently.

### 6.17 Application compatibility system

- Covered in depth in §11. Summary: native apps first; **Wine per-prefix**
  managed by a **Wine Prefix Manager**; **DOSBox-X** and **ScummVM** launchers;
  a local **compatibility database** with per-app profiles and honest ratings;
  file-association + "Open With" routing that can send a `.exe`/`.doc`/`.psd`
  to the right handler (native, Wine, or emulator).

### 6.18 Sandboxing strategy

- **[DECISION]** Pragmatic, layered, *not* a security theater:
  - Wine apps run as the **user** (never root), in **isolated per-app
    prefixes**, with **no** default access to `Z:`-mapped system paths beyond
    the user's home and an explicit shared folder; risky apps can be launched
    with **bubblewrap** confinement (filesystem + network scoping) via a
    per-app profile toggle.
  - First-party apps follow least-privilege; privileged actions go through
    **polkit** prompts, not setuid sprawl.
  - Optional **Firejail** profiles for the bundled browser and for
    user-flagged "untrusted" Wine apps.
  - We are **honest** (P10, §15): Wine is not a security boundary against
    hostile native Windows malware; the docs say so plainly.

### 6.19 Optional virtualization / emulation layer

- **[DECISION]** Optional, install-on-demand, not in the base image:
  - **DOSBox-X** (DOS + early Windows apps/games) and **ScummVM** (supported
    adventure engines) are *first-tier* compatibility tools with Castalia
    launchers (§9, §11).
  - **86Box / PCem** (full-fidelity legacy PC emulation) and **QEMU/KVM**
    (KVM only where the CPU supports VT-x/AMD-V — many T2 P4s do not) are
    **advanced, opt-in** tools in the Software Center for enthusiasts who want
    to run a real period OS in a VM. Clearly labeled as advanced; not part of
    the core UX promise.

---

## 7. Desktop / Shell Architecture

The shell is the product's face. It is called **Castalia Explorer** (the file
manager + desktop) working alongside the **Castalia Panel** (taskbar/menu/tray).
Built in **Qt 5 / C++17** (§12). Architecturally it is a set of cooperating
processes over an existing WM (**Openbox**), not a monolith — so a crash in one
piece never blacks out the desktop (P8).

### 7.1 Process model

| Process | Role | If it crashes |
|---------|------|---------------|
| `openbox` | Window management, Alt+Tab, decorations, keybinds | runit-supervised; restarts; windows survive |
| `castalia-session` | Starts/monitors the above; theme + power + autostart | Restarts children; logs |
| `castalia-panel` | Taskbar, launch menu, window list, tray host, clock | Supervised; restarts in <1 s, desktop stays up |
| `castalia-desktop` | Desktop icons, wallpaper, right-click menu | Supervised; restart repaints |
| `castalia-explorer` | File manager windows (spawned on demand) | Per-window; one crash ≠ lost desktop |
| `castalia-tray`/SNI | System tray / status-notifier host | Supervised |

This separation is a direct expression of P8 (recoverable) and P2 (a heavy
Explorer window never freezes the panel).

### 7.2 Panel / taskbar

- Bottom panel (default height 30 px @1024, 28 px @800 for space). Left→right:
  **Launch button** → **Quick Launch** → **window list** (grows) → **tray** →
  **clock/calendar**. Fully reconfigurable, but the default is the point (P-defaults).
- Window-list buttons show icon + truncated title; grouping optional (off by
  default at 800×600 to keep labels readable). Hover shows full title; on
  compositor-on tiers, an optional lightweight thumbnail (off on floor).
- Right-click panel → lock/unlock, add/remove applets, panel settings.

### 7.3 Application launcher (the "Castalia Menu")

- A single corner **launch menu** (the XP-era ergonomic, original art/name).
  Layout: left column = **pinned/favorites** + **All Applications** (categorized
  per freedesktop menu spec), right column = **user places** (Documents,
  Pictures, Music, Computer, Network Places), **Control Center**, **Search**,
  and **Power** (shutdown/restart/logoff/lock).
- **Search box** at the top filters apps, settings, and recent documents as you
  type (indexed lightly, §7.10). Keyboard-first: press launch-key, type, Enter.
- **[DECISION]** Original naming throughout: "All Applications", not
  "All Programs"; "Castalia Menu", not "Start". Categories use plain names
  (Internet, Media, Office, Accessories, Games, System, Compatibility).

### 7.4 Quick Launch, tray, clock, notifications

- **Quick Launch:** a small pinned-icon strip (Explorer, browser, Control
  Center by default). Drag to add/remove.
- **System tray:** StatusNotifierItem + XEmbed fallback (old apps). Hosts
  network, volume, battery, update, and app indicators.
  **SNI half shipped** (`shell/panel/src/TrayHost.{h,cpp}`): the panel owns
  `org.kde.StatusNotifierWatcher` and registers itself as a host, so anything
  speaking SNI — libappindicator/libayatana, KDE apps, `QSystemTrayIcon` on a
  D-Bus desktop — gets an indicator. Left click calls `Activate`, right click
  `ContextMenu`; icons come from `IconName` (ours, then the system theme) or
  `IconPixmap`, never a blank square; `Passive` items hide themselves; an item
  whose application leaves the bus is dropped. Every read is **asynchronous**
  and the wiring is queued out of the D-Bus dispatch — see the worklog for why
  that is not optional.
  **XEmbed half shipped too** (`shell/panel/src/XEmbedTray.{h,cpp}`): the panel
  owns the `_NET_SYSTEM_TRAY_S<screen>` selection, announces it with the
  `MANAGER` root message (applications that started before the panel are
  waiting for exactly that), and answers `SYSTEM_TRAY_REQUEST_DOCK` by
  reparenting the client's window into a container of its own,
  `XEMBED_EMBEDDED_NOTIFY`-ing it and mapping it. GTK2-era icons, Wine, and
  `QSystemTrayIcon` on a machine with **no session bus at all** arrive this
  way. The two halves are independent by design and run side by side. The
  containers are created with xcb and never handed to Qt: a legacy icon paints
  itself `ParentRelative`, so its background is the container's *X* background
  and nothing else — each one is given a pixmap of the exact slice of panel it
  covers, so the icons sit on the tray well's gradient instead of on a flat
  patch of it. If another tray manager already owns the selection we stand
  down rather than adopt half the session's icons, and on shutdown every icon
  is handed back to the root window so restarting the panel does not kill it.
- **Clock/calendar:** click → month calendar + date/time; original design.
  12/24 h, locale-aware, NTP optional (off if offline).
- **Notifications:** a light `org.freedesktop.Notifications` server (our own,
  Qt) — corner toasts, a small history, per-app mute. No cloud, no account.
  **MVP shipped** (`apps/notificaciones`, `castalia-notificaciones`): owns the
  bus name, implements Notify / CloseNotification / GetCapabilities /
  GetServerInformation and the NotificationClosed signal, so `notify-send` and
  any third-party app reach the user. Toasts stack above the panel strut,
  slide in ≤200 ms (off under reduce-motion), expire on their own and dismiss
  on click; every one is appended to a capped TSV history
  (`--historial`), and a muted app is recorded **without** a toast. Started by
  `castalia-session`. 1.0: actions/buttons, an inhibit ("do not disturb")
  switch, and a tray indicator once the tray hosts anything. **Senders:**
  `castalia::notify()` (`castalia-notify`, its own tiny library so QtDBus is
  linked only where it is used) — the recycle bin, the screenshot tool and the
  archiver announce what they finish.

### 7.5 Desktop icon manager

- Icons for user files on `~/Desktop`, plus system anchors: **Computer**
  (drives/devices), **Documents**, **Network Places**, **Recycle** (Trash),
  and a **Castalia** intro/Welcome icon. All toggleable in Desktop settings.
- Grid snap, arrange-by, per-icon rename, drag to Explorer. Right-click desktop
  → new folder/file, paste, display settings, personalize (theme), arrange.
- Wallpaper: fit/fill/center/tile, per-monitor; original wallpapers (§8, §21).

### 7.6 Alt+Tab switcher, window list, shutdown dialog

- **Alt+Tab:** icon + title list; optional thumbnails only on compositor-on
  tiers. Alt+` cycles same-app windows. Fully keyboard-navigable.
  **Shipped** (`shell/panel/src/Switcher.{h,cpp}`) — and it is Castalia's own
  switcher, not Openbox's restyled: a centred card in the theme's surface,
  icons from the shared 48 px family, the accent wash and leading bar the
  launch menu uses for hover, and the selection sliding between rows in
  ≤120 ms (skipped under reduce-motion). The order is
  **most-recently-used** — one press goes back to the window you came from —
  which the panel maintains by following `_NET_ACTIVE_WINDOW`. Alt+Shift+Tab
  walks back, Alt+` narrows the list to the windows of the program you are
  already in, `Esc` abandons the switch and `Enter` takes it. It lives in the
  panel process: the §16 budget gives the switch ≤120 ms and spawning a
  process would spend most of that before drawing a pixel.
  It **owns the binding itself** — an X passive grab on Alt+Tab, Alt+Shift+Tab
  and Alt+` — so `openbox-rc.xml` must not bind them (X gives a grab to the
  first client that asks, and Openbox starts first);
  `tools/tests/test_openbox_rc.py` fails the build if one comes back.
  Icons resolve ours-first, by WM_CLASS through the shared roster
  (`AppRoster`, one table with the launch menu), then whatever the program
  publishes in `_NET_WM_ICON` — which is what Wine, DOSBox and anything else
  we did not write arrive with — and last the generic `window` icon.
- **Shutdown/logoff/restart dialog:** a single modal with big, clear,
  original-iconed buttons: **Shut Down · Restart · Log Off · Lock · Switch
  User** (+ Suspend where the hwprobe says it's safe, §4.6). Confirmation on
  destructive actions; remembers nothing sensitive.

### 7.7 Run dialog & keyboard access

- **Run** (default hotkey, e.g. the launch-key + R): type a command, a path, a
  URL, or an app name; history; runs native or routes `.exe` to Wine (§11).
- **Global keyboard map** (all rebindable): launch menu, Run, lock, screenshot,
  show-desktop, switch workspace, volume/brightness keys, Explorer, terminal.
  The OS is **fully operable keyboard-only** (P-accessibility, §7.12).

### 7.8 File search

- **[DECISION]** Two-tier: (1) instant **name search** in the current Explorer
  location (always available, no index); (2) an optional lightweight **content/
  name index** (a small `locate`-style + optional full-text via a tiny indexer)
  that is **off on the 512 MB floor** and on-by-default at ≥1 GB. Menu search
  (§7.3) queries app names + settings + recent docs, not the whole disk, to stay
  instant.

### 7.9 Recent documents & favorites/pinned

- **Recent documents:** freedesktop `recently-used` list, surfaced in the menu
  and Explorer; per-user, clearable, local-only (P7).
  **Shipped for the menu** (`castalia::recent`, `shell/libcastalia-ui/Recent.
  {h,cpp}`): the store is the standard `~/.local/share/recently-used.xbel`, so
  our recent documents *are* the desktop's — a file opened in Notas shows up in
  a GTK app's recent list and the other way round. The Start Menu leads with
  the last eight, they are covered by the §7.3 search (by file name *and* by
  folder), and "Vaciar la lista" really deletes the store. Notas, Escritor and
  the image viewer record; Explorer's own "Recientes" place is still to come.
- **Favorites/pinned apps:** pin from menu or Quick Launch; stored in
  `~/.config/castalia/panel.conf`. Pinned **places** (folders/shares) too.

### 7.10 User profile folders

- XDG user dirs (`~/Documents`, `~/Pictures`, `~/Music`, `~/Videos`,
  `~/Downloads`, `~/Desktop`) created and localized at account creation; shown
  with friendly icons in Explorer's sidebar and the menu's right column
  (the "My Documents" ergonomic, original names — §10).

### 7.11 Desktop widgets, multi-monitor, low-res, keyboard, accessibility

| Concern | Decision |
|---------|----------|
| **Widgets** | **[DECISION]** *Minimal and off by default.* Only ultra-light optional applets (clock, CPU/RAM meter, sticky note) — panel applets, not a heavy gadget engine. Nothing that pulls the network or spins a timer on the floor tier (P2/P7). |
| **Multi-monitor** | RandR-based: extend/mirror/primary, per-monitor wallpaper, panel on primary (optional on each). Detected in Display Settings (§9). Common on Core 2 desktops; tested on T1. |
| **800×600 behavior** | First-class (P1): panel 28 px, single-column menu that fits 600 px height, dialogs designed to fit 800×600 with no clipped buttons, Explorer defaults to list view. A CI "small-screen" screenshot test (§19) guards this. |
| **Keyboard-only** | Every action reachable without a mouse; visible focus rings; mnemonics on menus/dialogs; Tab order defined. |
| **Accessibility basics** | High-Contrast theme (§8), adjustable font scale (with 800×600-safe presets), large-cursor option, sticky/slow keys via Xkb, screen-reader hook (Orca) available (opt-in, not on floor by default), audible + visual bells. Honest scope: full AT parity is a later phase (§18). |

### 7.12 Shell performance stance

The panel + desktop + session together must fit the idle-RAM budget (§16) with
the compositor off, repaint the menu in <150 ms, and never block the UI thread
on disk or network I/O (all file/enumeration/network work is async or in worker
threads). This is enforced by the performance tests in §19, not left to chance.

### 7.13 Interface language ✅

Castalia is **written in Spanish**. The string literals in the source are what
a Spanish user reads, and no catalogue is consulted for them; every other
language is a Qt translation catalogue layered on top. **Shipped: `es`
(source) + `en`.**

- **The default is Spanish whatever `LANG` says.** A machine set to
  `en_US.UTF-8` still boots Castalia in Spanish unless somebody chose
  otherwise — the product is Spanish-first (§8), and an interface that changes
  language because of an environment variable cannot be screenshotted twice or
  gated deterministically. *"Follow the system"* is offered, but as a choice
  you make, not a default.
- **One file holds the choice**, the way the theme does (§6.6):
  `~/.config/castalia/locale.conf` → `/etc/castalia/locale.conf`, in the same
  flat shape (`[locale]` / `language = "en"`). The Control Center → **Idioma**
  page writes it; `CASTALIA_LANG` overrides it for one run (screenshots, gates,
  bug reports).
- **The loader runs before main().** `castalia::locale` installs the
  translator from a `Q_COREAPP_STARTUP_FUNCTION`, i.e. immediately after the
  `QApplication` constructor and before a single line of anybody's `main()`.
  This is not an optimisation: Qt cannot retranslate a widget that already
  exists, so a binary that resolves its labels into a table before installing
  the translator ends up **half translated with no error anywhere** — which is
  exactly what the shutdown dialog did until the hook replaced the per-`main()`
  call. Linking `libcastalia-ui` is now genuinely enough.
- **Search stays bilingual.** The launch menu indexes the translated label
  *and* the Spanish source string, so an English interface still answers to
  "buscaminas" and the Spanish keyword lists (§7.3) keep working.
- **Toolchain:** `tools/i18n_build.py {extract,release,--check}` wraps
  lupdate/lrelease; catalogues live in `i18n/castalia_<code>.ts` and compile to
  `castalia_<code>.qm`, installed to `/usr/share/castalia/i18n` by both the
  `.deb` and the ISO hook.
- **Gates** (`tools/tests/test_i18n.py`): a declared language with no
  catalogue, an untranslated string, a lost `%1` or `&`, a catalogue nobody
  installs, a profile that does not stage `i18n/` into the chroot, and a GUI
  entry point that never installs a translator are all build failures. What it
  deliberately does *not* check is whether the translations are any **good** —
  that needs someone who speaks the language.

---

## 8. Visual Design System

The look must read as **premium, cohesive, and intentional** — the opposite of
"random Linux theme cobbled from four icon sets." Everything derives from one
documented system so that boot, login, desktop, apps, and dialogs feel like *one
product*. **None of it copies XP's Luna or any Microsoft art** (§3).

### 8.1 Design language: "Castalia Classic"

- **Concept:** *warm Mediterranean stone + sea light.* Think sun-lit sandstone,
  azure sea, olive, and silver — a coastal-castle palette (Castellón/Castalia
  lore, §21) rather than a corporate blue. It evokes the *era's* friendliness
  (soft depth, gentle gradients, rounded-but-not-bubbly controls) with an
  identity that is unmistakably ours.
- **Depth, used sparingly:** a *hint* of gradient and a 1 px light/shadow bevel
  give tactility (era-appropriate) while staying cheap to render with the
  compositor off (P4). No heavy glass, no blur, no drop-shadow soup.
- **Grid & spacing:** 4 px base unit; controls on an 8 px rhythm; title bars
  24–28 px; touch-not-required but comfortable hit targets.

### 8.2 Color system

One accent-driven system; themes are palettes over the same geometry.

| Theme | Story | Title/accent | Surface | Use |
|-------|-------|--------------|---------|-----|
| **Castalia Classic** (default) | Sandstone + sea | Warm azure accent over stone-neutral chrome | Light warm grey | Default OOBE |
| **Castalia Azul** | Deeper sea blue | Saturated marine blue | Cool light grey | "I want blue like the old days" without copying Luna blue |
| **Castalia Oliva** | Olive/terracotta | Muted olive-green accent | Warm sand | Calm, earthy |
| **Castalia Plata** | Silver/graphite | Neutral silver, low-chroma | Light silver | Understated, "pro" |
| **Castalia Medianoche** ✅ | Midnight dark mode | Sky-blue accent on graphite | Dark graphite (not pure black) | A comfortable full-desktop dark mode; the shared UI library derives a matching QPalette so unstyled views/inputs follow the theme (§6.16) |
| **Castalia High Contrast** | Accessibility | Pure black/white + single strong accent, thick focus | Black or white | AT/low-vision (§7.11) |

- **Gradients** are *original* two-stop vertical gradients defined in the theme
  file (e.g. Classic title bar = `#3E82B6 → #2C6699` azure, **not** XP's colors)
  with ≤12% luminance delta so they render cleanly at 16-bit/24-bit color depth
  (some old GMA modes are 16-bit — gradients are dithered to avoid banding).
- All colors are specified for both 24-bit and 16-bit targets; the theme engine
  picks the nearest safe pair.

### 8.3 Window controls & chrome (original)

- **[DECISION]** Original min/max/close glyphs on an original button geometry
  (documented in `/branding/spec/controls.svg`): squared-with-2px-radius
  buttons, close = accent-tinted, 16×16/20×20 glyph grid. Title bar: icon left,
  title centered-left, controls right. **Not** XP's orb/pill shapes or colors.
- Scrollbars, sliders, checkboxes, radios, tabs, progress bars, tree/list views:
  all in one QSS + GTK-CSS spec so Qt and GTK apps match pixel-for-pixel.

### 8.4 Iconography

- **[DECISION]** One original icon family, drawn on a **48/32/24/16 px** grid
  with a documented light source (top-left), corner radius, outline weight, and
  the Castalia palette. Delivered as **SVG + pre-rasterized PNG** (old machines
  render pre-baked PNGs faster than live SVG — the theme ships both; Explorer
  uses PNG at list sizes).
- Metaphors reuse universal ideas (folder, gear, disk, network globe), art is
  ours. A documented **icon contribution spec** (§20 theming guide) keeps
  community icons on-style. No XP-clone icon sets (§3.4).

### 8.5 Typography

- **[DECISION]** UI font: an original or libre humanist sans chosen for
  crispness at 11 px on 800×600 (candidates: a hinted libre face such as
  **Inter/DejaVu-class** or an original "Castalia Sans"). Ship **only** libre/
  original fonts (§3.9). Monospace for terminal/log (DejaVu Mono-class).
- Sizes defined per DPI tier; 800×600 uses an 11 px base with full hinting;
  never below 9 px for body. Antialiasing on; sub-pixel optional (off on
  16-bit modes).

### 8.6 Boot, login, wallpapers, sounds (all original)

| Asset | Concept (original) | Constraint |
|-------|--------------------|------------|
| **Boot screen** | A quiet, framebuffer-friendly Castalia wordmark + a slim progress bar over a stone/azure field; text-mode fallback for no-KMS GPUs | No Plymouth-heavy graphics on floor; must render at 640×480 fb |
| **Login (LightDM greeter)** | Coastal wallpaper, user list with original avatars, clock, accessibility button | ≤ few MB RAM; keyboard-first |
| **Wallpapers** | A small original set: "Sandstone Coast", "Azure Bay", "Olive Terraces", "Silver Harbor", "Castalia Keep" (a stylized castle) — plus solid/gradient options for the floor tier | Ship at 1024×768 and 1280×1024; ≤ target KB each; tasteful, not gimmicky |
| **Sounds** | Original short motif set: *startup*, *shutdown*, *notify*, *error*, *device-in/out*, *empty-trash* — warm, brief, Mediterranean-tinged (§21) | ≤ ~1–2 s each; ogg; **off** switch honored; never sampled from MS |

### 8.7 Animation rules & performance

- **[DECISION]** Animations are **subtle, short (≤150 ms), and disabled when the
  compositor is off or the tier is floor** (P2/P4). Allowed: menu fade/slide
  (compositor-on only), gentle window map. Forbidden on any tier: anything that
  drops the panel below 30 fps or delays first paint of a menu past 150 ms.
- A global **"Reduce animations"** toggle (on by default on 32/floor) in Display
  settings; High-Contrast forces it on.

### 8.8 Pixel-perfect & "not cheap cosplay" checklist

- Dialogs are designed and screenshot-tested at **800×600** and **1024×768**
  (§19) — no clipped buttons, no scrollbars-inside-scrollbars, no 4-px text.
- **One** icon family, **one** control spec, **one** font stack, **one** palette
  system — coherence is the anti-"cosplay" weapon.
- Every default (spacing, corner radius, gradient stops, sound levels) is
  specified in `/branding/spec` and `/themes`, reviewed like product art, not
  left to a downloaded theme. This discipline is what separates a *product* from
  a *theme pack* (P3).

### 8.9 How we make it feel premium

- **Cohesion over flash:** the same 6 colors, 1 grid, 1 icon light-source
  everywhere. Users read cohesion as quality.
- **Restraint:** fewer, better assets; crisp edges; correct optical alignment;
  real typographic hierarchy.
- **Finish:** consistent empty-states, consistent iconography in dialogs,
  correct focus rings, no placeholder art shipping, no mixed-metaphor icons.
- **Performance is aesthetic:** on old hardware, *instant* feels more premium
  than *fancy*. We buy perceived quality with speed (P2).

---

## 9. Built-in Applications

All first-party apps share one Qt5/C++ UI toolkit library (`libcastalia-ui`,
§12) so they look identical and start fast. **Where a mature upstream app is
lighter and good enough, we ship it *reskinned* first and replace it with a
first-party app later** (progressive originality, P3) — the table's "Strategy"
column marks **[Own]** (build ours), **[Wrap]** (Castalia UI over an upstream
engine/CLI), or **[Curate]** (ship a proven upstream app, Castalia-themed, on
our roadmap to replace).

**Legend for scope:** *MVP* = first alpha; *1.0* = public 1.0; *Later* =
post-1.0. **Perf budget** = cold-start / idle-RSS target on the T2 floor
(P4/512 MB), compositor off; enforced in §19.

### 9.1 Core shell & settings apps

| App (original name) | Purpose | Strategy · Lang | Key deps | MVP → 1.0 → Later | UI notes | Perf budget |
|---|---|---|---|---|---|---|
| **Castalia Explorer** (Files) | File manager + desktop | [Own] C++/Qt5 | Qt5, udisks2, gvfs-less mount helper, libmagic | MVP: browse/copy/move/rename/delete-to-Trash, sidebar places, list+icon views, USB mount, basic search. 1.0: tabs, cut/paste queue w/ progress, network places (Samba), archive integration, Open With, properties/permissions, thumbnails (opt). Later: dual-pane, batch rename, spatial mode, cloud-less bookmarks | Two-pane sidebar; list view default @800×600; async I/O; per-window process | ≤600 ms / ≤35 MB |
| **Castalia Control Center** | Single hub for all settings | [Own] C++/Qt5 | Qt5, polkit | MVP: category grid launching individual settings panels. 1.0: unified search across settings, breadcrumb, "recently changed". Later: policy/profiles export | Category grid like a friendly settings home; each tile = a panel below | ≤400 ms / ≤25 MB |
| **Hardware Center** ✅ | Show detected hardware, drivers, quirks | [Own] C++/Qt5 | hwprobe svc, lspci/lsusb libs, elogind | MVP: tree of devices + driver in use + status. 1.0: toggle known quirks (force vesa, disable module), microcode/firmware status, suspend test button. Later: driver-option profiles | Read-mostly, cautious toggles w/ warnings | ≤400 ms / ≤25 MB |
| **Network Center** ✅ | Wired/Wi-Fi/VPN/shares config | [Wrap] C++/Qt5 over NetworkManager | NM, nmcli/libnm, cifs-utils | MVP: wired DHCP/static, Wi-Fi connect, status tray. 1.0: VPN (basic), proxy, DNS, mount Samba shares as Network Places. Later: connection profiles, per-network firewall | Wizard for shares; plain-language states | ≤400 ms / ≤22 MB |
| **Sound Mixer** | Volume, devices, per-app | [Wrap] C++/Qt5 over PipeWire/ALSA | PipeWire/ALSA, libpulse-compat | MVP: master + device select + mute. 1.0: per-app volume, input levels, test tones. Later: simple EQ/profiles | Tray popup + full window; works on both PW and bare ALSA | ≤300 ms / ≤18 MB |
| **Display Settings** | Resolution, multi-monitor, scaling, animations | [Own] C++/Qt5 over RandR | libxcb-randr | MVP: pick mode w/ 15-s revert timer, single monitor. 1.0: multi-monitor extend/mirror/primary, wallpaper, reduce-animations, compositor toggle. Later: color/gamma, per-monitor DPI | Safe "keep changes?" countdown always | ≤350 ms / ≤20 MB |
| **Theme Manager** | Switch/tweak themes, wallpaper, sounds, cursors | [Own] C++/Qt5 | theme engine, GTK+Qt bridges | MVP: pick a bundled theme + accent + wallpaper. 1.0: import/export theme bundle, sound scheme, cursor/icon size, font scale. Later: light theme editor | Live preview tile; one switch changes everything (§6.16) | ≤350 ms / ≤22 MB |
| **User Accounts** | Local users, avatars, autologin, groups | [Own] C++/Qt5 | shadow, PAM, polkit | MVP: add/remove user, set password, avatar. 1.0: admin/standard toggle (sudo group), autologin w/ warning, switch-user. Later: guest session, password policy | Confirmation-heavy; no online accounts | ≤300 ms / ≤18 MB |

### 9.2 Software, updates, recovery, system tools

| App | Purpose | Strategy · Lang | Key deps | MVP → 1.0 → Later | UI notes | Perf budget |
|---|---|---|---|---|---|---|
| **Software Center** (Centro de software) ✅ | Browse/remove installed apps | [Wrap] C++/Qt5 over dpkg/apt | dpkg-query, apt, PolicyKit | **MVP shipped**: browse everything installed with real dpkg data (name, version, disk size, summary), a running total, type-to-filter search, and a guarded remove (pkexec → apt-get) (`apps/software`, `castalia-software`; shows honest sizes — e.g. 843 pkgs · 4.8 GiB). 1.0: install new packages (apt search), categories/screenshots, a "Compatibility" section for Wine apps, offline bundle install. Later: ratings mirror | Honest size hints; numeric-sorted | ≤700 ms / ≤40 MB |
| **Update Center** (Centro de actualizaciones) ✅ | System + app updates | [Wrap] C++/Qt5 over apt | apt, Restore Points svc | **MVP shipped**: lists the packages with a newer version available (parsed from `apt list --upgradable`: name, installed→candidate version), an honest "up to date" empty state, re-check, and a guarded "install updates" that **auto-takes a Restore Point first** (`castalia-restore create --reason pre-update && apt-get upgrade`, chained under pkexec so a bad update is always rollback-able from the Recovery Center); `--demo` shows a sample list for offscreen render (`apps/updates`, `castalia-actualizaciones`; menu → Sistema). 1.0: channels (stable/testing/nightly), changelog, "safe to reboot" state, rollback link. Later: scheduled/offline updates | Never auto-installs silently; clear risk copy | ≤500 ms / ≤30 MB |
| **Recovery Center** (Centro de recuperación) ✅ | Restore Points, repair, backup export | [Own] C++/Qt5 over the Restore Points backend | rsync, Restore Points svc | **MVP shipped**: a Qt front-end over the shared, unit-tested `castalia_recovery` engine — list points (date/reason/description), create a new one, and restore to one (typed-confirm dialog; auto pre-restore point), all through a graphical privilege prompt (`recovery/gui`, `castalia-recuperacion`; menu → Sistema); `--demo` shows sample points for offscreen render. 1.0: schedule points, exclude paths, export logs to USB, "repair boot" launcher. Later: user-data backup wizard | Mirrors the recovery-env actions from within a healthy system (P8) | ≤500 ms / ≤28 MB |
| **Task Manager** | Processes, CPU/RAM/disk, kill | [Own] C++/Qt5 | /proc, procps | MVP: process list, sort, end task, CPU/RAM meters. 1.0: per-app grouping, startup impact, network per-proc, autostart tab. Later: history graphs | Big "End Task" affordance; safe-kill confirm | ≤350 ms / ≤20 MB |
| **Services Manager** (Servicios del sistema) ✅ | Start/stop/enable runit services (plain language) | [Own] C++/Qt5 | runit, service metadata (§6.4) | **MVP shipped**: reads `/etc/sv/*` plus each service's `service.conf` (§6.4 metadata: display name, description, category, essential), shows whether it is enabled at boot (a symlink in the runsvdir) and whether it is actually running (`sv status`), and starts/stops/restarts through pkexec with an extra confirmation on essential services; a service with no `service.conf` still appears, labelled by its directory name. `--demo` shows a sample set (`apps/servicios`, `castalia-servicios`; menu → Sistema). 1.0: enable-at-boot, view a service's log, category grouping. Later: dependency view | Plain-language names/descriptions, not raw unit files | ≤300 ms / ≤18 MB |
| **Log Viewer** (Visor de registros) ✅ | Read/filter system + service logs | [Own] C++/Qt5 | /var/log, svlogd | **MVP shipped**: lists the readable system logs and every svlogd `current` under `/var/log/<service>/`, tails the end of the file (bounded read — a huge log costs one buffer, not its size), classifies each line's severity with a pure function and colours it from the theme, and filters by severity and by text with honest counters. `--demo` shows a sample log (`apps/registros`, `castalia-registros`; menu → Sistema). 1.0: time range, export, "explain this error" links to Help. Later: saved filters | Plain-text, colored severity; no binary journal | ≤300 ms / ≤20 MB |
| **Disk Manager** ✅ | Partitions, format, mount, SMART | [Wrap] C++/Qt5 over parted/udisks | parted, udisks2, smartmontools, ntfs-3g | MVP: view disks/partitions, mount/unmount, format removable. 1.0: create/resize partitions (w/ heavy warnings), SMART health, fstab helper. Later: simple RAID/LVM view | Destructive ops gated by typed confirm | ≤450 ms / ≤26 MB |
| **Printer Manager** | Add/manage printers & jobs | [Wrap] C++/Qt5 over CUPS | CUPS, Gutenprint, foomatic | MVP: auto-detect + add USB/IPP printer, test page, job queue. 1.0: network printers, driver picker, defaults, share. Later: scanner (SANE) tab | Wizard-driven add; plain driver names | ≤450 ms / ≤26 MB |

### 9.3 Productivity & media apps

| App | Purpose | Strategy · Lang | Key deps | MVP → 1.0 → Later | UI notes | Perf budget |
|---|---|---|---|---|---|---|
| **Archive Manager** (Archivador) ✅ | Zip/tar/7z-extract | [Wrap] C++/Qt5 over bsdtar (libarchive) | libarchive-tools | **MVP shipped**: open zip/tar/gz/bz2/xz/7z and browse as a folder tree, extract all or a selection, create zip/tar.gz — one tool (bsdtar) covers every format (`apps/archiver`, `castalia-archivador`; `--list` prints entries headless). 1.0: add/update, password zip, Explorer "Extract here" integration. Later: split archives | Folder-tree view; menu → Archivos comprimidos | ≤400 ms / ≤24 MB |
| **Text Editor** (Notas) | Plain-text editing | [Own] C++/Qt5 | Qt5 | MVP: open/save/find/replace, encoding, word-wrap. 1.0: line numbers, simple syntax highlight, tabs. Later: macros | Fast, Notepad-ergonomic, original | ≤250 ms / ≤16 MB |
| **Rich Text Editor** (Escritor) ✅ | Formatted docs (HTML/ODT) | [Own] C++/Qt5 (QTextDocument) | Qt5, libreoffice-less | **MVP shipped**: bold/italic/underline, font family & size, text colour, alignment (izq/centro/der/justificado) and bullet/numbered lists; open HTML/txt/Markdown, save HTML, plain text or **ODT** (QTextDocumentWriter), with unsaved-changes guarding and a live-formatted toolbar (`apps/richtext`, `castalia-escritor`; menu → Escritor). 1.0: images, tables, print/PDF. Later: templates | WordPad-class scope, honest limits | ≤450 ms / ≤28 MB |
| **Paint app** (Pintura) ✅ | Bitmap drawing | [Own] C++/Qt5 | Qt5 | **MVP shipped**: pencil/line/rect/ellipse/flood-fill/eraser, 28-color palette + custom picker, adjustable brush width, multi-step undo, open/save PNG/BMP/JPG (`apps/paint`, binary `castalia-pintura`). 1.0: layers-lite, selection, resize, screenshots edit. Later: simple filters | Paint-class ergonomics, original UI | ≤450 ms / ≤30 MB |
| **Calculator** | Standard + scientific | [Own] C++/Qt5 | Qt5 | MVP: standard + keyboard. 1.0: scientific, unit convert, history. Later: programmer mode | Fits 800×600; keyboard-first | ≤200 ms / ≤14 MB |
| **Character Map** (Mapa de caracteres) ✅ | Browse & copy glyphs | [Own] C++/Qt5 | Qt5 (QFontDatabase) | **MVP shipped**: a natively-drawn glyph grid over any installed font, a curated set of Unicode blocks (Latin, Greek, Cyrillic, punctuation, currency, arrows, math, box-drawing, symbols, dingbats…), a large preview with the `U+XXXX`/decimal code point, a "characters to copy" line and copy-to-clipboard (`apps/charmap`, `castalia-caracteres`; menu → Mapa de caracteres). 1.0: search by name, recent glyphs, advanced view | Accessory-class; self-contained | ≤250 ms / ≤18 MB |
| **Sticky Notes** (Notas adhesivas) ✅ | Quick reminders on the desktop | [Own] C++/Qt5 | Qt5 (QJson) | **MVP shipped**: a corkboard of natively-painted pastel note cards (five colours, folded corner, drop shadow) you can type in, recolour and delete; notes persist to `~/.config/castalia/adhesivas.json` with debounced writes and reload on launch; `--demo` shows samples without touching saved data (`apps/stickies`, `castalia-adhesivas`; menu → Notas adhesivas). 1.0: detach a note to its own always-on-top window, reminders/alarms | Accessory-class; self-contained | ≤250 ms / ≤18 MB |
| **Clock** (Reloj) ✅ | Analog/digital clock, stopwatch, alarm | [Own] C++/Qt5 | Qt5 | **MVP shipped**: a natively-painted analog face (accent rim, numbered dial, hour/minute/second hands), a digital readout with the localized Spanish date, a stopwatch (start/pause/reset with tenths) and a simple alarm (`apps/clock`, `castalia-reloj`; menu → Reloj); `--time HH:MM:SS` freezes the hands for reproducible screenshots. 1.0: multiple alarms, world clocks, countdown timer | Accessory-class; self-contained | ≤250 ms / ≤16 MB |
| **Magnifier** (Lupa) ✅ | Screen magnifier (accessibility) | [Own] C++/Qt5 | Qt5 (QScreen) | **MVP shipped**: grabs the area under the pointer and shows it enlarged with a crosshair; 2×/4×/8× zoom and a follow-cursor toggle; `--demo` magnifies a bundled sample so it renders with no display (`apps/magnifier`, `castalia-lupa`; menu → Lupa). 1.0: lens (floating) mode, colour-inversion & high-contrast filters, smoothing toggle | Accessibility; self-contained | ≤250 ms / ≤18 MB |
| **Screenshot Tool** (Captura) ✅ | Capture screen/region | [Own] C++/Qt5 | Qt5 (QScreen) | **MVP shipped**: full-screen or dragged-region capture with a live preview, adjustable delay, auto-named save into ~/Imágenes/Capturas, and copy-to-clipboard; a headless `--capture full --out` for scripts (`apps/screenshot`, `castalia-captura`). Proven end to end: captured the real composited desktop (wallpaper + a live window). 1.0: window capture, annotate (arrow/box/text), global hotkey. Later: scroll capture | Self-contained, no external tool | ≤250 ms / ≤16 MB |
| **Media Player** | Audio + video playback | [Wrap] C++/Qt5 over libmpv | libmpv/ffmpeg | MVP: play common audio/video, playlist, seek/volume. 1.0: subtitles, codecs pack prompt, DVD/CD audio, keyboard control. Later: light library view | mpv engine = light + wide codec support; original chrome | ≤600 ms / ≤45 MB |
| **Image Viewer** | View/rotate/slideshow images | [Own] C++/Qt5 | Qt5, libjpeg/png/webp | MVP: view/zoom/rotate, next/prev, common formats. 1.0: slideshow, EXIF, set-as-wallpaper, basic crop. Later: batch convert | Fast decode; thumbnails via Explorer cache | ≤300 ms / ≤22 MB |
| **Terminal** ✅ | Shell access | [Own] C++/Qt5 + POSIX (forkpty) | Qt5, libutil, bash | **MVP shipped**: an ORIGINAL PTY-backed VT100/ANSI emulator written from scratch (no qtermwidget) — UTF-8, 16/256/24-bit colour, bold/underline/inverse, cursor movement, erase, scroll regions, insert/delete, alternate screen (vim/less), scrollback, OSC titles, copy/paste (`apps/terminal`, `castalia-terminal`; `--run` proves it by driving a real shell). 1.0: tabs, profiles, colour schemes, mouse selection. Later: split, quake-drop | Runs a real login shell; `ls --color`, editors and pagers work | ≤300 ms / ≤18 MB |
| **Screensaver** (Salvapantallas) ✅ | Idle animation | [Own] C++/Qt5 | Qt5 | **MVP shipped**: four original QPainter scenes in the Castalia palette — "ondas" (azure waves + bokeh + mark), "mystify" (echoing polyline ribbons), "estrellas" (warp starfield), "aurora" (accent-tinted aurora curtains, shared with the desktop's flourish via `castalia::paintAurora`); full-screen, exits on input, `--preview`/`--screenshot`, deterministic motion (`apps/screensaver`, `castalia-salvapantallas`). Control Center picker shipped. 1.0: idle activation. Later: more scenes | Original art, no assets/GL; on-brand | ≤250 ms / ≤20 MB |
| **System Monitor** (Monitor / Administrador de tareas) ✅ | Live processes + performance | [Own] C++/Qt5 | Qt5, /proc | **MVP shipped**: a live, sortable process table (PID, name, per-process CPU%, memory) with "Finalizar proceso" (SIGTERM), plus a Rendimiento tab — a scrolling total-CPU graph, per-core bars and a memory meter — all read straight from /proc, no deps (`apps/monitor`, `castalia-monitor`). 1.0: per-user filter, tree view, priority/nice, disk & net I/O. Later: history | Task-Manager ergonomics; numeric-sorted columns | ≤250 ms / ≤24 MB |
| **System Diagnostics** (Diagnóstico) ✅ | Detailed system info + benchmarks | [Own] C++/Qt5 | Qt5, /proc·/sys, pciutils | **MVP shipped**: a "Sistema" tab (OS/kernel, CPU model·threads·freq·cache, RAM, GPU, screen, per-disk, network) and a "Rendimiento" tab that really measures CPU (multi-thread xorshift Mop/s), memory bandwidth (GB/s), disk (fsync write + fadvise read MB/s), 2D graphics (QPainter Mpx/s) and the network (loopback TCP Gbit/s + gateway ping) with animated gauges and a Castalia score (`apps/diagnostics`, `castalia-diagnostico`; `--report` prints a headless text report). 1.0: history/compare, export, per-core view. Later: stress test | Spectacular gauges; every number measured live on the machine | ≤300 ms / ≤22 MB |

### 9.4 Onboarding, help & compatibility apps

| App | Purpose | Strategy · Lang | Key deps | MVP → 1.0 → Later | UI notes | Perf budget |
|---|---|---|---|---|---|---|
| **Welcome & Help Center** (Bienvenida) ✅ | First-boot orientation + offline help | [Own] C++/Qt5 | Qt5 (QTextBrowser) | **MVP shipped**: a topic sidebar with rich, themed, 100%-offline pages (Bienvenido, El escritorio, Aplicaciones, Windows®, Rendimiento, Instalar, Ayuda), each with quick-action buttons that launch the relevant app; opened on first boot by the live session (`apps/welcome`, `castalia-bienvenida`; menu → Centro de ayuda). Combines the planned Help Center + Welcome Tour. 1.0: search, contextual `?` links from each app, "show at start" persistence. Later: tips-of-the-day | Warm, brief, skippable; linked from the menu and every app | ≤350 ms / ≤22 MB |
| **Migration Assistant** ✅ | Import files/settings from an old Windows disk | [Own] C++/Qt5 | ntfs-3g, rsync | MVP: mount an NTFS disk, copy Documents/Pictures/Music/Desktop/Favorites to the new home. 1.0: browser bookmarks import, map "My Documents"→Documents, dedupe. Later: some app-settings hints | Read-only source; never writes the old disk; clear progress | ≤450 ms / ≤28 MB |
| **Wine Prefix Manager** | Create/manage per-app Wine prefixes | [Own] C++/Qt5 over Wine/winetricks | Wine, winetricks, bubblewrap | MVP: create prefix, install an .exe, launch, per-prefix Wine version. 1.0: winetricks presets, compat-DB profiles, sandbox toggle, uninstall. Later: prefix snapshot/export | Guided; shows honest compat rating (§11) | ≤500 ms / ≤30 MB (mgr; apps extra) |
| **DOSBox-X Launcher** | Run DOS/early-Win apps & games | [Wrap] C++/Qt5 over DOSBox-X | dosbox-x | MVP: add a DOS program/folder, auto-config, launch. 1.0: per-game profiles, mount wizard, cycles/scaler presets. Later: front-end art/box scans (user-provided) | Hides dosbox.conf behind a friendly form | ≤400 ms / ≤24 MB (launcher) |
| **Classic Games folder** | Bundled, legally-clean games | [Curate] mixed + [Own] C++/Qt5 | e.g. original/GPL games, ScummVM demos w/ permissive licenses | **Two titles shipped**, both clean-room, original-art, written from scratch in Qt with no third-party assets (§3.9) and a head-less model self-test as a CI gate: **Buscaminas** (bevelled field, LED counters, native mines/flags/face, three levels, first-click-safe, chording — `apps/buscaminas`) and **Solitario** (Klondike; public-domain rules; deck, felt and cards drawn natively; draw-1/draw-3, click-to-move, double-click home, auto-finish, win detection — `apps/solitario`). Both on the Start Menu. Next: mahjong; ScummVM front-end for freeware titles; community pack | **Only** legally-clean/FOSS titles; provenance-logged (§3.9) | per-game; each ≤50 MB RSS target |

### 9.5 App-catalog notes

- **The three system tools shipped 2026-08-18** — the last §9 apps that were
  still ticks on a plan:
  - **Centro de hardware** (`castalia-hardware`): the device tree grouped by
    what a person recognises (Gráficos, Red, Almacenamiento…), each row saying
    which kernel module is driving it. It reads **sysfs and procfs** — always
    present — and uses `lspci` only when it happens to be installed, and only
    to put human names on PCI ids. A FLOOR-tier machine with no pciutils reads
    "Dispositivo 8086:0d57" instead of a marketing name; nothing is ever
    hidden for want of a name. USB devices carry their own strings, so they
    are named without any id database at all. "Copiar informe" puts a
    plain-text summary on the clipboard, which is what you actually need when
    someone is helping you with a machine they cannot see (§20).
  - **Administrador de discos** (`castalia-discos`): disks and partitions from
    `lsblk --json`, mount/unmount through `udisksctl` (falling back to
    mount/umount), and format for removable media. The design is the refusals:
    format is offered **only** for a removable device that is not mounted, not
    write-protected, and not the medium this session booted from — and even
    then the button is a dead end until the user **types the device path
    exactly**, the same gate the installer uses (§14.5 #1). "¿Estás seguro?"
    is not a gate; typing `/dev/sdb1` is.
  - **Asistente de migración** (`castalia-migrar`): finds the NTFS/FAT
    partitions, mounts the chosen one **read-only** (and stops if that fails,
    rather than retrying without `ro`), lists the Windows accounts on it —
    minus the ones Windows creates for itself — and copies Documentos,
    Imágenes, Música, Vídeos, Escritorio and Favoritos into the new home with
    rsync. `--delete` is never passed and the destination is never the old
    disk. The folder table covers XP's `My Documents` *and* a Spanish
    Windows's `Mis documentos`, because a missing name there fails **silently**:
    it copies nothing and reports success. That table is gated by
    `--selftest`.
  - All three share `castalia::blocks` in `libcastalia-ui` for the block-device
    view, so the Disk Manager and the Migration Assistant cannot disagree about
    what a partition is.
- **Centro de redes** (`castalia-redes`) reached its §9 MVP the same week. It
  had been an honest read-only status view over iproute2; it is now three
  tabs — **Estado** (that same view, which needs no NetworkManager at all),
  **Wi-Fi** (the networks in range with plain-language signal and security,
  and connect) and **Configuración** (automática/DHCP or a manual
  address/gateway/DNS per connection) — wrapping `nmcli` per §6.9. Without
  NetworkManager the last two tabs disable themselves and say why, rather
  than offering buttons that cannot work. Two things are pure and gated:
  nmcli's terse rows are colon-separated **with backslash escapes**, and
  SSIDs contain colons, so a naive split renames the user's network; and a
  wrong `connection modify` argument list does not error — it applies, and
  the machine loses its network. The MVP's "status tray" is a **panel**
  indicator (`CastaliaPanel::updateNetwork`) reading `/sys/class/net` every
  ten seconds: dimmed when nothing is up, naming the interface on hover, and
  opening this app on click. A resident applet to poll three files is not a
  trade the FLOOR tier should make.
- **Consistency contract:** every app links to **Help Center** (F1), obeys the
  active theme, honors "reduce animations", and passes the 800×600 layout test
  (§19). New first-party apps must inherit `libcastalia-ui` or they don't ship.
- **What we deliberately do *not* build in v1:** an office suite (point users to
  a curated LibreOffice in the Software Center — too heavy to build/own), a
  heavyweight browser engine (we *curate* a light browser; we do not build one —
  §16), an email client (curate one), a photo-manager DB. Scope discipline
  (§22) is a feature.
- **Browser reality (honest):** no bundled heavyweight browser on the 512 MB
  floor. We curate a **light** browser (e.g. a Qt-WebEngine-free option or a
  memory-frugal build) and are explicit that modern heavy web apps will strain
  this hardware (§11, §16). This is a limitation of the *web*, not a Castalia
  bug — and we say so (P10).

---

## 10. XP Feature Parity Map

This maps the *user expectations* of the XP era to Castalia equivalents, and is
**honest** about the fidelity: **Equivalent** (matches or exceeds the
expectation), **Approximate** (delivers the intent, works differently), or
**Not attempted / different** (deliberately out of scope, with the reason).
Names on the right are original (§3.8).

| XP-era expectation | Castalia equivalent | Fidelity | Notes / honest caveats |
|---|---|---|---|
| Desktop with icons & wallpaper | Castalia Desktop (`castalia-desktop`) | **Equivalent** | Icons, wallpaper, right-click new/arrange; per-monitor wallpaper |
| Start menu | **Castalia Menu** (corner launch menu) | **Equivalent** | Same ergonomic; original name/art; search built in ✅ (accent-folding filter over apps + settings, Enter launches the first match) |
| Taskbar + notification area | Castalia Panel + tray | **Equivalent** | Bottom panel, window list, Quick Launch, tray, clock |
| Windows Explorer | **Castalia Explorer** | **Equivalent** | Browse/copy/search/network; tabs by 1.0 |
| My Computer | **Computer** (Explorer view of drives/devices) | **Equivalent** | Drives, removable media, network places |
| My Documents/Pictures/Music | XDG user folders w/ friendly icons | **Equivalent** | `~/Documents` etc., surfaced in menu + sidebar |
| Control Panel | **Castalia Control Center** | **Equivalent** | One hub; category tiles; search |
| Add/Remove Programs | **Software Center** (+ Update Center) | **Equivalent** | Install/remove native + curated apps; also Wine apps |
| Device Manager | **Hardware Center** | **Approximate** | Shows detected devices/drivers/quirks; Linux auto-loads drivers, so it's *inspect + toggle quirks*, not *install .inf drivers* |
| System Restore | **Restore Points** (Recovery Center) | **Equivalent** | rsync/btrfs snapshots; auto point before updates; boot-time rollback in Recovery env |
| Event Viewer | **Log Viewer** (Visor de registros) ✅ | **Equivalent** | Plain-text logs, filter/search; friendlier than raw. Shipped: /var/log + svlogd service logs, severity colouring, bounded tail |
| Services (services.msc) | **Services Manager** (Servicios del sistema) ✅ | **Equivalent** | Plain-language runit services; start/stop/enable. Shipped: /etc/sv + service.conf metadata, `sv status`, pkexec actions |
| Task Manager | **Task Manager** | **Equivalent** | Processes, meters, end task, startup, autostart |
| Windows Update | **Update Center** | **Equivalent** | Signed updates, channels, changelog, auto Restore Point |
| Security Center | **Security status** panel (Control Center) | **Approximate** | Firewall on/off, update status, ClamAV (opt), "old Windows app" warnings; not an AV suite |
| My Network Places | **Network Places** (Explorer + Network Center) | **Equivalent** | Browse/mount Samba shares; map as places |
| Printers & Faxes | **Printer Manager** | **Equivalent** (print); **Not attempted** (fax) | CUPS printers/jobs; fax is dead, out of scope |
| User Accounts | **User Accounts** | **Equivalent** | Local users, avatars, admin/standard, autologin |
| Fast User Switching | Switch User (elogind seats + LightDM) | **Approximate** | Works; on 512 MB two GUI sessions is tight — UI warns |
| Remote Desktop | **Remote Access** (opt-in VNC/RDP server + client) | **Approximate** | xrdp/x11vnc server (off by default, §15) + a client; not the exact RDP UX |
| Help and Support | **Help Center** | **Equivalent** | Fully offline, searchable, contextual |
| Search | Menu search + Explorer search (+ opt index) | **Equivalent** | Instant name search; optional content index ≥1 GB |
| Themes / Appearance | **Theme Manager** (5 themes) | **Equivalent** | One switch themes everything (§8) |
| Screensavers | **Screensaver** picker (xscreensaver-backed) | **Equivalent** | Curated, light savers + blank/lock; original art |
| Accessibility Options | Accessibility panel + High-Contrast theme | **Approximate** | HC theme, font scale, sticky/slow keys, big cursor, bell; full AT (screen reader) is later/opt-in |
| File associations | Explorer "Open With" + defaults (freedesktop MIME) | **Equivalent** | Per-type default handler; can route to native/Wine/emulator |
| Autorun / AutoPlay | **Autoplay policy** (Removable-media settings) | **Approximate by design** | Default **off/ask** — no silent autorun ever (§6.11, §15) |
| Power Options | **Power profiles** (Control Center) | **Equivalent** | Performance/Balanced/Power-saver, lid/brightness/sleep |
| Safe Mode | **Safe Mode** boot entry | **Equivalent** | Compositor off, minimal services, generic video |
| Recovery Console | **Recovery** boot entry + Recovery Center | **Equivalent** | Repair shell, fsck, chroot, rollback, boot repair |
| Driver Rollback | Restore Points + kernel/module pinning | **Approximate** | No per-device "roll back this driver" UI; instead roll back the system via Restore Points, or pin/hold a kernel/module version — honest difference from Windows |
| MSConfig / startup | Task Manager "Autostart" + Services Manager | **Equivalent** | Manage autostart apps + services |
| Command Prompt | **Terminal** | **Equivalent** | Full POSIX shell (more capable) |
| ClearType | Font antialias/hinting settings | **Equivalent** | Subpixel/greyscale AA per display |
| DirectX (for games) | Wine + Mesa (GL) / DOSBox-X | **Approximate/limited** | Old DirectX games via Wine where they run; modern DX-heavy games **won't** (§11) |

**Deliberately not attempted (and why):** Active Directory domain join
(enterprise, out of scope; Samba client only); NTFS as a *system* filesystem
(read old disks via ntfs-3g, but Castalia installs on ext4); Windows driver
model / `.inf` installation (Linux drivers are in-kernel); IE-specific web
features (dead); anything requiring Microsoft code or assets (§3).

---

## 11. Compatibility Strategy

Compatibility is a **spectrum of layers**, presented to the user as one honest,
managed experience. The governing rule (P10): *tell the truth about what runs,
what is emulated, and what never will.*

### 11.1 The layer stack (native-first)

| Layer | For | Tooling | User-facing surface |
|---|---|---|---|
| **1. Native Linux apps** | Everyday tasks | Curated packages | Software Center |
| **2. Curated light apps** | Replacing XP built-ins | First-party + vetted FOSS | Preinstalled / Software Center |
| **3. Wine (Win32/Win64)** | Selected Windows apps | Wine + winetricks, per-prefix | Wine Prefix Manager, Open With |
| **4. Boxedwine (sandboxed legacy)** | Old/fragile 16/32-bit Windows apps in a sandbox | Boxedwine | Advanced compat option |
| **5. DOSBox-X** | DOS + early-Windows apps/games | DOSBox-X | DOSBox-X Launcher |
| **6. ScummVM** | Supported adventure engines | ScummVM | ScummVM front-end |
| **7. 86Box / PCem (opt)** | Full-fidelity period PC (run a real period OS) | 86Box/PCem | Advanced, opt-in |
| **8. Network/peripheral compat** | Windows shares, printers | Samba, CUPS | Network Places, Printer Manager |

**Native-first policy:** the OS always prefers a native or curated app over
Wine. Wine/DOS/emulation are for software with *no adequate native option*
(legacy business apps, old games, sentimental software).

### 11.2 Wine strategy (the heart of Win32 compat)

- **[DECISION] Per-app prefixes.** Each Windows app installs into its **own**
  `WINEPREFIX` (`~/.local/share/castalia/wine/<app-id>/`), so one app's DLL
  overrides/winetricks hacks never contaminate another. Managed by **Wine
  Prefix Manager** (§9).
- **Wine version management:** ship a **known-good pinned Wine** as the default;
  allow *per-prefix* alternate Wine versions (staging, older) from the Castalia
  repo for apps that need them. The compat DB records which version worked.
- **Winetricks integration:** presets (common runtimes, fonts, DLLs) offered as
  one-click options *scoped to the prefix*, driven by the compat profile for the
  detected app.
- **Sandboxing:** optional **bubblewrap/Firejail** confinement per prefix
  (filesystem + network scoping), toggled in the profile (§6.18, §15). Default
  gives the app the user's home + one shared folder, not the whole system.
- **Compatibility database & app profiles:** a local, shipped-and-updatable DB
  (`/var/lib/castalia/compat-db`) mapping app signatures (installer hash / name)
  to a **profile**: recommended Wine version, winetricks, registry tweaks, and
  an **honest rating** — *Platinum/Gold/Silver/Bronze/Broken* (our own scale,
  informed by community data, not scraped verbatim). The UI shows the rating
  *before* the user invests time.
- **Safe defaults:** new prefixes are 64-bit-with-32-bit (`win7`-class default
  Windows version, adjustable), no admin, no network mounts, DXVK **only** where
  the GPU/GL can support it (rare on this hardware; default off).

### 11.3 File associations & "Open With"

- Explorer routes by MIME + a Castalia **handler map**: a `.exe`/`.msi` offers
  "Install with Wine" (→ Prefix Manager); a `.doc`/`.xls` prefers the curated
  native office app, with "Open With → Wine (Word/Excel prefix)" as an
  alternative; a DOS `.exe` or a game folder offers "Run in DOSBox-X"; a
  supported game data folder offers "Play in ScummVM". The user can set any
  default and always sees the honest options.

### 11.4 DOS, adventure games, and full emulation

- **DOSBox-X Launcher** hides `dosbox.conf` behind a friendly form (mount,
  cycles, scaler, per-game profiles). Target: "add folder → Play".
- **ScummVM** front-end lists supported engines; ships only with
  legally-clean/freeware game data (§3.9) and detects user-provided data.
- **86Box/PCem** (opt-in, advanced): for enthusiasts wanting to run a genuine
  period OS in emulation — clearly separated from the core promise; the user
  supplies their own legal OS media.

### 11.5 Network & peripheral compatibility

- **Samba client** → **Network Places** (browse/mount Windows shares; SMB1 is
  **off by default** for security, with a clearly-warned legacy toggle for
  ancient NAS/PCs). **[DECISION]** No Samba *server* in the base (opt-in later).
- **CUPS** covers the vast majority of period printers; the Printer Manager
  wizard handles USB + network.

### 11.6 What will NOT work — stated plainly (P10)

The **Help Center** and Wine Prefix Manager repeat this honestly:

| Won't work | Why |
|---|---|
| Kernel-mode Windows drivers (`.sys`), hardware utilities that talk to Windows drivers | Wine is a user-space Win32 layer, not the NT kernel; there is no Windows driver model on Linux |
| Most modern anti-cheat (kernel/ring-0 anti-cheat) online games | Anti-cheat explicitly blocks Wine/virtualization; unfixable by us |
| Modern DirectX 10/11/12-heavy games | The target GPUs lack the horsepower/features regardless of translation; DX9-era and older is the realistic ceiling, and even then case-by-case |
| Certain copy-protection / DRM (SecuROM/StarForce-class, some dongles) | Ring-0 DRM and exotic dongles don't survive Wine; some period titles simply won't run |
| Apps needing very modern Windows APIs / .NET-latest | Old hardware + Wine + old runtimes bound what's feasible; newest frameworks are out of range |
| Obscure Win-only peripherals (some scanners, capture cards, winmodems) | No Linux driver + Wine can't bridge kernel hardware access |

**The honest framing we ship:** *"Castalia runs a curated set of Windows
applications well through Wine, excels at DOS and classic games, and is a great
home for old productivity and media software. It is not a way to run modern
Windows games or hardware that needs Windows drivers — and we'll always tell you
up front whether a given app is likely to work."*

---

## 12. Programming Languages and Frameworks

Choices optimize for four things, in order: **(1) runs well on old hardware**,
**(2) maintainable by a tiny team for a decade**, **(3) small binaries / low
RAM**, **(4) developer velocity**.

### 12.1 Language matrix

| Layer | Language | Rationale (old-HW / maintainability / size / velocity) |
|---|---|---|
| **Desktop shell, Explorer, Control Center, all first-party apps** | **C++17** + **Qt 5.15 LTS** | One toolkit for the whole UI = cohesion + shared `libcastalia-ui`; native code = low RAM/fast start on P4; Qt5 is stable, mature, LTS, and *proven light* (LXQt); QSS gives the CSS-like theming the brief wants. C++17 (not 20) for broad old-toolchain compatibility. |
| **System glue, boot/build scripts, service `run` scripts, installer orchestration** | **POSIX shell** | Universal, tiny, no runtime; exactly right for runit/init glue and ISO/build steps. Kept simple and `shellcheck`-clean. |
| **Low-level patches, kernel config bits, small perf-critical helpers, Xorg/driver shims** | **C (C11)** | The lingua franca of the Linux plumbing we integrate with; minimal footprint; direct. |
| **A few safe, long-running daemons** (hwprobe, Restore-Points engine, update helper) | **Rust** *(pragmatic, optional)* | Memory-safety where a bug is a security/reliability risk and the code runs as root/near-root; builds on the *modern build host* (not the target), so toolchain weight isn't a target concern. **[DECISION]** Rust is allowed but **not required** — each daemon may start as C and graduate to Rust; the desktop must never *depend* on a huge Rust runtime tree. |
| **Installer backend, build tooling, CI scripts, image assembly, test harness** | **Python 3** | Great velocity for tooling and the installer's *logic* (not its always-running UI). **[DECISION]** Python is a **tooling/installer** language, **never** a dependency of the running desktop or a resident service (no heavy Python daemon on the floor tier). |
| **Theme/config definitions** | **INI / TOML** (+ **QSS** for widget styling) | Human-editable, comment-friendly, trivially parsed, corruption-resistant (P5). TOML for structured config, INI for simple key/values, QSS for the visual layer. |
| **Optional scripting/extensibility** (later) | **Lua** | Tiny, embeddable, fast; reserved for *optional* power-user extension points (e.g. Explorer actions) — never required for core function. |
| **Build systems** | **CMake** (C/C++ apps) + **Meson** (where a component already uses it) | **[DECISION]** CMake is the **primary** build system for all first-party C++/Qt code (universal Qt support, everyone knows it); Meson is tolerated only for third-party components that already ship it. One primary = less cognitive load for a small team. |

### 12.2 Qt5 vs GTK3 — decided

**[DECISION] Qt 5.15 LTS**, not GTK3, as the first-party toolkit.

- **Why Qt5 wins here:** one toolkit builds the *entire* product (shell, file
  manager, control panels, apps) with a **shared style + widget library**, so
  cohesion (P3) is nearly free; **QSS** delivers the CSS-like theming the brief
  asks for; the API is **stable** across the 5.15 LTS line (GTK3→GTK4 churn and
  GTK's CSS-theming instability are real maintenance taxes for a themed
  product); **LXQt proves Qt is light** on old hardware; C++ integration is
  first-class. We *support* GTK apps (theme bridge, §6.16) but *build* in Qt.
- **Why not Qt6:** Qt6 raises the compiler/CPU/GL baseline (drops some legacy GL
  paths, needs newer toolchains) — wrong for GMA/P4. **Qt 5.15 LTS** is the
  sweet spot: modern enough, old-hardware-friendly, long-lived.
- **Risk & mitigation:** Qt5 upstream LTS is winding down over the coming years;
  Debian will carry security fixes for our support window, and the KDE
  community's Qt5 patch collection is a fallback. We revisit a Qt6 (or a very
  light custom toolkit) migration only if/when the *recommended* hardware tier
  moves up — not for v1.

### 12.3 Shared foundations

- **`libcastalia-ui`** (C++/Qt5): the common widget/style/theming/help-hook
  library every first-party app links. Enforces look, 800×600 layout rules,
  animation policy, and the Help (F1) contract (§9.5).
- **`libcastalia-sys`** (C/C++): thin wrappers over udisks/polkit/eudev/runit/
  apt used by settings apps, so app code stays declarative.
- **Coding standards, linters:** `clang-format` + `clang-tidy` for C/C++,
  `shellcheck` for shell, `ruff`/`black` for Python tooling, `rustfmt`/`clippy`
  for Rust. Enforced in CI (§17).

---

## 13. Package and Update Model

Built on **dpkg/apt** (from the Debian base, §5) with a **signed Castalia
overlay repository** layering our shell, apps, themes, and metapackages. The
goal: current, **safe**, and — above all — **un-brickable** on old machines.

### 13.1 Repository structure

| Repo | Contents | Trust |
|---|---|---|
| **Base** (Debian stable, de-systemd'd mirror subset) | Kernel, Xorg, libraries, curated system packages | Debian keys + Castalia snapshot pin |
| **Castalia Core** | Shell, first-party apps, themes, branding, default configs, metapackages (`castalia-desktop`, `castalia-compat`, `castalia-min`) | **Castalia signing key** |
| **Castalia Community** | Vetted extra apps, extra themes/icons, games | Castalia community key, reviewed |
| **Castalia Non-free-firmware** *(optional, legal-gated)* | Wi-Fi/GPU firmware, microcode where redistribution is permitted | Separate key; **opt-in** at install (§14), never required for wired path |

- **Metapackages** define editions/roles: installing `castalia-desktop` pulls a
  known-good, tested set; `castalia-compat` adds Wine/DOSBox/ScummVM;
  `castalia-min` is the floor build. This is how we ship *coherent* systems
  instead of a pile of packages.

### 13.2 Update channels & signing

| Channel | Who | Cadence | Guardrails |
|---|---|---|---|
| **Stable** | Everyone (default) | Security + curated updates | Every batch tested on the QA matrix (§19) before publish |
| **Testing** | Enthusiasts | Pre-release | Auto Restore Point; "may break" banner |
| **Nightly** | Developers | Continuous | No guarantees; VM-first |

- **All packages and repo indexes are signed**; apt verifies signatures; the
  Castalia keyring ships in the base and rotates via a documented process.
- **Reproducible-ish builds:** first-party packages build deterministically in
  CI from tagged sources; build inputs are pinned (§17). Full bit-for-bit
  reproducibility is a *goal*, not a v1 gate (**[ASSUMPTION]** upstream deps
  limit this).

### 13.3 The anti-brick update design (the important part)

**[DECISION]** Every system-level update transaction is wrapped so a failure or
power loss is *survivable* on old, flaky hardware:

1. **Pre-flight:** check free space (refuse if the disk can't hold the update +
   headroom — a top cause of bricking), check power (warn on battery), and
   verify signatures.
2. **Auto Restore Point** *before* applying system updates (rsync/btrfs, §6.5) —
   automatic, not optional, on the system tier.
3. **Apply** via apt with careful ordering; the bootloader/kernel is updated
   **last** and the *previous* kernel is **kept** (never remove the running
   kernel; keep N-1 as a boot fallback).
4. **Post-flight health check** on next boot; if the new kernel/desktop fails,
   the GRUB menu already offers the **previous kernel** and **Safe Mode**, and
   the Recovery env can **roll back the Restore Point** (§6.13).
5. **Never** auto-reboot; **never** silently install. The Update Center shows a
   clear "safe to restart" state and a one-click rollback link.

### 13.4 Rollback, snapshots, offline & deltas

- **Rollback / Restore Points:** the unified recovery primitive (§6.13, §9
  Recovery Center). Default rsync/hardlink on ext4 (works on any disk); btrfs
  snapshots on opt-in btrfs installs for cheaper points. Points are created
  before updates and on user demand; retention is space-aware.
- **Offline package bundles:** the Software/Update Centers can **export** a
  needed package set to USB from a connected machine and **import** it on an
  offline one (`.deb` set + signed index). This is essential for air-gapped and
  no-Ethernet-driver situations (P6).
- **Delta updates — honest stance:** **[DECISION]** No binary deltas in v1.
  Debian's model doesn't do per-package binary deltas well (only index pdiffs);
  faking it adds fragility. We instead minimize download pain with careful
  packaging, `apt` index pdiffs, and offline bundles. Revisit deltas only if
  bandwidth proves a real blocker (**[SPIKE]**).

### 13.5 How we avoid bricking old machines — checklist

- Keep N-1 kernel; update bootloader last; never remove the running kernel.
- Refuse updates that don't fit the disk (with headroom).
- Always Restore-Point before system changes.
- Signed everything; verify before apply.
- Recovery env is **independent** of the installed root.
- Warn on battery / low power during updates.
- Test every Stable batch on the real-hardware + emulator matrix (§19) before it
  ships. **The update pipeline is a release-engineering responsibility, not an
  afterthought.**

---

## 14. Installer and First Boot Experience

The install is the first impression and a make-or-break moment on flaky old
hardware. It must be **forgiving, honest, and safe** (never silently eat a
dual-boot Windows partition), and it must **fall back to text** when the GPU
won't do graphics.

### 14.1 Live ISO

- **[DECISION]** A **hybrid live ISO** (USB *and* CD/DVD bootable via isolinux
  El-Torito) boots to a working Castalia desktop *before* installing, so users
  can **test hardware first** (does Wi-Fi work? sound? display?). The live
  session runs from RAM/overlay; an **"Install Castalia"** icon sits on the
  desktop.
- Boot menu, in the order it appears: **Probar Castalia OS (sesión en vivo)**
  — the default — **Instalar Castalia OS en el disco**, **sesión en vivo con
  gráficos seguros (VESA)**, and **instalar en modo texto**. No memory-test
  entry: we ship no `memtest86+.bin`, and an entry that boots nothing is worse
  than no entry (add both together or neither).
- **Shipped** — and this took a correction. The menu lived in
  `iso/isolinux/isolinux.cfg` as a design nothing read, while `build/mkiso.sh`
  wrote its own **single-entry** config inline; every ISO published up to that
  point therefore had one boot entry, no installer entry anywhere, and a
  `castalia.installer=` kernel argument no code on the image parsed. The menu
  is now one template in the repo (`iso/isolinux/isolinux.cfg.in` +
  `entries-install.cfg`) rendered per edition by mkiso.sh, the install entries
  appear only for an edition whose profile sets `INSTALLER="yes"` (so the lean
  boot-proof image never offers what it cannot deliver), and
  `tools/tests/test_iso_boot.py` fails the build if the two drift apart again
  or if the menu offers a `castalia.installer=` mode the live session has no
  case for.
- **Both install entries boot the ordinary live session first** and pass
  `castalia.installer=gui|text`;
  `/usr/local/bin/castalia-live-session` reads it out of `/proc/cmdline`. The
  graphical path opens `castalia-instalador` **over the live desktop**, so the
  machine you are about to install onto is one you have already seen working.
- **The live desktop shows an "Instalar Castalia OS" icon** — the fifth fixed
  icon, present only when `castalia::isLiveSession()` (the launcher exports
  `CASTALIA_LIVE=1`; live-boot's `/run/live/medium` is the second opinion). An
  installed desktop never shows it.
- **The build proves the image, not its own exit code.** `stage_hook` runs the
  chroot under `set -e` and then *checks the rootfs*: `castalia-panel`,
  `castalia-desktop` and `castalia-session` must be under `/opt/castalia`, and
  an `INSTALLER="yes"` edition must also carry `castalia-live-session` and
  `castalia-instalador`, or the build dies before squashing. This is not
  belt-and-braces: `castalia-live-desktop-amd64` 0.1.1 shipped **with no
  `/opt/castalia` at all** because the chroot's cmake could not read
  `../VERSION` (not in `SRC_DIRS`) and the stage's exit status was `rm`'s. An
  image with no shell keeps the base autologin getty, so it booted to a root
  console.
- **Graphics failure is not a dead end.** If X cannot start on that hardware
  the live launcher prints, in Spanish, the commands that still work
  (`castalia-live-session`, `castalia-instalar-texto`, `reboot`) and hands over
  a root shell on tty1 — the same two commands are in `/etc/issue`. Before
  this, `startx` was `exec`'d from a respawning inittab line and a machine
  whose GPU it could not drive simply showed nothing.

### 14.2 Graphical installer (with text fallback)

- **[DECISION]** A **Qt5 graphical installer** (shares `libcastalia-ui`) for the
  normal path; a **dialog/ncurses text installer** as the guaranteed fallback
  for G4/VESA-only or broken-GL machines. Both drive the **same** Python backend
  (§12) so logic is shared and testable.
- **Status:** the shared backend is implemented and tested
  (`installer/castalia_installer`, `python3 -m castalia_installer --dry-run`):
  a pure `build_plan(config, disk)` produces an inspectable, ordered step list;
  `engine.execute()` runs it behind a typed-confirmation gate; 38 unit tests
  assert the layout, safety gate, fstab UUIDs and offline-capability, and the
  loopback smoke proves the real parted/mkfs/rsync path on a disk image.
- **Graphical front-end shipped** (`installer/gui`, binary
  `castalia-instalador`): a themed Qt wizard (Bienvenida → Disco → Cuenta →
  Resumen → Instalación) sharing `libcastalia-ui`. The Resumen page shows the
  *real* plan by calling the backend `--dry-run`, and the "Instalar" button
  stays disabled until the user types the exact target disk to confirm. It
  probes real disks via `lsblk` and streams the backend's progress live. It is
  staged on the desktop/compat live ISOs (with python3, parted, rsync, grub-pc)
  and reachable from the Start menu.
- **Text front-end shipped** (`castalia_installer.tui`, `castalia-instalar-
  texto`): the guaranteed graphics-free fallback (§14.5 #5). A plain
  line-based flow driving the *same* backend and confirmation gate, fully
  unit-tested with scripted input (no TTY needed) — so the fallback is proven,
  not aspirational.
- **End-to-end proven** ✅: `installer/tests/qemu-install.sh` runs the *full*
  installer (partition → format → copy → UUID fstab → **GRUB in the chroot** →
  UUID-root fixup → user) to a loopback disk from a real bootable Debian, then
  **boots that disk in QEMU** and reaches userspace on its own
  (`PASS — Debian GNU/Linux 12 pc-castalia ttyS0`, evidence in
  `installer/tests/last-qemu-install-evidence.txt`). Booting by root-FS UUID
  (not the install-time device path) is a real correctness fix landed here.
  Remaining Phase 5 work: dual-boot detection, recovery/Restore Points.
- Installer stages (each skippable-with-defaults where safe):

| Stage | What it does | Safety |
|---|---|---|
| **Language/keyboard** | Locale + keymap | Detected default |
| **Hardware probe** | Runs hwprobe (§6.15): GPU/audio/NIC/suspend + quirks | Shows results; picks safe drivers |
| **Display test** | Confirms a working mode **before** committing; 15-s auto-revert | Never strands user at black screen |
| **Network setup** | Wired DHCP auto; Wi-Fi optional; **offline OK** | Works fully offline (P6) |
| **Disk / partitioning** | Guided (whole-disk) or manual; **dual-boot detection** | See §14.3 |
| **Filesystem** | ext4 default; btrfs (snapshots) optional on capable disks | Sensible defaults |
| **Bootloader** | GRUB2 to MBR/EFI; detects other OSes to chain-load | Confirms target device explicitly |
| **User creation** | Local user, hostname, admin/standard, optional autologin (warned) | No online account (P7) |
| **Theme selection** | Pick one of the 5 themes + accent + wallpaper | Live preview |
| **Sound test** | Play the (original) test chime; adjust device/volume | Skips if no audio HW |
| **Firmware opt-in** | Offer optional non-free firmware (Wi-Fi/GPU) *only if* a device needs it and it's legal | Clearly opt-in (§13) |
| **Summary/commit** | Plain-language recap; **explicit** "this will erase X" | Typed confirm for destructive |

### 14.3 Disk, dual-boot, and bootloader safety

- **Dual-boot is a first-class, guarded scenario:** the installer **detects
  existing OSes/partitions** (including a Windows install) and, by default,
  offers **"Install alongside"** (shrink + use free space) or **manual**, with
  a **loud, explicit warning** before any destructive action and a required
  typed/again confirmation for "erase whole disk."
- Partitioning: guided creates `/`, a small `/boot` kept within the first
  128 GB (old-BIOS safety, §6.2), and swap sized to RAM (with hibernate off by
  default, §4.6). Manual mode for experts.
- GRUB is installed to the chosen device only after an explicit confirmation of
  *which* disk; detected other OSes are added as boot entries.

### 14.4 First boot: welcome, migration, recovery entries

- **First-boot Welcome Tour** (§9): warm, brief, skippable; offers theme tweak,
  points at Help Center, and reassures "no account needed" (P7).
- **"Migrate from an old Windows PC" assistant** (§9 Migration Assistant): if an
  NTFS disk is present (internal second drive or USB), offer to **copy** the old
  Documents/Pictures/Music/Desktop/Favorites (and browser bookmarks by 1.0) into
  the new home. **Read-only** on the source; never modifies the old disk;
  clearly framed as "we'll copy your files, we won't touch the original."
- The GRUB menu always carries **Safe Mode** and **Recovery** entries from first
  boot (§6.2, §6.13), and the previous kernel after the first update (§13.3).

### 14.5 Installer non-negotiables

1. Never destroy data without an explicit, unambiguous, typed confirmation.
2. Always leave a bootable path (previous OS entry preserved when dual-booting).
3. Prove the display works before committing a mode.
4. Complete a full install **offline**.
5. Fall back to text UI when graphics fail — never a dead end.

---

## 15. Security Model

Realistic for the threat model of an offline-leaning, retro desktop on old
hardware — **secure by default without being annoying**, and **honest** about
where the boundaries are (P10). We are not promising enterprise security on a
2005 laptop; we *are* promising sane defaults, no surveillance, and clear
warnings.

### 15.1 Core posture

| Control | Default | Notes |
|---|---|---|
| **Desktop runs non-root** | Yes | Admin actions via **polkit** prompts; first user is in the admin (sudo) group; no root login on the desktop |
| **Firewall** | **On**, default-deny inbound | `nftables` with a simple Castalia ruleset; Network Center exposes it in plain language; outbound allowed |
| **Signed updates** | Yes | apt signature verification; Castalia keyring (§13) |
| **Telemetry** | **None** by default | No data collection; any diagnostics are local, opt-in, inspectable (P7) |
| **Automatic remote access** | **Off** | No SSH/VNC/RDP server enabled by default; Remote Access is explicit opt-in with a warning (§10) |
| **Autorun/AutoPlay** | **Off / ask** | No code runs from removable media automatically, ever (§6.11) |
| **Account model** | Local only | No online identity; passwords hashed (yescrypt/sha512 via PAM) |
| **Screen lock** | Available, on-suspend optional | elogind idle → lock; keyboard-lockable |

### 15.2 App & compatibility sandboxing (and its honest limits)

- First-party apps follow least-privilege; privileged ops go through polkit, not
  setuid sprawl.
- **Wine apps:** run as the user, in isolated prefixes, with optional
  **bubblewrap/Firejail** confinement (fs + net scoping) per profile (§6.18,
  §11). Default access = user home + one shared folder, not the whole system.
- **Bundled/curated browser** and user-flagged "untrusted" Wine apps can run
  under a Firejail profile.
- **The honest caveat (shipped in docs):** *Wine is a compatibility layer, not a
  security boundary against hostile native Windows malware. A malicious Windows
  program run under Wine can still harm your files. Only run software you
  trust.* We say this plainly in the Wine Prefix Manager and Help Center.

### 15.3 Old-hardware / old-web security realism

- **Browser on 32-bit old hardware is the weakest link.** Modern JS-heavy sites
  strain the CPU/RAM, and 32-bit browser builds age out of security support
  faster. We: (a) curate a **memory-frugal, still-patched** browser; (b) warn in
  Help that the modern web is hostile to this class of machine; (c) recommend
  the browser primarily for light/older sites and offline use; (d) keep the
  browser sandboxed (Firejail) and updated via the security channel.
- We do **not** pretend a 2005 machine is a safe host for sensitive banking on
  the modern web — the docs advise using a modern device for high-risk tasks
  (P10). This honesty is a feature.

### 15.4 Optional protective tools

- **ClamAV** (optional, install-on-demand): an on-demand scanner surfaced in the
  Security panel and Explorer context menu, useful mainly for scanning Windows
  files/Wine downloads before running them. **Not** a resident real-time shield
  (too heavy for the floor); framed accurately.
- **Security status panel** (Control Center, §10): shows firewall state, update
  status, remote-access state, and whether risky toggles (SMB1, autorun,
  autologin) are on — a single honest dashboard, not an AV-suite cosplay.

### 15.5 Secure-but-not-annoying principles

- Prompt for privilege **only** when truly needed, with plain-language reasons.
- Safe defaults mean most users never see a security dialog.
- Every risky option (SMB1, autologin, remote access, non-free firmware, running
  an untrusted `.exe`) is **off by default** and carries a one-line honest
  warning at the point of enabling — not a wall of legalese.
- No nagging, no "your PC is at risk!" dark patterns, no upsell.

---

## 16. Performance Budgets

Budgets are **enforced in CI** on emulated reference machines and spot-checked
on real hardware (§19), not merely aspired to. Two reference targets bracket the
range; a feature that blows the **floor** budget does not ship (P1/P2).

**Reference machines:**
- **FLOOR (T2):** single-core-equivalent P4 @ ~2.4–3 GHz, **512 MB RAM**, GMA-
  class GPU, 5400 rpm PATA/SATA, **800×600**, compositor **off**.
- **TARGET (T1):** Core 2 Duo @ ~2 GHz, **2 GB RAM**, GMA/nouveau, SATA,
  **1024×768+**, compositor optional-on.

### 16.1 Boot-time budgets

| Metric | FLOOR (P4/512 MB) | TARGET (C2D/2 GB) |
|---|---|---|
| Power-on → GRUB | (BIOS-bound, excluded) | — |
| GRUB → login greeter | ≤ **35 s** | ≤ **18 s** |
| Login → usable desktop (panel + menu responsive) | ≤ **20 s** | ≤ **10 s** |
| Cold boot → usable (greeter+login+desktop, autologin) | ≤ **55 s** | ≤ **28 s** |

*(Rationale: XP itself took 40–90 s on this hardware; we aim to feel at least as
fast, ideally faster, thanks to runit + a lean stack.)*

### 16.2 Memory budgets

| Component | FLOOR RSS target | TARGET RSS target |
|---|---|---|
| **Idle desktop** (kernel+services+shell, no apps) | ≤ **170 MB** used | ≤ **300 MB** used |
| Shell alone (panel+desktop+session) | ≤ **60 MB** | ≤ **80 MB** |
| Control Center | ≤ **25 MB** | ≤ **35 MB** |
| Explorer window | ≤ **35 MB** | ≤ **55 MB** |
| Headroom for one user app on FLOOR | ≥ **250 MB free** after idle | — |

*(Idle ≤170 MB on 512 MB leaves real room to work — the whole point.)*

### 16.3 Responsiveness budgets

| Interaction | FLOOR | TARGET |
|---|---|---|
| Launch menu open (first paint) | ≤ **150 ms** | ≤ **80 ms** |
| Menu search first results | ≤ **200 ms** | ≤ **100 ms** |
| Explorer launch (window visible) | ≤ **600 ms** | ≤ **350 ms** |
| Explorer directory (1k entries) list | ≤ **400 ms** | ≤ **200 ms** |
| Control Center open | ≤ **400 ms** | ≤ **250 ms** |
| Window drag/resize (no compositor) | ≥ **30 fps** perceived | ≥ **50 fps** |
| Alt+Tab switch | ≤ **120 ms** | ≤ **60 ms** |
| Animation frame budget (when on) | animations **off** on FLOOR | ≤ **150 ms**, ≥ 30 fps |

### 16.4 Enforced rules

- **Compositor optional, never mandatory.** The desktop is fully usable and
  attractive with picom off; picom auto-on only ≥2 GB + adequate GL (§4.4).
- **No mandatory heavy browser** in the minimum install; the FLOOR image ships
  Help + a light browser, not a 400 MB-RSS engine (§9.5).
- **UI thread never blocks** on disk/network — all such work is async/worker
  (§7.12); a "slow operation" that would exceed budget shows progress instead of
  freezing.
- **CI perf gates (§19):** a nightly QEMU FLOOR image boots, launches each app,
  and records boot time, idle RSS, and menu/Explorer latency; a regression past
  budget **fails the build**. Real-hardware spot checks each release confirm the
  emulated numbers.
- **Budget changes are reviewed like API changes** — raising a FLOOR budget
  requires justification and sign-off, because the FLOOR *is the product's
  promise*.

---

## 17. Build System and Repository Structure

A **monorepo** (with third-party pulled as pinned submodules/tarballs) so a
small team has one place, one CI, one version scheme. The build produces:
first-party **`.deb` packages** → a **signed Castalia repo** → **live/install
ISOs** per edition → **QEMU test images**.

### 17.1 Repository tree

```
castalia-os/
├─ build/            # top-level build orchestration
│   ├─ Makefile          # `make iso EDITION=classic64` etc. (thin over scripts)
│   ├─ mkrepo.sh         # assemble + sign the apt overlay repo
│   ├─ mkiso.sh          # build live/install ISO per edition (live-build/debootstrap)
│   ├─ profiles/         # edition profiles: classic32, classic64, legacy32, min
│   └─ chroot/           # bootstrap + package-set definitions (metapackages)
├─ docs/             # THIS bible + all docs (§20); source of the offline Help
├─ branding/         # logos, wordmarks, boot/login art, spec/ (controls.svg, grid)
│   ├─ spec/             # design tokens: colors, grid, icon grid, control geometry
│   ├─ boot/  login/     # boot splash + greeter assets (original)
│   └─ sound/            # original system sounds (ogg) + source project files
├─ themes/           # Classic/Azul/Oliva/Plata/Medianoche/HighContrast bundles
│   ├─ engine/           # theme.conf schema + apply logic (§6.16)
│   ├─ icons/            # original icon family (SVG + prebaked PNG, §8.4)
│   └─ cursors/  fonts/  # original/libre cursors + libre/original fonts
├─ shell/            # C++/Qt5 shell: panel, desktop, session, menu, tray
│   ├─ libcastalia-ui/   # shared widget/style/help library (§12.3)
│   ├─ libcastalia-sys/  # system wrappers (udisks/polkit/runit/apt)
│   ├─ panel/ desktop/ session/ menu/ tray/ switcher/
│   └─ explorer/         # Castalia Explorer (file manager)
├─ apps/             # first-party apps (§9), one dir each, all on libcastalia-ui
│   ├─ control-center/ hardware-center/ network-center/ sound-mixer/
│   ├─ display-settings/ theme-manager/ user-accounts/ software-center/
│   ├─ update-center/ recovery-center/ task-manager/ services-manager/
│   ├─ log-viewer/ disk-manager/ printer-manager/ archive-manager/
│   ├─ text-editor/ rich-editor/ paint/ calculator/ screenshot/
│   ├─ media-player/ image-viewer/ terminal/ help-center/ welcome/
│   ├─ migration-assistant/ wine-prefix-manager/ dosbox-launcher/
│   └─ games/            # curated legally-clean games (§9.4)
├─ installer/        # Qt5 GUI + ncurses TUI front-ends + shared Python backend
├─ packages/         # debian/ packaging for every first-party component
│   ├─ metapackages/     # castalia-desktop, castalia-compat, castalia-min...
│   ├─ kernel/           # custom kernel config(s) if needed (§6.3)
│   └─ compat/           # Wine/DOSBox/ScummVM integration packages + compat-db
├─ iso/              # ISO layout templates, isolinux/grub menus, splash
├─ tests/            # test suites: unit (per app), integration, QEMU smoke,
│   ├─ qemu/             # boot/install/perf harness (§16, §19)
│   ├─ ui/               # 800×600 + 1024×768 screenshot/layout tests
│   └─ compat/           # Wine/DOSBox app-profile regression tests
├─ tools/            # dev tooling: theme linter, provenance checker, asset baker
├─ third_party/      # pinned upstream (submodules/tarballs) + patches/
├─ legal/            # LICENSE(s), ASSET_PROVENANCE.csv, THIRD_PARTY.md, notices
└─ ci/               # pipeline definitions, runners, signing config (keys NOT here)
```

### 17.2 Build pipeline (source → ISO)

1. **Build first-party packages:** CMake builds each `shell/` + `apps/`
   component → `.deb` via the `packages/debian/` metadata. Unit tests run here.
2. **Bake assets:** `tools/` rasterizes SVG icons to PNG per size, validates
   themes against the schema, and **runs the provenance checker** (fails if any
   binary asset lacks an `ASSET_PROVENANCE.csv` row, §3.9).
3. **Assemble + sign the repo:** `build/mkrepo.sh` combines Castalia `.deb`s
   with the pinned Debian base snapshot into a signed apt repo.
4. **Build ISOs:** `build/mkiso.sh` (over `debootstrap`/`live-build`) produces
   live+install ISOs per **edition profile** (classic32/64, legacy32, min),
   installing the right metapackages and default configs.
5. **Sign artifacts:** ISOs + repo indexes signed; checksums published.
6. **Test images:** produce QEMU disk images for the smoke/perf/install suites.

### 17.3 Signing & secrets

- **Signing keys live outside the repo** (a hardware token / offline key for the
  release key; a CI key for nightlies). `ci/` references them by name only.
- Release signing is a **manual, gated** step (a human approves a Stable
  publish); nightlies auto-sign with the lower-trust CI key.

### 17.4 CI (`/ci`)

- **Per-commit:** build all first-party packages; run unit tests; `clang-tidy`/
  `shellcheck`/`ruff`/`clippy`; theme + provenance linters; license check.
- **Nightly:** full ISO build (all editions) → **QEMU smoke + perf suite**
  (§16, §19): boot the FLOOR image, launch every app, assert boot time/idle RSS/
  latency budgets, run install + upgrade + rollback tests, run the 800×600
  layout screenshot diff, run the Wine/DOSBox compat regressions.
- **Release:** the nightly suite **plus** a real-hardware certification pass
  (§19) before a Stable publish.

### 17.5 Versioning

- **[DECISION]** Product versions are `MAJOR.MINOR` with an original code-name
  per release (§21). Packages use Debian-style versions. One monorepo tag per
  release drives repo + ISO builds reproducibly.

---

## 18. Development Roadmap

Phased, MVP-first, each phase shippable/demoable. **Non-goals per phase are as
important as goals** — they are the scope-discipline mechanism (§22). Timeframes
assume a very small team (1–3 people) and are deliberately conservative.

> **Guiding rule:** *Every phase ends with something that boots and can be shown.
> No phase is "infrastructure only" with nothing to run.*

### Phase 0 — Research & prototype
- **Goals:** Validate the base (Debian de-systemd + runit) on real P4 + C2D;
  prototype `libcastalia-ui` + the panel/menu in Qt5; stand up the monorepo, CI,
  and ISO build skeleton; resolve open **[SPIKE]**s (PipeWire-on-floor,
  NetworkManager-vs-connman, Boxedwine, btrfs-vs-rsync restore).
- **Deliverables:** monorepo + CI; a throwaway ISO that boots to Openbox on real
  hardware; a Qt panel proof-of-concept; a written spike report per open item.
- **Non-goals:** any real app; theming polish; installer.
- **Acceptance:** ISO boots on the FLOOR + TARGET reference machines; panel PoC
  opens a menu within budget; base/init choice **confirmed or fallback taken**.
- **Risks:** base choice wrong (mitigated by the fallback, §5.4); Qt5 too heavy
  (measured, not assumed).
- **Test plan:** QEMU boot; real-HW boot on 2+ machines; RSS/latency spot-check.

### Phase 1 — Bootable base ISO
- **Goals:** A reproducible, signed **base** live+install ISO (no custom shell
  yet) with kernel, Xorg, audio, wired net, runit, apt + Castalia repo skeleton.
- **Deliverables:** `castalia-min` ISO (classic32 + classic64); signed repo;
  hwprobe v0.
- **Non-goals:** shell, apps, GUI installer (text install OK here).
- **Acceptance:** boots + text-installs on FLOOR/TARGET; wired DHCP + audio work;
  apt installs from the Castalia repo.
- **Risks:** old-GPU Xorg regressions (per-GPU quirks table).
- **Test plan:** QEMU + real-HW boot/install; audio/net smoke.

### Phase 2 — Themed desktop MVP (existing WM)
- **Goals:** Castalia Panel + desktop + menu + tray on Openbox; the **default
  theme** (Classic) end-to-end (Openbox deco + QSS + icons + cursor + greeter);
  LightDM greeter themed.
- **Deliverables:** `castalia-desktop` metapackage; a desktop that *looks like
  Castalia*; Theme Manager MVP (switch bundled themes).
- **Non-goals:** Explorer, Control Center, compat layer.
- **Acceptance:** boots to a cohesive themed desktop within boot/RSS budget on
  FLOOR; menu/tray/clock work; theme switch works.
- **Risks:** cohesion/perf tradeoff (enforce §16 gates now).
- **Test plan:** 800×600 layout test; idle RSS; menu latency.

### Phase 3 — Castalia Control Center MVP
- **Goals:** Control Center hub + the essential panels: Display, Sound, Network,
  User Accounts, Theme Manager, Power profiles.
- **Deliverables:** those apps at MVP scope (§9); polkit integration.
- **Non-goals:** Explorer, Software/Update, compat.
- **Acceptance:** a user can set resolution, volume, wired network, create a
  user, switch theme, pick a power profile — all within budget on FLOOR.
- **Risks:** RandR/audio edge cases on old GPUs/codecs (quirks table).
- **Test plan:** per-panel functional tests on emulated + 1 real machine.

### Phase 4 — Castalia Explorer MVP
- **Goals:** The file manager: browse/copy/move/rename/Trash, sidebar places,
  USB mount (udisks), list+icon views, basic search, Open With.
- **Deliverables:** Explorer MVP; desktop-icon integration; Archive Manager MVP.
- **Non-goals:** network places, tabs, thumbnails (1.0).
- **Acceptance:** reliable file ops within latency budget; USB stick mounts;
  no UI-thread stalls on a 1k-file dir.
- **Risks:** async I/O correctness (worker-thread tests).
- **Test plan:** file-op integration suite; USB mount test; latency gate.

### Phase 5 — Installer & recovery
- **Restore Points shipped** ✅ (P8): a space-efficient hardlinked rsync
  snapshot engine that works on plain ext4 — no btrfs
  (`recovery/castalia_recovery`, `castalia-restore create|list|restore|prune`,
  `--dry-run`). `snapshot_argv`/`restore_argv` are pure and unit-tested (11
  tests: `--link-dest` dedup, /home never captured, restore direction,
  list/prune ordering, the `--confirm` gate); a real tmpdir smoke
  (`recovery/tests/snapshot-smoke.sh`) proves it end to end — snapshot, break
  the system, restore, and verify (edit reverted, deleted file back, bad file
  gone, exec bit kept, unchanged files hardlink-shared). Restoring auto-takes a
  `pre-restore` point first, so a restore is itself reversible. Surfaced in the
  Control Center → Recuperación. Remaining: the recovery boot env + a
  from-recovery restore UI.
- **Backend shipped** ✅: the shared, unit-tested Python install backend
  (`installer/castalia_installer`) — guided whole-disk layout (§14.3), a
  confirmation-gated engine (§14.5 #1), UUID fstab, GRUB + user in chroot. It
  runs `--dry-run` (prints the plan), is covered by 38 unit tests, and is
  proven on a **real block device** by the loopback smoke
  (`installer/tests/loopback-smoke.sh`). Remaining: the Qt GUI + ncurses
  front-ends, dual-boot detection, and the end-to-end QEMU install-and-boot.
- **Goals:** Qt GUI installer + ncurses fallback (shared backend); dual-boot
  detection; Display Test; **Restore Points** engine + Recovery boot env + Safe
  Mode; Recovery Center MVP.
- **Deliverables:** graphical + text install; recovery initramfs; Restore Points.
- **Non-goals:** migration assistant polish; scheduled backups.
- **Acceptance:** clean install on FLOOR/TARGET; dual-boot preserves Windows;
  a broken update is recoverable via Restore Point in the recovery env.
- **Risks:** partitioning/bootloader on old BIOSes (extensive real-HW testing).
- **Test plan:** install matrix (whole-disk, alongside, manual); forced-fail +
  rollback test; power-loss-during-update simulation.

### Phase 6 — Wine / DOSBox compatibility suite
- **Goals:** Wine Prefix Manager (per-app prefixes, versions, winetricks,
  sandbox toggle), DOSBox-X Launcher, ScummVM front-end, compat-DB + Open With
  routing, Software Center MVP (native + compat apps).
- **Deliverables:** `castalia-compat` metapackage; compat-DB v1; those apps.
- **Non-goals:** 86Box/PCem polish; Boxedwine beyond a spike.
- **Acceptance:** install + run a curated set of Windows apps and DOS games via
  the friendly UI, each showing an honest compat rating; associations route
  correctly.
- **Risks:** Wine-per-app fragility (pin known-good Wine; profile-driven).
- **Test plan:** compat regression suite over the curated app list.

### Phase 7 — Visual polish & sound
- **Goals:** All 5 themes finalized; original wallpapers, boot/login art, icon
  family, **original sound palette**; animation policy; High-Contrast +
  accessibility basics; remaining first-party apps (editors, paint, media,
  image viewer, calculator, screenshot, terminal, help, welcome) to 1.0 scope.
- **Deliverables:** the full polished visual system (§8); complete app set.
- **Non-goals:** feature creep beyond the §9 catalog.
- **Acceptance:** cohesion review passes; 800×600 + 1024×768 pixel checks pass;
  all sounds/art provenance-clean (§3.9).
- **Risks:** polish is a time-sink (timebox; ship "good", iterate).
- **Test plan:** design review; layout screenshot diffs; provenance CI.

### Phase 8 — Hardware certification
- **Goals:** Certify the reference real-hardware matrix (§19): P4 desktop, C2D
  desktop, an early laptop, GMA, and one nouveau + one radeon GPU; finalize the
  quirks table; suspend/resume per-model results.
- **Deliverables:** a published **Hardware Compatibility Guide** (§20) with
  tested/untested/broken per model; quirks shipped.
- **Non-goals:** Legacy (non-SSE2) build (stretch).
- **Acceptance:** each reference machine installs, boots, sounds, networks,
  prints, and runs a Wine app; suspend result documented.
- **Risks:** thin real-HW access (community hardware program, §19/§22).
- **Test plan:** the full real-HW certification checklist per machine.

### Phase 9 — Public alpha
- **Goals:** First public ISO (classic32 + classic64), stable channel skeleton,
  offline Help, feedback path (local bug-report export, no telemetry).
- **Deliverables:** downloadable signed alpha ISOs + docs.
- **Non-goals:** perfection; broad hardware guarantees.
- **Acceptance:** a stranger can download, verify, install, and use it offline
  with the docs; no data-loss bugs open.
- **Risks:** reputation from a rough first impression (label alpha honestly).
- **Test plan:** external-tester script; install/first-boot survey.

### Phase 10 — Beta
- **Goals:** Stabilize from alpha feedback; update pipeline hardened; more
  curated apps; migration assistant to 1.0; performance re-tuned.
- **Deliverables:** beta ISOs; working Stable/Testing channels; upgrade path
  alpha→beta proven.
- **Non-goals:** new subsystems.
- **Acceptance:** in-place updates work + roll back; no data-loss/brick bugs;
  budgets green on FLOOR.
- **Test plan:** upgrade/rollback matrix; longevity soak on real HW.

### Phase 11 — 1.0 release
- **Goals:** Ship Castalia Classic 1.0 (classic32 + classic64) with the full §9
  1.0 app scope, all 5 themes, installer+recovery, compat suite, signed updates,
  and the complete doc set (§20).
- **Deliverables:** 1.0 ISOs + repos + docs + release notes + known-issues.
- **Non-goals (post-1.0):** Legacy non-SSE2 build; 86Box polish; Wayland;
  office suite of our own; advanced accessibility (screen reader) as default.
- **Acceptance:** the §23 "first public release criteria" all met.
- **Test plan:** the entire QA matrix (§19) green on emulators + certified HW.

---

## 19. QA and Hardware Certification

Two tiers of QA: **automated (emulator) gates** that run continuously, and
**real-hardware certification** that gates Stable releases. The FLOOR promise
(P1) is only credible if it is *tested*, so testing is a first-class subsystem
(`/tests`, §17).

### 19.1 Emulator matrix (continuous, in CI)

| Emulator | Role | Configs |
|---|---|---|
| **QEMU** (primary) | Automated boot/install/perf/upgrade/recovery + FLOOR budget gates | FLOOR: 1 vCPU cap, 512 MB, std VGA/`-vga std`, IDE disk, 800×600. TARGET: 2 vCPU, 2 GB, virtio/QXL, 1024×768. TCG (no KVM) to mimic slow CPUs. |
| **VirtualBox** | Manual/scripted install + desktop smoke; a "typical user VM" | 512 MB & 2 GB; SATA + IDE; VBoxVGA |
| **VMware** (Player) | Cross-check install + display/sound | 1 GB; SVGA |

CI publishes each build's boot time, idle RSS, and interaction latencies vs
budget (§16); a regression fails the build.

### 19.2 Real-hardware certification matrix (gates Stable)

| Reference machine | Represents | Must pass |
|---|---|---|
| **P4 desktop, 512 MB–1 GB, GMA (845/865/915)** | FLOOR | boot, install, sound (AC'97/HDA), wired net, USB, print, one Wine app, all budgets |
| **Core 2 Duo desktop, 2 GB, GMA/nouveau** | TARGET | all of the above + compositor-on + media playback |
| **Early laptop (Pentium M / Core Duo), 1 GB** | Mobility | + battery/lid/brightness, touchpad, suspend result documented, Wi-Fi best-effort |
| **NVIDIA 6/7/8-series (nouveau)** | G2 GPU | Xorg + 2D + basic GL; quirks recorded |
| **ATI Radeon 9000/X-series/HD-2000–4000 (radeon)** | G3 GPU | Xorg + 2D + basic GL; quirks recorded |

**Hardware access reality:** a tiny team can't own everything. A **Community
Hardware Program** (§22) lets trusted testers run a **scripted certification
checklist** and submit signed results; the Hardware Compatibility Guide (§20) is
built from these. Emulators cover the automated gates; real HW covers what
emulators can't (real GPUs, suspend, odd BIOSes).

### 19.3 Test categories (per release)

| Category | What it verifies |
|---|---|
| **Boot** | Cold/warm boot to desktop within budget; Safe Mode; Recovery entry |
| **Install** | Whole-disk, alongside (dual-boot preserves Windows), manual, text-fallback |
| **Upgrade** | alpha→beta→1.0 in-place; channel switch; N-1 kernel kept |
| **Recovery** | Forced-fail update + Restore Point rollback; power-loss-during-update; fsck/boot-repair |
| **Suspend** | S3 per model (tested/untested/broken recorded); lid/brightness |
| **Sound** | AC'97 + HDA + USB audio; per-app volume; test chime |
| **Ethernet** | DHCP + static on Realtek/Intel/VIA/nForce NICs |
| **Wi-Fi** | best-effort connect on ath9k/rtl (firmware path) |
| **USB** | 1.1/2.0 mass storage mount, HID, printer, audio |
| **Wine** | curated app list runs at recorded ratings; per-prefix isolation holds |
| **DOSBox/ScummVM** | curated game list launches + runs |
| **File Manager** | full op suite; 1k-dir latency; no UI stalls; USB integration |
| **Control Center** | every panel functional; polkit prompts correct |
| **Low-RAM** | FLOOR idle ≤170 MB; one app leaves ≥250 MB free; no OOM at rest |
| **Disk corruption/recovery** | simulated bad sectors/interrupted write → fsck + recovery succeed, no silent data loss |
| **Regression** | prior fixed bugs stay fixed (each gets a test) |
| **UI layout** | 800×600 + 1024×768 screenshot diffs; no clipped controls |
| **Provenance/legal** | no asset without a ledger row; license check green (§3.9) |

### 19.4 Bug tracker labels

`type:bug` · `type:regression` · `type:enhancement` · `type:legal` ·
`area:shell` · `area:explorer` · `area:control-center` · `area:installer` ·
`area:recovery` · `area:wine` · `area:dosbox` · `area:kernel-hw` ·
`area:audio` · `area:network` · `area:print` · `area:theme` · `area:perf` ·
`area:docs` · `tier:floor` · `tier:target` · `gpu:gma` · `gpu:nouveau` ·
`gpu:radeon` · `sev:data-loss` · `sev:brick` · `sev:crash` · `sev:cosmetic` ·
`hw:certified-machine` · `blocks-release` · `good-first-issue`.

**Release blockers (never ship with these open):** any `sev:data-loss`,
`sev:brick`, `type:legal`, or a FLOOR-budget regression on `tier:floor`.

---

## 20. Documentation Set

Docs are a **shipped product**, authored in the monorepo (`/docs`, Markdown),
and the **offline Help Center** (§9) is generated from the same source — so the
manual is always on the machine, no internet required (P5/P6).

| Document | Audience | Contents | Source |
|---|---|---|---|
| **User Manual** | End users | The desktop, menu, Explorer, Control Center, apps, everyday tasks | `/docs/user/` → Help Center |
| **Install Guide** | New installers | Making a USB/CD, booting, the installer, dual-boot, first boot | `/docs/install/` |
| **Recovery Guide** | Users in trouble | Safe Mode, Recovery env, Restore Points, boot repair, "my update broke it" | `/docs/recovery/` |
| **Hardware Compatibility Guide** | Buyers/testers | Certified machines, GPU/audio/NIC/Wi-Fi status, suspend results, quirks | Generated from §19 cert results |
| **Developer Guide** | Contributors | Repo layout, build, CI, coding standards, how to add an app | `/docs/dev/` |
| **Theming Guide** | Artists/themers | Theme bundle format, icon grid/spec, QSS tokens, contribution rules | `/docs/theming/` |
| **Packaging Guide** | Packagers | Making a Castalia `.deb`, metapackages, the repo, signing, provenance | `/docs/packaging/` |
| **Wine Compatibility Guide** | Power users | Prefixes, versions, winetricks, sandbox, the compat-DB, ratings, honest limits | `/docs/wine/` |
| **Legal Notice** | Everyone | Trademarks, the Microsoft non-affiliation statement, licenses, GPL offer, asset attributions | `/legal/` + `/docs/legal/` |
| **Release Notes template** | Each release | What's new, changed, fixed, known regressions | `/docs/releases/TEMPLATE.md` |
| **Known Issues template** | Each release | Current known bugs + workarounds per area | `/docs/releases/KNOWN_ISSUES.md` |
| **Troubleshooting Guide** | Users | Symptom→fix trees (no boot, no sound, no net, no display, Wine app won't run) | `/docs/troubleshooting/` |
| **Offline Help System** | Users | The above, indexed + searchable, contextual from every app's F1 | Built from all `/docs` at ISO time |

**Doc principles:** plain language, screenshots of **Castalia** (never Windows,
§3), every error message in Log Viewer links to a Help topic, and each doc lists
its "last verified on version". The **Legal Notice** and **provenance ledger**
are release blockers if incomplete (§3.9, §19).

---

## 21. Branding and Lore

A cohesive, warm, professional identity — enough personality to be memorable,
never gimmicky.

### 21.1 Names & scheme

- **Product family:** **Castalia Classic** (publisher: **Tombatossals
  Softworks**). Internal project: **Castalia OS**.
- **Version naming:** `MAJOR.MINOR` + an original **coastal/castle** code-name
  per release, alphabetical, Mediterranean-flavored but not tied to any real
  trademark. Example progression: **1.0 "Peñíscola"**, 1.1 "Oropesa",
  1.2 "Benicàssim"… (evocative of the Castellón coast — see 21.5 — while staying
  generic place-inspired, checked for trademark collisions before use). **Never**
  a Windows code-name; never a Microsoft-associated word.
- **Component names** are plain and original (§3.8): Castalia Explorer, Castalia
  Menu, Control Center, Hardware/Network Center, Software/Update/Recovery Center,
  Restore Points, Theme Manager, Wine Prefix Manager.

### 21.2 Wordmark, icon philosophy, boot & login

- **Wordmark/logo (v0.2, the "C monogram keep"):** an original mark — a
  **stylised letter C drawn as a thick azure ring, open to the right, with the
  Castalia keep standing inside it on green hills**: crenellated main tower,
  two conical-roofed turrets, warm lit windows, an arched door. Flat colour
  with a hint of depth, geometry only (no fonts, no rasters), so one 48-grid
  file serves the tray, the boot splash and a 1024 px press render. It reads
  as a blue-and-green badge at 16 px and as the full monogram from 24 px up.
  The publisher lockup ("Tombatossals Softworks") appears in About and the
  boot credit. Source of truth: `branding/logo/castalia-mark.svg`; the native
  painter `castalia::drawMark()` mirrors it stop for stop.
- **Icon philosophy (§8.4):** universal metaphors, original art, one grid, one
  light source, warm-stone + azure accents; friendly but crisp; legible at
  16 px. "Approachable, not childish; classic, not skeuomorphic-heavy."
- **Boot screen:** quiet wordmark + slim progress over a stone/azure field;
  framebuffer-safe at 640×480; text fallback (§8.6).
- **Login (greeter):** a coastal wallpaper, original user avatars, clock,
  accessibility button; calm and welcoming.

### 21.3 Wallpaper concepts (original)

*"Sandstone Coast", "Azure Bay", "Olive Terraces", "Silver Harbor",* and the
hero *"Castalia Keep"* (a stylized castle on a headland at golden hour) — plus
tasteful solid/gradient options for the FLOOR tier. All original, provenance-
clean, shipped at 1024×768 and 1280×1024, size-budgeted (§8.6).

### 21.4 Sound palette (original)

A short, warm **motif** unifies the system sounds — a two- or three-note phrase
in a Mediterranean-tinged mode — rendered as: *startup* (welcoming, ~1.5 s),
*shutdown* (a gentle resolution of the motif), *notify* (soft), *error* (clear
but not harsh), *device connect/disconnect* (a rising/falling pair),
*empty-trash* (a light sweep). All original compositions, ≤~2 s, ogg, honoring
the global mute (§8.6). Recognizably *Castalia*, never an XP pastiche.

### 21.5 "About Castalia" & the lore (subtle)

- The **About Castalia** panel credits **Tombatossals Softworks**, shows the
  version + code-name, the licenses, and the Microsoft non-affiliation statement
  (§3.7).
- **Lore, kept light:** *Castalia* evokes both the mythic spring of the Muses
  (creativity, clarity) and a Mediterranean coastal-castle feeling.
  **Tombatossals** is a nod to the legendary giant of Castellón folklore — a
  quiet Valencian/Castellón wink for those who know, never explained
  gimmickily in the UI. The Mediterranean/castle inspiration lives in the
  *palette, wallpapers, and code-names* — a coherent mood, not a costume. No
  mascot cartoons, no forced theme; the nostalgia is for *good computing*, and
  the place-feeling is the seasoning, not the meal.

---

## 22. Risks and Brutal Reality

No optimism here. This is where projects like this die, and how to not die.

### 22.1 What is genuinely easy

- Getting a Debian-based system to boot on P4/C2D hardware. (Solved problem;
  antiX/MX prove it.)
- Theming a desktop to *look* period-appropriate. (A weekend gets you 60%.)
- Running Wine/DOSBox/ScummVM at all. (The upstreams do the hard part.)
- Bundling FOSS apps. (Repackaging is mechanical.)

### 22.2 What is hard (but doable by a small team)

- **A genuinely cohesive, original shell + Control Center** that feels like a
  product, not a theme. This is the real work and the real differentiator — it's
  months of Qt/C++, not a weekend.
- **The installer + recovery + Restore Points** working reliably on flaky old
  disks and BIOSes. Data-loss bugs here are fatal to trust; this deserves
  disproportionate testing.
- **Honest hardware coverage** across real GPUs/audio/suspend. Emulators lie;
  you need real machines (see the Community Hardware Program).
- **The update pipeline that never bricks.** Release engineering is a discipline,
  not a script.

### 22.3 What is effectively impossible for one developer

- **A from-scratch kernel or a ReactOS-class Win32 reimplementation.** Decades,
  teams, and still not a daily driver. **Do not attempt** (this is why we use
  Linux + Wine, §5).
- **Perfect Windows app/game compatibility**, especially modern DirectX and
  kernel anti-cheat. Physically impossible on this hardware and/or actively
  blocked by vendors (§11.6).
- **Owning every subsystem.** You cannot maintain your own browser engine, your
  own office suite, your own codecs, your own driver stack. **Curate upstream;
  own the experience.**
- **Supporting literally all old hardware.** The long tail is infinite. Pick a
  matrix (§4, §19) and be honest about the rest.

### 22.4 What must be avoided

- **Cloning the whole OS.** The single biggest trap. It never ships. We emulate
  *ergonomics*, we build *a curated Linux with a custom shell*.
- **Copying any Microsoft asset "just for now".** One infringing icon/sound/
  wallpaper is an existential legal risk (§3). The provenance CI gate exists to
  make this impossible to do by accident.
- **Scope creep into "own everything".** Every subsystem you build from scratch
  is a subsystem you must maintain forever with a tiny team.
- **Systemd-style complexity / opaque binary state.** It fights P5 (repairable)
  and P2 (fast). We chose runit + plain logs deliberately.
- **Perfectionism before shipping.** A rough-but-real alpha beats a perfect plan.
- **Depending on a heavy runtime (Python/JS) for the resident desktop.** It
  breaks the FLOOR budget.

### 22.5 Where projects like this actually die

1. **The "boil the ocean" death:** trying to reimplement Windows/kernel/browser.
   *Antidote:* §5 decision + §22.3 — build on Linux, curate upstream.
2. **The "eternal 0.1" death:** endless refactoring, no releases. *Antidote:*
   MVP-first roadmap (§18); every phase ships something bootable.
3. **The "solo burnout" death:** one person, infinite scope, no feedback.
   *Antidote:* ruthless scope (§9.5 "what we don't build"), public alpha early
   (Phase 9), community hardware/testing program.
4. **The "legal landmine" death:** an infringing asset surfaces post-launch.
   *Antidote:* the provenance ledger + CI gate + attorney review (§3).
5. **The "it bricks grandma's PC" death:** a bad update destroys data and trust
   evaporates. *Antidote:* the anti-brick update design (§13.3) + Restore Points
   + disproportionate installer/recovery testing (§19).
6. **The "nobody can tell it from a theme" death:** it looks like yet another
   XFCE reskin. *Antidote:* the custom shell + Control Center + cohesive original
   design system (§7, §8) is the product; theming is the surface.

### 22.6 Lessons from ReactOS and the alternative-OS graveyard

- **ReactOS:** 25+ years, real talent, clean-room rigor — still alpha, still not
  a daily driver, because reimplementing a moving, closed target is
  Sisyphean. **Lesson:** don't reimplement Windows; *run* Windows apps via Wine
  on a mature kernel. Compatibility is cheaper as *translation on Linux* than as
  *reimplementation of NT*.
- **Haiku:** beautiful, cohesive, spiritually right — but a tiny ecosystem and
  driver base after decades, because it forwent the Linux driver/app commons.
  **Lesson:** stand on the Linux hardware/software commons; spend your budget on
  UX and cohesion, not on re-growing an ecosystem.
- **Countless "XP-like Linux" reskins:** they look the part for a screenshot,
  then stall because there's no *product* underneath — no cohesive shell, no
  control surface, no recovery story, no identity. **Lesson:** the shell +
  control center + recovery + honest compat + original identity **are** the
  product. That's exactly what this bible commits to building.

### 22.7 How to keep scope under control & actually ship

- **The FLOOR is sacred; the app catalog (§9) is closed.** New app ideas go to a
  post-1.0 backlog, not into v1.
- **Every phase ends bootable/demoable (§18).** No infrastructure-only phases.
- **Curate, don't build,** anything you can't maintain forever (browser, office,
  codecs, VM stack).
- **Ship the alpha early (Phase 9)** and let real users/hardware redirect the
  effort.
- **"Would this make the P4/512 MB experience worse?" is a veto question** on
  every feature (P1).
- **Progressive originality:** ship reskinned-upstream where fine (§9 [Wrap]/
  [Curate]), replace with first-party only when it's genuinely better — never
  rebuild for pride.

### 22.8 Why "great curated Linux + custom shell" is the viable path (summary)

Because it gives us, on day one: a proven kernel, the best old-hardware/driver
coverage available, a security-update stream, the entire Debian archive to
curate from, and Wine/DOSBox/ScummVM for compatibility — so **100% of our scarce
engineering budget goes to the things users actually feel**: the shell, the
control surface, the visual identity, the recovery story, and honest
compatibility. Cloning the OS spends that same budget re-growing plumbing that
already exists and better — and never ships. This path *ships*, and it ships
something real.

---

## 23. Final Recommendation

A concrete, committed plan. No hedging.

### 23.1 The stack (decided)

| Decision | Choice |
|---|---|
| **Base OS** | **Debian stable, de-systemd'd (Devuan/antiX lineage)**; i386 (SSE2) + amd64; signed **Castalia overlay repo** on top. Fallback on file: **Void Linux (glibc, runit)**. |
| **Init / supervision** | **runit** (SysVinit fallback); **eudev** + **elogind** |
| **Display** | **Xorg** (modesetting/intel/nouveau/radeon + Mesa); **picom** compositor optional, off on FLOOR. Wayland deferred. |
| **Audio / Net / Print / Mount** | ALSA+PipeWire(-lite) / NetworkManager / CUPS+Gutenprint / udisks2+polkit; Samba client for Network Places |
| **Desktop stack** | **Openbox** WM + **Castalia shell** (panel/desktop/menu/tray/Explorer) |
| **Toolkit / languages** | **Qt 5.15 LTS + C++17** for shell & all apps (shared `libcastalia-ui`, QSS theming); **C** for system glue; **Rust** for a few root-adjacent daemons (optional); **POSIX shell** for boot/build; **Python** for installer backend + tooling only; **INI/TOML/QSS** for config/theme; **CMake** primary build |
| **Filesystem / recovery** | **ext4** default (btrfs opt-in); **Restore Points** (rsync/hardlink, btrfs when available); independent Recovery env + Safe Mode |
| **Compatibility** | Native-first; **Wine** per-app prefixes (version-managed, sandboxable) + compat-DB with honest ratings; **DOSBox-X**, **ScummVM**; 86Box/PCem opt-in |
| **Security** | Non-root desktop, nftables firewall on, signed updates, **no telemetry**, no remote access by default, autorun off, honest Wine/browser caveats |
| **Editions** | **Castalia Classic 32 (SSE2)** + **Castalia Classic 64**; **Legacy 32 (non-SSE2)** as a post-1.0 stretch only |

### 23.2 MVP target (what the first real thing is)

A **bootable, installable Castalia Classic** that: boots to a **cohesive
Castalia-themed desktop** (panel + menu + tray + desktop) on **Openbox** within
the FLOOR budget; runs **Castalia Explorer** (browse/copy/USB) and a **Control
Center** with the essential panels (Display, Sound, Network, Users, Theme,
Power); installs via a **safe GUI installer (text fallback)** with **dual-boot
protection**, **Restore Points**, and a **Recovery** entry — all on real P4 and
Core 2 Duo hardware. *(= Phases 1–5.)*

### 23.3 1.0 target

Everything in the MVP **plus**: the full §9 app catalog at 1.0 scope; all **5
themes** + original wallpapers/sounds/boot/login art; the **Wine/DOSBox/ScummVM
compatibility suite** with the compat-DB and Software Center; **signed updates**
with channels + anti-brick pipeline; **certified** on the real-hardware matrix
(§19); and the **complete offline documentation set** (§20). Shipped as
**classic32 + classic64**, honestly labeled, legally clean.

### 23.4 First 30 days

1. Stand up the **monorepo + CI + ISO build skeleton** (§17).
2. **Phase 0 spikes:** confirm **Debian-de-systemd + runit** boots on a real P4
   and a real C2D; measure a **Qt5 panel/menu PoC** against the FLOOR budget;
   decide the open **[SPIKE]**s (PipeWire-on-floor, NM-vs-connman, rsync-vs-btrfs
   restore, Boxedwine relevance).
3. Draft **`libcastalia-ui`** skeleton + the **design tokens** in
   `/branding/spec` (palette, grid, control geometry).
4. Write the **provenance ledger + CI legal gate** (§3.9) *before* any asset
   lands — the legal discipline is scaffolding, installed first.
5. Deliverable at day 30: a throwaway ISO that boots to Openbox + a Qt panel PoC
   on real hardware, plus a written spike/decision report.

### 23.5 First 90 days

- Complete **Phase 1 (base ISO)** and **Phase 2 (themed desktop MVP)**: a signed
  `castalia-min` + `castalia-desktop` ISO that boots to a **cohesive Castalia
  desktop** (Classic theme end-to-end) within budget on FLOOR + TARGET, with the
  **Theme Manager MVP** switching bundled themes.
- Begin **Phase 3 (Control Center MVP)**: Display + Sound + Network + Users
  panels functional.
- CI enforcing **FLOOR budget gates** on every nightly ISO.

### 23.6 First 6 months

- Land **Phases 3–5**: Control Center MVP, **Castalia Explorer MVP**, and the
  **installer + recovery + Restore Points** — i.e. **reach the MVP target**
  (§23.2): a safe, installable, recoverable, cohesive Castalia you can put on a
  real P4/C2D and use for files, settings, and basic tasks.
- Start **Phase 6 (Wine/DOSBox suite)** groundwork (Wine Prefix Manager +
  compat-DB skeleton).
- Begin the **Community Hardware Program** to seed the certification matrix.

### 23.7 First public release criteria (the bar for a public alpha → 1.0)

A build may go **public alpha** (Phase 9) when, and only when:

1. It **installs and boots** on the FLOOR (P4/512 MB) and TARGET (C2D/2 GB)
   reference machines, GUI and text installers both working.
2. **No open `sev:data-loss`, `sev:brick`, or `type:legal` bugs**; the
   **provenance ledger + legal notice are complete** (§3.9, §19.4).
3. **Dual-boot installs preserve an existing Windows partition**, verified.
4. **Restore Points + Recovery env** demonstrably recover a deliberately broken
   update (and survive a simulated power-loss-during-update).
5. **FLOOR performance budgets are green** in CI (boot, idle ≤170 MB, menu
   ≤150 ms, Explorer ≤600 ms).
6. **Wired networking, audio, and USB storage work** out of the box on the
   reference machines; **a curated Wine app and a DOS game run** via the
   friendly UI with honest compat ratings.
7. The **offline Help Center** answers the core "how do I…" and "it broke, now
   what" questions, and every shipped asset is **original or licensed** (§3).

Meeting all seven on **certified real hardware** (not just emulators), across
the §19 matrix, with the full §9 1.0 app scope and all 5 themes, is the bar for
**1.0**.

---

### Closing statement

Castalia Classic wins by being **honest, cohesive, fast, recoverable, and
original** — a *designed product* for hardware the industry abandoned, built on
a proven Linux base with a custom Qt shell and a managed compatibility layer,
**not** a clone and **not** a theme pack. Emulate the comfort of the era;
originate everything protected; ship an MVP that boots on a real Pentium 4; and
let real users on real old machines pull the rest of the roadmap forward.

*Familiar, but ours. Retro, but alive. Beautiful, but light. Built by
Tombatossals Softworks.*

---

*End of Project Bible v0.9. This document is the source of truth; changes go
through pull request against `/docs/PROJECT_BIBLE.md` with a rationale, and any
change to a `[DECISION]` requires an explicit note of what superseded it and
why.*
