# build/ — Build orchestration

Top-level build entry point: turns first-party sources + the pinned Debian base
into signed `.deb` packages, a signed apt overlay repo, and live/install ISOs
per edition. See [`docs/PROJECT_BIBLE.md` §17.2](../docs/PROJECT_BIBLE.md#17-build-system-and-repository-structure).

## Contents

| Path | Status | Purpose |
|------|--------|---------|
| `Makefile` | ✅ | `make -C build iso EDITION=classic64`, `dry-run`, `themes`, `sounds`, `shell`, `deb`, `repo`, `test`, `test-all`, `e2e` |
| `mkiso.sh` | ✅ | 7-stage pipeline (deps → bootstrap → configure → packages → assets → squashfs → hybrid ISO); `--dry-run` validates profiles and prints the exact plan (CI-exercised); a real run needs root + debootstrap/mksquashfs/xorriso. The `live-amd64` and `live-desktop-amd64` editions build + QEMU-boot in CI |
| `profiles/` | ✅ | `classic64` (amd64), `classic32` (i386/686-pae, §4.1), `min` (FLOOR base), `live-amd64`, `live-desktop-amd64`, `live-compat-amd64` |
| `mkrepo.sh` | ✅ | Assemble the apt overlay repo from built `.deb`s (`pool/` + `dists/` + checksummed `Release`); signs `InRelease`/`Release.gpg` when a key id is passed (`--sign`, §17.3 — keys never live here); unsigned output works with `[trusted=yes]` for QA |
| `chroot/` | started | Castalia archive key lands here (§13.1) |

## Editions (§4.1)

- `classic32` — i686 **SSE2 required**
- `classic64` — x86-64
- `legacy32` — i686 **non-SSE2**, minimal (post-1.0 stretch)
- `min` — the FLOOR base image (`castalia-min` metapackage)

Signing keys are **never** committed; they are referenced by name from `ci/`
(§17.3).
