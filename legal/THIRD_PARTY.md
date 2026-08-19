# Third-Party Components

Castalia Classic stands on the Linux commons. This file tracks every
third-party component we ship or build on, with its SPDX license identifier, so
that license-compatibility can be checked in CI and GPL/attribution obligations
can be satisfied by the packaging pipeline.

See [`docs/PROJECT_BIBLE.md` §3.9](../docs/PROJECT_BIBLE.md#3-legal-and-branding-strategy)
and [§13](../docs/PROJECT_BIBLE.md#13-package-and-update-model).

> **Rule:** a component may not ship unless it appears here with a license that
> is compatible with our distribution terms. Pinned versions live in
> `third_party/` (submodules/tarballs + patches).

## Core platform (from the Debian base)

| Component | Role | SPDX license (typical) |
|-----------|------|------------------------|
| Linux kernel | Kernel | GPL-2.0-only (with syscall exception) |
| glibc | C library | LGPL-2.1-or-later |
| Xorg server + drivers | Display | MIT / X11 |
| Mesa | GL drivers | MIT |
| runit | init/supervision | BSD-3-Clause / public-domain-ish |
| eudev | device manager | GPL-2.0-or-later |
| elogind | seat/session | LGPL-2.1-or-later / GPL-2.0-or-later |
| dpkg / apt | packaging | GPL-2.0-or-later |
| GRUB2 | bootloader | GPL-3.0-or-later |
| isolinux/syslinux | live boot | GPL-2.0-or-later |

## Desktop / toolkit

| Component | Role | SPDX license (typical) |
|-----------|------|------------------------|
| Qt 5.15 LTS | UI toolkit | LGPL-3.0-only / GPL |
| Openbox | window manager | GPL-2.0-or-later |
| picom | compositor (optional) | MIT / MPL-2.0 |
| LightDM | login greeter | GPL-3.0-or-later |
| NetworkManager | networking | GPL-2.0-or-later |
| PipeWire / ALSA | audio | MIT / LGPL |
| CUPS + Gutenprint | printing | Apache-2.0 / GPL |
| udisks2 + polkit | storage/authz | GPL-2.0-or-later / LGPL |

## Compatibility layer

| Component | Role | SPDX license (typical) |
|-----------|------|------------------------|
| Wine | Win32 compatibility | LGPL-2.1-or-later |
| winetricks | Wine helper | LGPL-2.1-or-later |
| DOSBox-X | DOS/early-Win emulation | GPL-2.0-or-later |
| ScummVM | adventure-game engines | GPL-3.0-or-later |
| Boxedwine (eval) | sandboxed legacy Wine | GPL-2.0-or-later |
| 86Box / PCem (optional) | full PC emulation | GPL-2.0-or-later |

## Fonts / art (see ASSET_PROVENANCE.csv for the authoritative per-asset ledger)

| Component | Role | SPDX license |
|-----------|------|--------------|
| DejaVu / Liberation / Noto (or an original face) | UI/mono fonts | Bitstream-Vera / OFL-1.1 |

*Entries are illustrative of the intended stack and MUST be reconciled against
the actual pinned versions in `third_party/` before any public release. Exact
licenses can vary by version — always confirm from the shipped source.*
