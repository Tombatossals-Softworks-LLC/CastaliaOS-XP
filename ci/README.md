# ci/ — Continuous integration & release pipeline

Pipeline definitions and runner config. **Signing keys and secrets live outside
this repo** and are referenced by name only. See
[`docs/PROJECT_BIBLE.md` §17.3–17.4](../docs/PROJECT_BIBLE.md#17-build-system-and-repository-structure).

## Pipeline stages

The pipelines are implemented as GitHub Actions in `.github/workflows/`:

| Stage | Workflow | Runs | Does |
|-------|----------|------|------|
| **Per-commit** | `ci.yml` | pushes to `main`, every PR (+ manual) | `ruff` lint; unit tests (QA tooling, installer, Restore Points); **theme + provenance linters**; mkiso/mkdeb/mkrepo dry-run plans; shell build; the offscreen render of every app × every theme; **live E2E** (EWMH taskbar, the full app suite and the real session under Xvfb + Openbox); **§16 performance budgets** (shell/app memory at the FLOOR numbers, launch latency); **distribution gate** (build the `.deb`, install it, run it, apt-resolve it from the generated overlay repo); live ISO build + QEMU boot; installer loopback + install-and-boot |
| **FLOOR perf** | `ci.yml` job `perf-floor` | every push/PR | Builds the shell for **i386** in a Debian bookworm i386 container — the first thing in the pipeline that builds the 32-bit edition at all — and measures the §16 budgets on the architecture they were written for. Informational: which architecture the budgets are *enforced* on is a §16.4 decision |
| **Nightly** | `nightly.yml` | daily 03:17 UTC (+ manual) | REAL ISO builds (`live-amd64`, `live-desktop-amd64`) → QEMU boot + framebuffer proofs at the FLOOR tier; the installer end-to-end; a nightly-stamped `.deb` + apt repo. Artifacts kept 7 days |
| **Desktop ISO** | `desktop-iso.yml` | manual | On-demand graphical ISO build + framebuffer capture |
| **Release** | `release.yml` | tag `v*` | The full QA gate (`tests/run.sh full`), then `.deb` + apt repo + both ISOs (each boot-verified in QEMU), `SHA256SUMS` (GPG-signed when the key secret is configured) → a **DRAFT** GitHub Release; a human reviews and publishes (§17.3). Real-hardware certification (§19) stays a manual pre-publish step |

Everything the workflows run is also runnable locally: `sh tests/run.sh full`
(see `tests/README.md`), `sh packages/mkdeb.sh`, `sh build/mkrepo.sh`,
`sh build/mkiso.sh --edition …`.

## Releasing

Tagging `vX.Y.Z` (matching the repo-root `VERSION` file — the workflow
refuses a mismatch) runs the whole release pipeline and leaves a draft
release with QEMU-verified artifacts. See [`docs/RELEASING.md`](../docs/RELEASING.md).

## Signing

- Release key: offline / hardware token, manual approval.
- Nightly key: lower-trust CI key, auto-sign.
- Keys are **never** committed here.

## Legal gate

A build cannot pass if the provenance ledger or legal notice is incomplete
(§3.9, §19) — this is a hard, non-overridable gate.
