# Releasing Castalia OS

How a commit becomes a distributable release (Bible §17.3–§17.5, §19).
Everything here is automated by `.github/workflows/release.yml` except the
two steps the Bible deliberately keeps human: certification and the final
publish click.

## The short version

```sh
# 1. set the version (MAJOR.MINOR[.PATCH], §17.5)
echo 0.2.0 > VERSION
git commit -am "Release 0.2.0"

# 2. tag it — the tag MUST be v$(cat VERSION); the workflow refuses a mismatch
git tag v0.2.0
git push origin main v0.2.0
```

The `Release` workflow then:

1. **Gates.** `tests/run.sh full` — ruff, every unit suite, the theme +
   provenance (legal, §3.9) linters, the mkiso/mkdeb/mkrepo dry-run plans,
   a clean shell build with the three head-less self-tests, the offscreen
   render of every app in every theme, and the live E2E suite (EWMH taskbar,
   all 25 apps, the real `castalia-session`) under Xvfb + Openbox.
2. **Packages.** `packages/mkdeb.sh` + `build/mkrepo.sh`; the `.deb` is then
   actually installed on the runner and an installed binary is executed. When
   the `CASTALIA_SIGNING_KEY` / `CASTALIA_SIGNING_PASSPHRASE` secrets are set,
   the overlay repo is **GPG-signed** (`InRelease` + `Release.gpg`) so users
   can add the stable repo to apt without `[trusted=yes]`; unset, it stays a
   `[trusted=yes]` QA repo. Keys are never in the repo (§17.3).
3. **Builds the ISOs.** `live-amd64` (boot-verified headless in QEMU at the
   FLOOR tier: 1 vCPU, 512 MB, TCG) and `live-desktop-amd64` (booted with a
   real VGA; the desktop framebuffer capture must be non-empty).
4. **Checksums + signs.** `SHA256SUMS` over every asset; if the
   `CASTALIA_SIGNING_KEY` / `CASTALIA_SIGNING_PASSPHRASE` repository secrets
   are configured, a detached `SHA256SUMS.asc` is added. Keys are never in
   the repo (§17.3).
5. **Drafts the release.** Assets + notes land in a **draft** GitHub
   Release. Nothing is public yet.

## The human part (§17.3, §19)

Before clicking **Publish** on the draft:

- [ ] Real-hardware certification pass on at least one FLOOR and one TARGET
      machine (§19.2) for MINOR releases; QEMU evidence suffices for PATCH.
- [ ] No open `sev:data-loss`, `sev:brick`, `type:legal` issues, and no
      FLOOR budget regression (§19.4 — release blockers).
- [ ] Spot-check `SHA256SUMS` against a locally downloaded asset.
- [ ] If this is a Stable release, re-sign `SHA256SUMS` with the **offline
      release key** (the CI secret is the lower-trust nightly key) and
      replace the `.asc` asset before publishing.

## Version scheme (§17.5)

- Product versions are `MAJOR.MINOR[.PATCH]` in the repo-root `VERSION`
  file; one code-named tag per release (`v0.2.0`) drives repo + ISO builds
  reproducibly.
- Package versions derive from the same file: nightlies append
  `~nightlyYYYYMMDD` (sorts *before* the release per Debian semantics).

## Rehearsal

`workflow_dispatch` on the Release workflow runs every stage — gates,
packages, both ISOs, boot proofs — but skips the publish job, so the whole
pipeline can be exercised without a tag. The per-commit `ci.yml` already
runs the same distribution gate (deb → install → run → apt-resolve) on
every push, so a broken release pipeline is caught long before tag day.

## Where things live

| Artifact | Produced by | Verified by |
|----------|-------------|-------------|
| `castalia-desktop_X.Y.Z_amd64.deb` | `packages/mkdeb.sh` | installed + executed on the runner; apt-resolved from the overlay repo |
| `castalia-repo-X.Y.Z.tar.gz` | `build/mkrepo.sh` | `apt-get update` against the generated `dists/` tree |
| `castalia-live-amd64-X.Y.Z.iso` | `build/mkiso.sh` | `tests/qemu/boot-smoke.sh` (serial marker = userspace) |
| `castalia-live-desktop-amd64-X.Y.Z.iso` | `build/mkiso.sh` | `tests/qemu/screenshot.py` (real VGA framebuffer) |
| `SHA256SUMS`(`.asc`) | release workflow | manual spot-check before publish |
