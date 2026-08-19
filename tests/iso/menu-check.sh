#!/bin/sh
# Assert the boot menu INSIDE a built ISO (Bible §14.1).
#
# The unit gate (tools/tests/test_iso_boot.py) checks the template and the
# build script agree. This checks the artefact: it pulls isolinux.cfg back out
# of the finished image and looks at what a person booting that USB stick will
# actually be offered. The two together are what stops another release going
# out with a single boot entry and no way to install.
#
#   usage: menu-check.sh path/to.iso [--expect-installer]
set -eu

ISO=${1:?usage: menu-check.sh ISO [--expect-installer]}
EXPECT_INSTALLER=0
shift
for arg in "$@"; do
    case "$arg" in
        --expect-installer) EXPECT_INSTALLER=1 ;;
        *) echo "menu-check: unknown option: $arg" >&2; exit 2 ;;
    esac
done

[ -f "$ISO" ] || { echo "menu-check: no such ISO: $ISO" >&2; exit 2; }
command -v xorriso >/dev/null || { echo "menu-check: need xorriso" >&2; exit 2; }

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
CFG="$WORK/isolinux.cfg"
# The menu as isolinux reads it: comments discuss the modes, entries offer
# them, and only the entries can mislead anyone.
DIRECTIVES="$WORK/directives.cfg"

xorriso -osirrox on -indev "$ISO" \
    -extract /isolinux/isolinux.cfg "$CFG" >/dev/null 2>&1 \
    || { echo "menu-check: FAIL — no isolinux/isolinux.cfg in the ISO" >&2; \
         exit 1; }

grep -v '^[[:space:]]*#' "$CFG" > "$DIRECTIVES"

fail=0
say() { echo "menu-check: $*"; }
bad() { say "FAIL — $*" >&2; fail=$((fail + 1)); }

grep -q '^DEFAULT live' "$CFG" \
    || bad "the live session is not the default entry"
grep -q '^LABEL live$' "$CFG" \
    || bad "no live entry at all"
grep -q 'KERNEL /live/vmlinuz' "$CFG" \
    || bad "entries do not point at the staged kernel"
grep -q '@' "$DIRECTIVES" \
    && bad "an unsubstituted @PLACEHOLDER@ shipped in the menu"
grep -q 'LABEL livesafe' "$CFG" \
    || bad "no safe-graphics entry (§14.1 requires one)"

if [ "$EXPECT_INSTALLER" -eq 1 ]; then
    grep -q 'castalia.installer=gui' "$DIRECTIVES" \
        || bad "no graphical install entry on an installer edition"
    grep -q 'castalia.installer=text' "$DIRECTIVES" \
        || bad "no text install entry on an installer edition"
else
    grep -q 'castalia.installer=' "$DIRECTIVES" \
        && bad "an edition with no installer offers to install"
fi

# Whatever the edition, the first entry a person sees must be the live one.
first=$(grep -m1 '^LABEL ' "$CFG" | awk '{print $2}')
[ "$first" = "live" ] || bad "the first entry is '$first', not 'live'"

if [ "$fail" -eq 0 ]; then
    say "PASS — $(grep -c '^LABEL ' "$CFG") entries, live first and default"
    grep '^\s*MENU LABEL' "$CFG" | sed 's/^/menu-check:   /'
    exit 0
fi
echo "menu-check: the menu as shipped:" >&2
cat "$CFG" >&2
exit 1
