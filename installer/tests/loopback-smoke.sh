#!/bin/sh
# loopback-smoke.sh — prove the installer engine on a REAL block device.
#
# Unlike the unit tests (which assert the plan with a fake runner), this runs
# the actual engine — parted, mkfs.ext4, mkswap, mount, rsync, and UUID-based
# fstab generation — against a loopback disk image, then mounts the result and
# checks the copied tree, the fstab UUIDs, and /etc/hostname are really there.
# It skips the chroot phase (--copy-only), which needs a bootable rootfs and
# is covered by the QEMU install test instead.
#
# Needs root (losetup/parted/mkfs/mount). Usage: sudo sh loopback-smoke.sh
set -eu

# System tools (parted/mkfs/losetup) live in sbin; make sure they're findable.
export PATH="/usr/local/sbin:/usr/sbin:/sbin:$PATH"

command -v losetup >/dev/null 2>&1 || { echo "need losetup" >&2; exit 2; }
[ "$(id -u)" = "0" ] || { echo "loopback-smoke: must run as root" >&2; exit 2; }

HERE=$(cd "$(dirname "$0")/.." && pwd)     # installer/
WORK=$(mktemp -d)
IMG="$WORK/disk.img"
SRC="$WORK/src"
MNT="$WORK/target"
LOOP=""
mkdir -p "$MNT"

cleanup() {
    umount -lf "$MNT/boot" 2>/dev/null || :
    umount -lf "$MNT" 2>/dev/null || :
    [ -n "$LOOP" ] && { swapoff "${LOOP}p2" 2>/dev/null || :; \
                        losetup -d "$LOOP" 2>/dev/null || :; }
    rm -rf "$WORK"
}
trap cleanup EXIT INT TERM

echo "loopback-smoke: building a fake source root"
mkdir -p "$SRC/etc" "$SRC/usr/bin" "$SRC/home"
echo "castalia-marker-$$" > "$SRC/etc/castalia-release"
printf '#!/bin/sh\necho hi\n' > "$SRC/usr/bin/hello"
chmod +x "$SRC/usr/bin/hello"
# a few more files so rsync has something to preserve
for i in 1 2 3; do echo "file $i" > "$SRC/home/doc$i.txt"; done

echo "loopback-smoke: creating an 8.6 GiB sparse disk image"
truncate -s 8600M "$IMG"
LOOP=$(losetup -fP --show "$IMG")
echo "loopback-smoke: attached $LOOP"

echo "loopback-smoke: running the REAL installer engine (--copy-only)"
PYTHONPATH="$HERE" python3 -m castalia_installer \
    --disk "$LOOP" --disk-size-mib 8600 \
    --source-root "$SRC" --mount-root "$MNT" \
    --hostname pc-castalia --user dave --ram-mib 512 \
    --copy-only --confirm-erase "$LOOP"

echo "loopback-smoke: re-mounting the installed root to verify"
mount "${LOOP}p3" "$MNT"
mount "${LOOP}p1" "$MNT/boot"

fail=0
if [ -f "$MNT/etc/castalia-release" ]; then
    echo "  OK  copied tree present: $(cat "$MNT/etc/castalia-release")"
else
    echo "  FAIL copied tree missing" >&2; fail=1
fi
if [ -x "$MNT/usr/bin/hello" ]; then
    echo "  OK  executable bit preserved by rsync"
else
    echo "  FAIL executable bit lost" >&2; fail=1
fi
if grep -q "^UUID=.* /      ext4" "$MNT/etc/fstab" 2>/dev/null; then
    echo "  OK  fstab has a real root UUID:"
    sed 's/^/        /' "$MNT/etc/fstab"
else
    echo "  FAIL fstab missing real UUID" >&2; fail=1
fi
if [ "$(cat "$MNT/etc/hostname" 2>/dev/null)" = "pc-castalia" ]; then
    echo "  OK  /etc/hostname written"
else
    echo "  FAIL /etc/hostname wrong" >&2; fail=1
fi
# The UUIDs in fstab must match what blkid reports for the real partitions.
root_uuid=$(blkid -s UUID -o value "${LOOP}p3")
if grep -q "UUID=$root_uuid" "$MNT/etc/fstab"; then
    echo "  OK  fstab root UUID matches blkid ($root_uuid)"
else
    echo "  FAIL fstab UUID does not match blkid" >&2; fail=1
fi

[ $fail -eq 0 ] && echo "loopback-smoke: PASS — engine really installs to disk" \
    || { echo "loopback-smoke: FAIL" >&2; exit 1; }
