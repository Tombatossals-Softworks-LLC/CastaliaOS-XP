#!/bin/sh
# shrink-smoke.sh — prove that making room does not destroy what was there.
#
# Shrinking is the only thing this installer does to data that was on the
# machine before it arrived, so it is the one operation whose unit tests are
# not enough. The tests assert the plan; only a real block device proves the
# engine carries the plan out, that the resizer really did move the end of the
# filesystem, and that every byte on the far side of the old boundary is still
# readable afterwards.
#
# The method is the one that cannot be argued with: fill a partition with
# known data — including a file placed deliberately NEAR THE END, which is the
# region a mis-ordered shrink destroys and a correct one relocates — record a
# checksum, shrink, and check again.
#
# NTFS is used when ntfs-3g is on the host, because Windows is the case this
# feature exists for. Otherwise it falls back to ext4 and says so: what is
# under test is the ordering and the offsets, which are the same either way,
# and a smoke test that silently does not run is worse than one that runs on
# the second-best filesystem.
#
# Needs root. Usage: sudo sh installer/tests/shrink-smoke.sh
set -eu
export PATH="/usr/local/sbin:/usr/sbin:/sbin:$PATH"

command -v losetup >/dev/null 2>&1 || { echo "need losetup" >&2; exit 2; }
[ "$(id -u)" = "0" ] || { echo "shrink-smoke: must run as root" >&2; exit 2; }

HERE=$(cd "$(dirname "$0")/.." && pwd)     # installer/
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

# Which filesystem can we actually exercise here?
if command -v mkfs.ntfs >/dev/null 2>&1 && \
   command -v ntfsresize >/dev/null 2>&1; then
    FS=ntfs
    MKFS="mkfs.ntfs -q -f -L OtherOS"
else
    FS=ext4
    MKFS="mkfs.ext4 -qF -L OtherOS"
    echo "shrink-smoke: NOTE — ntfs-3g not installed, exercising ext4 instead"
    echo "shrink-smoke:        (same ordering, same offsets, different tool)"
fi
echo "shrink-smoke: filesystem under test: $FS"

echo "shrink-smoke: building a fake source root"
mkdir -p "$SRC/etc" "$SRC/home"
echo "castalia-marker-$$" > "$SRC/etc/castalia-release"
echo "hello" > "$SRC/home/doc.txt"

# ---- 1. a disk that is FULL: one partition, edge to edge -------------------
# This is the case alongside install cannot help with, and the reason this
# feature exists. There is no free space anywhere on this disk.
DISK_MIB=24576
echo "shrink-smoke: creating a ${DISK_MIB} MiB disk with no free space at all"
truncate -s "${DISK_MIB}M" "$IMG"
LOOP=$(losetup -fP --show "$IMG")
parted -s "$LOOP" mklabel msdos
parted -s "$LOOP" mkpart primary 1MiB 100%
partprobe "$LOOP"
sleep 1
$MKFS "${LOOP}p1"
mount "${LOOP}p1" "$OTHER"

mkdir -p "$OTHER/Users/dave/Documents"
dd if=/dev/urandom of="$OTHER/Users/dave/Documents/photos.bin" \
   bs=1M count=256 status=none
echo "las fotos de la boda" > "$OTHER/Users/dave/Documents/nota.txt"
# The interesting one. A file written after several gigabytes of filler sits
# high in the volume — out beyond where the new partition boundary will fall.
# A shrink done in the wrong order leaves this unreadable while everything
# near the start still looks fine, which is exactly how the bug hides.
echo "shrink-smoke: placing a file out past the future boundary"
dd if=/dev/urandom of="$OTHER/filler.bin" bs=1M count=6144 status=none
dd if=/dev/urandom of="$OTHER/far-file.bin" bs=1M count=64 status=none
sync
FAR_MD5=$(md5sum "$OTHER/far-file.bin" | cut -d' ' -f1)
BEFORE=$(find "$OTHER" -type f -exec md5sum {} + | sort -k2 | md5sum)
BEFORE_COUNT=$(find "$OTHER" -type f | wc -l)
USED_MIB=$(df -BM --output=used "$OTHER" | tail -n1 | tr -dc '0-9')
echo "shrink-smoke:   $BEFORE_COUNT files, ${USED_MIB} MiB in use, digest ${BEFORE%% *}"
umount "$OTHER"

