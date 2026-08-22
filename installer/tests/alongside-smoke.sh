#!/bin/sh
# alongside-smoke.sh — prove a dual-boot install does not eat the other OS.
#
# §23.7 #3, one of the seven things required before a public alpha: "dual-boot
# installs preserve an existing Windows partition, **verified**". Unit tests
# assert the plan against a fake runner, which proves the plan is right; only
# a real block device proves the engine does what the plan says. This is that
# test.
#
# The method is the one that cannot be argued with: put a partition full of
# known data on a real (loopback) disk, record its checksum, run the REAL
# installer in alongside mode, and check the checksum again afterwards.
#
# The other partition here is ext4, not NTFS: mkfs.ntfs is not on every build
# host, and what is under test is whether the installer writes outside the
# free region — which is a question about offsets, not about filesystems. The
# detection code that tells NTFS from ext4 is unit-tested separately against
# real lsblk output (test_alongside.py).
#
# Needs root. Usage: sudo sh installer/tests/alongside-smoke.sh
set -eu
export PATH="/usr/local/sbin:/usr/sbin:/sbin:$PATH"

command -v losetup >/dev/null 2>&1 || { echo "need losetup" >&2; exit 2; }
[ "$(id -u)" = "0" ] || { echo "alongside-smoke: must run as root" >&2; exit 2; }

HERE=$(cd "$(dirname "$0")/.." && pwd)     # installer/
# shellcheck source=installer/tests/lib-smoke.sh
. "$HERE/tests/lib-smoke.sh"
WORK=$(mktemp -d)
IMG="$WORK/disk.img"
SRC="$WORK/src"
MNT="$WORK/target"
OTHER="$WORK/other"
LOOP=""
mkdir -p "$MNT" "$OTHER"

cleanup() {
    umount -lf "$MNT/boot" 2>/dev/null || :
    umount -lf "$MNT" 2>/dev/null || :
    umount -lf "$OTHER" 2>/dev/null || :
    if [ -n "$LOOP" ]; then
        swapoff "${LOOP}p3" 2>/dev/null || :
        losetup -d "$LOOP" 2>/dev/null || :
    fi
    rm -rf "$WORK"
}
trap cleanup EXIT INT TERM

echo "alongside-smoke: building a fake source root"
mkdir -p "$SRC/etc" "$SRC/home"
echo "castalia-marker-$$" > "$SRC/etc/castalia-release"
echo "hello" > "$SRC/home/doc.txt"

# ---- 1. a disk that already has somebody else's OS on the front half -------
echo "alongside-smoke: creating a 12 GiB disk with an existing OS partition"
truncate -s 12288M "$IMG"
LOOP=$(losetup -fP --show "$IMG")
parted -s "$LOOP" mklabel msdos
parted -s "$LOOP" mkpart primary ext4 1MiB 4096MiB
partprobe "$LOOP"
sleep 1
mkfs.ext4 -qF -L OtherOS "${LOOP}p1"
settle_dev "${LOOP}p1" || { echo "alongside-smoke: ${LOOP}p1 never appeared" \
                            >&2; exit 1; }
mount "${LOOP}p1" "$OTHER"
mkdir -p "$OTHER/Users/dave/Documents"
# Something big enough that a stray write would land in it, and something
# small and precious, because both are how this goes wrong in real life.
dd if=/dev/urandom of="$OTHER/Users/dave/Documents/photos.bin" \
   bs=1M count=256 status=none
echo "las fotos de la boda" > "$OTHER/Users/dave/Documents/nota.txt"
sync
BEFORE=$(find "$OTHER" -type f -exec md5sum {} + | sort -k2 | md5sum)
BEFORE_COUNT=$(find "$OTHER" -type f | wc -l)
echo "alongside-smoke:   $BEFORE_COUNT files, digest ${BEFORE%% *}"
umount "$OTHER"

TABLE_BEFORE=$(parted -sm "$LOOP" unit MiB print | grep '^1:')
echo "alongside-smoke:   existing partition: $TABLE_BEFORE"

# ---- 2. the REAL installer, in alongside mode ------------------------------
echo "alongside-smoke: running the REAL installer (--mode alongside)"
PYTHONPATH="$HERE" python3 -m castalia_installer \
    --disk "$LOOP" --disk-size-mib 12288 \
    --mode alongside \
    --source-root "$SRC" --mount-root "$MNT" \
    --hostname pc-castalia --user dave --ram-mib 512 \
    --copy-only --confirm-erase "$LOOP"

# ---- 3. is the other OS still there, byte for byte? ------------------------
echo "alongside-smoke: verifying the existing OS survived"
fail=0

TABLE_AFTER=$(parted -sm "$LOOP" unit MiB print | grep '^1:')
if [ "$TABLE_BEFORE" = "$TABLE_AFTER" ]; then
    echo "  OK  the existing partition entry is unchanged"
else
    echo "  FAIL partition 1 moved or resized" >&2
    echo "       before: $TABLE_BEFORE" >&2
    echo "       after:  $TABLE_AFTER" >&2
    fail=1
fi

if try_mount "${LOOP}p1" "$OTHER"; then
    echo "  OK  the existing filesystem still mounts"
    AFTER=$(find "$OTHER" -type f -exec md5sum {} + | sort -k2 | md5sum)
    AFTER_COUNT=$(find "$OTHER" -type f | wc -l)
    if [ "$BEFORE" = "$AFTER" ]; then
        echo "  OK  every file is byte-identical ($AFTER_COUNT files)"
    else
        echo "  FAIL the other OS's data changed" >&2
        echo "       before: ${BEFORE%% *} ($BEFORE_COUNT files)" >&2
        echo "       after:  ${AFTER%% *} ($AFTER_COUNT files)" >&2
        fail=1
    fi
    if [ "$(cat "$OTHER/Users/dave/Documents/nota.txt" 2>/dev/null)" \
         = "las fotos de la boda" ]; then
        echo "  OK  a named file reads back correctly"
    else
        echo "  FAIL the named file is gone or corrupted" >&2; fail=1
    fi
    umount "$OTHER"
else
    echo "  FAIL the existing filesystem no longer mounts" >&2; fail=1
fi

# ---- 4. and did Castalia actually get installed? ---------------------------
echo "alongside-smoke: verifying Castalia landed in the free space"
if try_mount "${LOOP}p4" "$MNT"; then
    if [ -f "$MNT/etc/castalia-release" ]; then
        echo "  OK  Castalia root at ${LOOP}p4: $(cat "$MNT/etc/castalia-release")"
    else
        echo "  FAIL Castalia root is empty" >&2; fail=1
    fi
    umount "$MNT"
else
    echo "  FAIL no Castalia root partition at ${LOOP}p4" >&2; fail=1
fi

PARTS=$(parted -sm "$LOOP" unit MiB print | grep -c '^[0-9]:')
if [ "$PARTS" = "4" ]; then
    echo "  OK  4 partitions: the original plus Castalia's three"
else
    echo "  FAIL expected 4 partitions, found $PARTS" >&2; fail=1
fi

if [ "$fail" = "0" ]; then
    echo "alongside-smoke: PASS — installed beside the other OS, data intact"
    exit 0
fi
echo "alongside-smoke: FAIL" >&2
exit 1
