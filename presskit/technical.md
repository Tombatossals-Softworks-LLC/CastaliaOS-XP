# Castalia OS — Technical details

What Castalia OS is made of, and the reasoning behind the choices. For the
full 23-section design document (the "Project Bible"), contact
hello@tombatossalssoftworks.com.

## The stack at a glance

| Layer | Choice | Why |
|---|---|---|
| **Base OS** | Debian stable, de-systemd'd (Devuan/antiX lineage) | Huge package base, long support, no systemd weight on the FLOOR tier |
| **Init / services** | runit (SysVinit fallback) · eudev · elogind | Tiny, fast, supervises and restarts the shell in ~1 s |
| **Display** | Xorg (Wayland deferred) · picom optional, off on low-end | Rock-solid on old GPUs; no compositor required |
| **Window manager** | Openbox with generated Castalia decorations | Light, scriptable, EWMH-correct |
| **Toolkit** | Qt 5.15 LTS + C++17 | One toolkit for shell and every app; native speed; long LTS |
| **Shell language** | C++17 (apps) · POSIX sh (session/supervision) | Fast where it matters; auditable where it must be |
| **Backends** | Python 3 (installer, Restore Points) | Testable, readable, unit-tested logic away from the UI |
| **Compatibility** | Wine (per-app prefixes) · DOSBox-X · ScummVM | Native-first, then a managed, sandboxed fallback |
| **Packaging** | `.deb` + an apt overlay repo; live/install ISOs | Standard, inspectable, resolvable |
| **Architectures** | amd64 and i386 (SSE2) | Covers the whole late-P4-onward field |

## Architecture

**One desktop, many processes.** The session manager (a readable POSIX-sh
supervisor) starts the window manager, the desktop plane and the panel, and
*supervises* them: a crash in any one piece never blacks out the desktop — it
restarts in about a second. Every application runs as its own process.

**One theme system, everywhere.** A single `theme.conf` (a small, commented
TOML) is the source of truth for a theme. A generator turns it into a Qt
stylesheet and an Openbox decoration theme; the same tokens drive the icons,
the greeter and the sounds. Change the theme in the Control Center and the
whole live desktop re-skins — "one switch changes everything." A shared C++
library, `libcastalia-ui`, gives every app the same token access, the native
brand mark and the common look.

**Design rules enforced as code.** A CI linter reads every theme and fails the
build unless: body text meets **WCAG 4.5:1** contrast against its surface
(**7:1** for the High Contrast theme); selection and titlebar text clear their
minima; and each titlebar gradient's two stops differ by **≤ 0.12 relative
luminance** so gradients don't band on 16-bit-era display modes. Metrics live
on a 4-px grid. No theme can ship broken.

**Performance as a budget, not a hope.** The FLOOR is a Pentium 4 with 512 MB
of RAM at 800×600. The shell honours it: the taskbar is **event-driven** —
it subscribes to the window manager's X `PropertyNotify` events over the xcb
file descriptor and coalesces bursts, instead of polling every second — and
the panel clock wakes once a minute rather than 60 times. Animations are all
≤ 200 ms, leave no timer running afterwards, and are skipped entirely under
`CASTALIA_REDUCE_MOTION=1` or on head-less/deterministic renders.

**Compatibility, sandboxed.** Native-first is the rule. When there's no native
answer, the compatibility manager runs the program under Wine in a **per-app
prefix**, so one Windows program's mess can't touch another's. DOSBox-X and
ScummVM integration are on the roadmap.

**Recovery, by design.** A unit-tested **Restore Points** engine snapshots the
system; the **Update Center** takes a Restore Point automatically *before*
applying updates; a **Recovery Center** GUI and an independent recovery
environment complete the safety net. The installer is equally careful: it
shows the exact plan before it does anything and refuses every destructive
step until the operator types the target disk to confirm — and it installs
fully offline, with no online account.

## Provenance & legal engineering

Castalia OS contains **no** Microsoft (or other third-party) code, branding,
icons, sounds or wallpapers. Every shipped binary asset — every icon, sound,
font, wallpaper, cursor — must have a row in a provenance ledger recording its
source, author and licence, and a CI gate **fails the build** if any asset
lands without one. Nothing derived from, traced from or sampled from a
proprietary asset can enter the tree. The project source is MIT-licensed;
bundled third-party components keep their own licences.

## How it's verified

Four pipelines keep the project honest:

- **Per-commit CI:** lint; unit suites (the QA tooling, the installer backend,
  the Restore Points engine, the two games' rules); the design & legal gates
  (theme linter, provenance, export smokes); **every application rendered
  offscreen in every one of the seven themes**; a **live end-to-end** run of
  the entire app suite under a real X server + Openbox (every app must map,
  live and exit cleanly; the session must boot, supervise and log out); and a
  **distribution gate** that builds the `.deb`, installs it, runs it and
  apt-resolves it from a generated overlay repo.
- **Nightly:** a real live ISO build booted and asserted under QEMU.
- **On-demand:** a full desktop-ISO proof.
- **Release:** a tag-driven pipeline that ships QEMU-boot-verified ISOs +
  packages + checksums to a human-approved draft.

Every headline claim in this kit is backed by real QEMU / X / disk captures
kept under version control.

## Hardware envelope

- **FLOOR:** Pentium 4 (SSE2), 512 MB RAM, GMA-class GPU, 800×600, 8 GB disk.
- **TARGET:** Core 2 Duo, 2 GB RAM, 1024×768+, 16 GB+ disk.
- Ethernet is first-class; Wi-Fi is best-effort; PS/2 and USB 1.1/2.0; CUPS
  and Samba for printing and sharing.
