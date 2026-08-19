# third_party/ — Pinned upstream components

Upstream components we build on, pulled as **pinned** submodules/tarballs, plus
our patches. Every component is tracked in
[`legal/THIRD_PARTY.md`](../legal/THIRD_PARTY.md) with its SPDX license. See
[`docs/PROJECT_BIBLE.md` §17.1](../docs/PROJECT_BIBLE.md#17-build-system-and-repository-structure).

## Planned contents

| Path | Purpose |
|------|---------|
| `<component>/` | Pinned source (submodule or vendored tarball) |
| `patches/` | Our patches on top of upstream, per component |

## Rules

- **Pin everything** — reproducible builds depend on fixed inputs (§13.2).
- A component may not land here unless it is recorded in `legal/THIRD_PARTY.md`
  with a distribution-compatible license (license-check enforces this).
- Prefer curating upstream over rebuilding it (§22): we do **not** maintain our
  own browser engine, office suite, codecs, or driver stack.
