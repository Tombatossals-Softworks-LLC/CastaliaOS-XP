# Packaging Guide

*Last verified on version 0.2.0.*

How Castalia becomes a `.deb`, a repository, and an ISO.

## The `.deb`

```sh
PYTHONPATH=tools python3 tools/theme_export.py     # generate the themes
python3 tools/i18n_build.py release                # compile the .qm files
cmake -S shell -B build/out/shell-build -DCMAKE_BUILD_TYPE=Release
cmake --build build/out/shell-build -j"$(nproc)"
sh packages/mkdeb.sh                               # or --dry-run to see the plan
```

`--dry-run` prints exactly what would be staged and where, and touches
nothing. That plan is what the `gates` tier runs on every commit, so a
packaging mistake surfaces before a release rather than during one.

### Where things land

| Path | What |
|---|---|
| `/opt/castalia/bin/` | every binary, with a symlink from `/usr/bin` |
| `/usr/share/castalia/` | themes, icons, i18n, boot assets, the Python backends |
| `/usr/share/castalia/grub/` | the gfxmenu theme and the Safe Mode + recovery generators |
| `/usr/share/castalia/hwprobe/` | the probe and its quirks table |
| `/etc/castalia/theme.conf` | the default theme — a **conffile**, so an admin edit survives an upgrade |
| `/etc/initramfs-tools/` | the recovery hook and its init-premount script |

`/opt` plus `/usr/bin` symlinks rather than straight into `/usr/bin`: it keeps
the whole desktop in one removable tree, and it keeps us out of paths other
Debian packages own.

### A trap worth knowing

Do **not** ship a file at a path another package owns. Castalia's Openbox
configuration lives at `/usr/share/castalia/openbox/rc.xml` and is passed to
Openbox with `--config-file`, precisely because shipping
`/etc/xdg/openbox/rc.xml` makes dpkg refuse to unpack the `openbox` package.

## The repository

```sh
sh build/mkrepo.sh
```

Produces a `dists/` tree that `apt` can consume. Signed when
`CASTALIA_SIGNING_KEY` is set in the environment; unsigned it is a
`[trusted=yes]` QA repo. **Keys never live in the repository** (§17.3).

## ISOs

```sh
sudo sh build/mkiso.sh --edition live-desktop-amd64
sh build/mkiso.sh --edition classic32 --dry-run     # plan only, no root
```

Editions are `build/profiles/*.conf`, sh-sourceable, six keys:

| Key | Meaning |
|---|---|
| `LABEL` | what the boot menu says |
| `ARCH` | `amd64` or `i386` |
| `SUITE` | the Debian suite to bootstrap |
| `MIRROR` | where from |
| `PACKAGES` | the edition's package set |
| `COMPRESSION` | squashfs compression |

Optional: `HOOK` (a chroot script that builds the shell **against the
target's** Qt and glibc — the correct-ABI way), `SRC_DIRS`, `INSTALLER=yes`.

The pipeline is seven stages: deps, bootstrap, configure, packages, hook,
assets, squashfs, iso. Each goes through `run()`, so `--dry-run` prints the
plan and validates the profile without root.

### The hook checks its own work

A chroot hook can exit 0 and still leave an image without the thing the
edition is named after. `mkiso.sh` therefore checks for the binaries
afterwards and refuses to build the ISO if they are missing. That is not
hypothetical: `castalia-live-desktop-amd64 0.1.1` shipped with no
`/opt/castalia` at all and a green build, because a `;` where a `&&` belonged
threw away the chroot's exit status.

## Versions (§17.5)

The repo-root `VERSION` file is the single source. Nightlies append
`~nightlyYYYYMMDD`, which sorts *before* the release by Debian's rules, so a
nightly never shadows the release it precedes.

## Provenance (§3.9)

`castalia_qa.provenance` fails the build for any shipped asset with no entry
in `legal/ASSET_PROVENANCE.csv`. It is a release blocker, not a warning.
