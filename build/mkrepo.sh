#!/bin/sh
# mkrepo.sh — assemble the Castalia apt overlay repo from built .debs
# (Bible §13.1, §17.2 step 3). Produces a standard flat pool + dists tree:
#
#   pool/main/*.deb
#   dists/<suite>/main/binary-<arch>/Packages{,.gz}
#   dists/<suite>/Release            (+ InRelease/Release.gpg when signing)
#
# Signing (§17.3): keys NEVER live in this repo. Pass --sign KEYID (or set
# CASTALIA_SIGN_KEY) to sign with a key already in the gpg keyring — the CI
# nightly key or the offline release key. Unsigned output is valid for
# local/QA use with [trusted=yes].
#
# Usage:
#   sh build/mkrepo.sh [--debs DIR] [--out DIR] [--suite S] [--arch A]
#                      [--sign KEYID] [--dry-run]
#
# Requires dpkg-scanpackages (dpkg-dev) and gzip; gpg only when signing.

set -eu

REPO=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DEBS="$REPO/build/out/deb"
OUT="$REPO/build/out/repo"
SUITE=castalia
COMPONENT=main
ARCH=amd64
SIGN=${CASTALIA_SIGN_KEY:-}
DRY=0
while [ $# -gt 0 ]; do
    case "$1" in
        --debs)  DEBS=${2:?}; shift 2 ;;
        --out)   OUT=${2:?}; shift 2 ;;
        --suite) SUITE=${2:?}; shift 2 ;;
        --arch)  ARCH=${2:?}; shift 2 ;;
        --sign)  SIGN=${2:?}; shift 2 ;;
        --dry-run) DRY=1; shift ;;
        -h|--help) sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "mkrepo: error: unknown option: $1" >&2; exit 2 ;;
    esac
done

die() { printf 'mkrepo: error: %s\n' "$*" >&2; exit 1; }
log() { printf 'mkrepo: %s\n' "$*"; }

BINDIST="dists/$SUITE/$COMPONENT/binary-$ARCH"

if [ "$DRY" -eq 1 ]; then
    log "PLAN pool: $DEBS/*.deb -> $OUT/pool/$COMPONENT/"
    log "PLAN index: dpkg-scanpackages -> $OUT/$BINDIST/Packages{,.gz}"
    log "PLAN release: checksummed Release -> $OUT/dists/$SUITE/Release"
    if [ -n "$SIGN" ]; then
        log "PLAN sign: InRelease + Release.gpg with key $SIGN (§17.3)"
    else
        log "PLAN sign: SKIPPED (no key) — repo is [trusted=yes]-only"
    fi
    log "plan complete — run without --dry-run after packages/mkdeb.sh"
    exit 0
fi

command -v dpkg-scanpackages >/dev/null || die "missing dpkg-scanpackages (install dpkg-dev)"
set -- "$DEBS"/*.deb
[ -e "$1" ] || die "no .debs in $DEBS — run packages/mkdeb.sh first"

rm -rf "$OUT"
mkdir -p "$OUT/pool/$COMPONENT" "$OUT/$BINDIST"
cp "$@" "$OUT/pool/$COMPONENT/"
log "pool: $# package(s)"

# Package index — paths in Packages must be relative to the repo root.
(cd "$OUT" && dpkg-scanpackages --multiversion "pool/$COMPONENT" /dev/null) \
    > "$OUT/$BINDIST/Packages" 2>/dev/null
gzip -9 -kn "$OUT/$BINDIST/Packages"
log "index: $BINDIST/Packages ($(grep -c '^Package:' "$OUT/$BINDIST/Packages") entries)"

# Release — apt refuses a dists tree without one (checksums cover the index).
RELEASE="$OUT/dists/$SUITE/Release"
{
    printf 'Origin: Castalia OS\n'
    printf 'Label: Castalia Core\n'
    printf 'Suite: %s\n' "$SUITE"
    printf 'Codename: %s\n' "$SUITE"
    printf 'Architectures: %s\n' "$ARCH"
    printf 'Components: %s\n' "$COMPONENT"
    printf 'Description: Castalia OS first-party package overlay (Bible SS13.1)\n'
    printf 'Date: %s\n' "$(LC_ALL=C date -Ru)"
    printf 'SHA256:\n'
    for f in "$OUT/$BINDIST/Packages" "$OUT/$BINDIST/Packages.gz"; do
        rel=${f#"$OUT/dists/$SUITE/"}
        printf ' %s %8d %s\n' "$(sha256sum "$f" | cut -d' ' -f1)" \
            "$(wc -c < "$f")" "$rel"
    done
} > "$RELEASE"
log "release: dists/$SUITE/Release"

if [ -n "$SIGN" ]; then
    command -v gpg >/dev/null || die "signing requested but gpg is missing"
    # Non-interactive signing (CI): when CASTALIA_SIGN_PASSPHRASE is set, feed
    # it through loopback pinentry so an unattended runner can sign. Left unset,
    # gpg uses the agent (an interactive/offline-key context). Positional params
    # carry the shared flags; we're past all argument parsing here.
    set -- --batch --yes --local-user "$SIGN"
    if [ -n "${CASTALIA_SIGN_PASSPHRASE:-}" ]; then
        set -- "$@" --pinentry-mode loopback --passphrase "$CASTALIA_SIGN_PASSPHRASE"
    fi
    gpg "$@" --clearsign -o "$OUT/dists/$SUITE/InRelease" "$RELEASE"
    gpg "$@" --detach-sign --armor -o "$OUT/dists/$SUITE/Release.gpg" "$RELEASE"
    log "signed: InRelease + Release.gpg (key $SIGN)"
else
    log "UNSIGNED repo (no key given) — use: deb [trusted=yes] file:$OUT $SUITE $COMPONENT"
fi

log "done: $OUT"
