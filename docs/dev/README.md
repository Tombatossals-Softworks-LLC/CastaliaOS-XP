# Developer Guide

*Last verified on version 0.2.0.*

Everything you need to build Castalia, run what CI runs, and add something to
it. The authority on *why* anything is the way it is remains
[`docs/PROJECT_BIBLE.md`](../PROJECT_BIBLE.md); this is the how.

## Repository layout

| Directory | What lives there |
|---|---|
| `shell/` | the Qt5 desktop shell: `panel/`, `desktop/`, `explorer/`, `libcastalia-ui/`, `session/` |
| `apps/` | every first-party application, one directory each, all linking `libcastalia-ui` |
| `installer/` | the Python install backend, the text installer, the Qt wizard |
| `recovery/` | Restore Points, and the recovery boot environment (`boot/`) |
| `hwprobe/` | the hardware probe and its quirks table |
| `tools/` | build-time generators (themes, sounds, icons, i18n) and the QA gates (`castalia_qa/`) |
| `themes/` | the seven theme bundles, as source |
| `build/` | `mkiso.sh`, `mkrepo.sh`, edition profiles, chroot hooks |
| `packages/` | `mkdeb.sh` — the `.deb` |
| `iso/` | boot menus and GRUB generators |
| `services/` | runit service definitions |
| `tests/` | `run.sh` (the one command), plus the e2e/perf/qemu harnesses |
| `docs/` | this documentation set (§20), which ships |
| `legal/` | provenance ledger, third-party notices, the non-affiliation statement |

## Build it

```sh
# Python-only gates — no toolchain needed beyond python3
sh tests/run.sh quick

# the shell and every app
cmake -S shell -B build/out/shell-build -DCMAKE_BUILD_TYPE=Release
cmake --build build/out/shell-build -j"$(nproc)"

# everything the per-commit CI runs
sh tests/run.sh full
```

Build dependencies on Debian/Ubuntu:

```sh
sudo apt-get install --no-install-recommends \
    qtbase5-dev libqt5svg5-dev libxcb1-dev cmake g++ \
    qttools5-dev-tools fonts-dejavu-core python3 ruff
```

## The test tiers

`tests/run.sh` is the single entry point, and CI runs exactly it. Tiers run in
the order given and stop at the first failure.

| Tier | What it does | Needs |
|---|---|---|
| `lint` | ruff over `tools/ installer/ recovery/ hwprobe/` | ruff |
| `unit` | every Python unit suite | python3 |
| `gates` | theme + provenance linters, mkiso/mkdeb/mkrepo dry runs, installer plans in all three modes, hwprobe, the Restore Points smoke | python3 |
| `build` | compile the shell, run the head-less self-tests | Qt5, cmake |
| `render` | offscreen-render every app in every theme | `build` |
| `e2e` | the live suite under Xvfb + Openbox | Xvfb, openbox, x11-utils |
| `perf` | the §16 budgets, measured off real binaries | `build`, Xvfb |
| `iso` | a real ISO build + QEMU boot | root, debootstrap, qemu |

Presets: `quick` = lint unit gates; `full` = quick + build render e2e perf.

## Coding standards

**C++ / Qt5.** C++17. Every app links `libcastalia-ui` and takes its colours
from the theme, never from a literal. Every app supports `--theme`, `--repo`,
`--selftest` and `--screenshot`, which is what makes the render tier possible.
User-visible strings go through `tr()` (§7.13).

**Python.** 3.11+, `from __future__ import annotations`, ruff-clean, no
third-party dependencies at runtime — the installer and the recovery tools run
on a live image where pip does not exist. Logic is split into *pure* functions
(planning, parsing, deciding) and a thin side-effect boundary (a `Runner`
class), so the interesting half is testable without a disk.

**Shell.** POSIX `sh`, not bash. `sh -n` clean, shellcheck clean. Anything that
runs during boot must be fail-open: a script that can exit non-zero in the boot
path is a machine that does not start.

## Comments

Comment the *why*, especially where the obvious thing is wrong. Several files
here carry a paragraph explaining a decision that looks eccentric until you
know what it prevents — `parted` not being usable to shrink a partition, the
recovery console running at `init-premount` rather than `init-bottom`, the
filesystem being resized before the partition. Those paragraphs are the point;
please write more of them rather than fewer.

## Adding an application

1. `apps/<name>/` with `CMakeLists.txt` and `src/main.cpp`; copy the shape of
   an existing small one (`apps/clock/`).
2. Add it to `apps/CMakeLists.txt`.
3. Add a row to `shell/panel/src/AppRoster.cpp` — the *one* table behind both
   the launch menu and Alt+Tab.
4. Add a 48px SVG at `themes/icons/48/<icon>.svg`. `test_app_roster.py` fails
   if the icon does not exist, because Qt returns a null icon silently and a
   blank square raises no error anywhere.
5. Add it to `tests/apps.manifest` so the render and e2e tiers exercise it.
6. Ship it: `packages/mkdeb.sh` and `build/hooks/desktop-amd64.sh`.

## Adding a QA gate

Gates live in `tools/castalia_qa/`, are pure functions of files on disk, and
have unit tests in `tools/tests/`. A gate that can pass while measuring
nothing is worse than no gate — see the tests in `test_perf.py` under
`DoesNotLieTest` for the shape that guards against it.

## CI

| Workflow | When | What |
|---|---|---|
| `ci.yml` | every push and PR | the full per-commit surface, plus a real ISO build and QEMU boot for both architectures, the loopback installs, and the dual-boot smokes |
| `nightly.yml` | 03:17 UTC | the heavy ISOs (desktop, classic32), the install-and-boot exercise, a nightly `.deb` + repo |
| `release.yml` | on a `v*` tag | everything, then a **draft** GitHub Release |

All apt installs go through `ci/apt-install.sh`, which puts a hard wall-clock
limit on apt and switches mirrors on the first failure. Do not call `apt-get`
directly in a workflow.

## Releasing

See [`docs/RELEASING.md`](../RELEASING.md).