TABLE_BEFORE=$(parted -sm "$LOOP" unit MiB print | grep '^1:')
echo "shrink-smoke:   before: $TABLE_BEFORE"

# ---- 2. the REAL installer, in shrink mode ---------------------------------
echo "shrink-smoke: running the REAL installer (--mode shrink)"
PYTHONPATH="$HERE" python3 -m castalia_installer \
    --disk "$LOOP" --disk-size-mib "$DISK_MIB" \
    --mode shrink --shrink "${LOOP}p1" \
    --source-root "$SRC" --mount-root "$MNT" \
    --hostname pc-castalia --user dave --ram-mib 512 \
    --copy-only --confirm-erase "$LOOP" || {
        echo "shrink-smoke: FAIL — the installer refused or errored" >&2
        exit 1
    }

# ---- 3. did the partition actually get smaller? ----------------------------
echo "shrink-smoke: verifying the shrink really happened"
fail=0

TABLE_AFTER=$(parted -sm "$LOOP" unit MiB print | grep '^1:')
SIZE_BEFORE=$(echo "$TABLE_BEFORE" | cut -d: -f4 | tr -dc '0-9')
SIZE_AFTER=$(echo "$TABLE_AFTER" | cut -d: -f4 | tr -dc '0-9')
echo "shrink-smoke:   after:  $TABLE_AFTER"
if [ "$SIZE_AFTER" -lt "$SIZE_BEFORE" ]; then
    echo "  OK  partition 1 shrank: ${SIZE_BEFORE} MiB -> ${SIZE_AFTER} MiB"
else
    echo "  FAIL partition 1 did not shrink" >&2; fail=1
fi
if [ "$SIZE_AFTER" -gt "$USED_MIB" ]; then
    echo "  OK  what is left (${SIZE_AFTER} MiB) still holds what was in it"
else
    echo "  FAIL shrank to ${SIZE_AFTER} MiB with ${USED_MIB} MiB in use" >&2
    fail=1
fi

# ---- 4. is every byte still there? -----------------------------------------
echo "shrink-smoke: verifying the data survived"
if mount "${LOOP}p1" "$OTHER" 2>/dev/null; then
    echo "  OK  the shrunk filesystem still mounts"
    AFTER=$(find "$OTHER" -type f -exec md5sum {} + | sort -k2 | md5sum)
    AFTER_COUNT=$(find "$OTHER" -type f | wc -l)
    if [ "$BEFORE" = "$AFTER" ]; then
        echo "  OK  every file is byte-identical ($AFTER_COUNT files)"
    else
        echo "  FAIL the data changed" >&2
        echo "       before: ${BEFORE%% *} ($BEFORE_COUNT files)" >&2
        echo "       after:  ${AFTER%% *} ($AFTER_COUNT files)" >&2
        fail=1
    fi
    # Named explicitly, because this is the one a wrong shrink eats.
    if [ "$(md5sum "$OTHER/far-file.bin" 2>/dev/null | cut -d' ' -f1)" \
         = "$FAR_MD5" ]; then
        echo "  OK  the file out past the old boundary reads back correctly"
    else
        echo "  FAIL the file past the old boundary is gone or corrupted" >&2
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
    echo "  FAIL the shrunk filesystem no longer mounts" >&2; fail=1
fi

# ---- 5. and did Castalia land in the space that freed up? ------------------
echo "shrink-smoke: verifying Castalia landed in the freed space"
if mount "${LOOP}p4" "$MNT" 2>/dev/null; then
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
    echo "  OK  4 partitions: the shrunk original plus Castalia's three"
else
    echo "  FAIL expected 4 partitions, found $PARTS" >&2; fail=1
fi

if [ "$fail" = "0" ]; then
    echo "shrink-smoke: PASS — made room on a full disk, data intact ($FS)"
    exit 0
fi
echo "shrink-smoke: FAIL" >&2
exit 1
