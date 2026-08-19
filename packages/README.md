# packages/ — Debian packaging & metapackages

`debian/` packaging for every first-party component, the metapackages that
define editions/roles, any custom kernel config, and the compatibility-layer
integration packages. See
[`docs/PROJECT_BIBLE.md` §13](../docs/PROJECT_BIBLE.md#13-package-and-update-model).

## Contents

| Path | Status | Purpose |
|------|--------|---------|
| `mkdeb.sh` | ✅ | Builds the **`castalia-desktop`** `.deb` from a compiled shell tree: all shell + app binaries into `/opt/castalia/bin` (with `/usr/bin` links), the runtime asset tree into `/usr/share/castalia`, the greeter session entry, the installer/recovery Python backends and their console launchers, per-theme Openbox decorations, and `/etc/castalia/theme.conf` as a conffile. The app list is `tests/apps.manifest` — the same list the QA suites gate — so nothing ships untested. CI builds it, installs it, runs it, and resolves it from the generated apt repo on every push |
| `metapackages/` | planned | `castalia-desktop`, `castalia-compat`, `castalia-min` (+ edition roles) |
| `kernel/` | planned | Custom LTS kernel config(s) if needed: i686+SSE2+PAE and x86-64 (§6.3) |
| `compat/` | planned | Wine/DOSBox-X/ScummVM integration packages + the compatibility DB (§11) |
| `debian/` (per component) | planned | Per-component packaging metadata once the fat package is split |

```sh
# build the shell, package it, and assemble the apt overlay repo
make -C build shell deb repo
sudo apt install ./build/out/deb/castalia-desktop_*.deb
```

## Repos & channels (§13.1–13.2)

- Repos: **Base** (Debian snapshot), **Castalia Core**, **Castalia Community**,
  **Castalia Non-free-firmware** (optional, legal-gated, opt-in).
- Channels: **stable** / **testing** / **nightly**. Everything signed.

## Anti-brick rule (§13.3)

System updates auto-create a Restore Point first, keep the N-1 kernel, update
the bootloader last, refuse updates that don't fit the disk, and never
auto-reboot.
