#!/bin/sh
# qemu-install.sh — the end-to-end installer proof (Bible §14, §19).
#
# The definitive "it really installs" test, and the one the loopback smoke
# leaves out: it drives the FULL castalia-install (partition → format → copy →
# fstab → GRUB in the chroot → user) against a loopback disk, using a real
# bootable Debian as the source, then boots the RESULTING disk in QEMU and
# asserts it reaches userspace on its own — from GRUB the installer wrote, the
# kernel it copied, and the fstab it generated.
#
# Needs root + debootstrap + qemu-system-x86_64 + parted/mkfs/rsync/grub tools.
# The debootstrapped source is cached (SRC_CACHE) so re-runs are fast.
#
# Usage: sudo sh installer/tests/qemu-install.sh [--timeout SEC]
set -eu
export PATH="/usr/local/sbin:/usr/sbin:/sbin:$PATH"
export DEBIAN_FRONTEND=noninteractive

TIMEOUT=360
while [ $# -gt 0 ]; do
    case "$1" in
        --timeout) TIMEOUT=${2:?}; shift 2 ;;
        *) echo "qemu-install: unknown option: $1" >&2; exit 2 ;;
    esac
done

[ "$(id -u)" = 0 ] || { echo "qemu-install: must run as root" >&2; exit 2; }
HERE=$(cd "$(dirname "$0")/.." && pwd)     # installer/
SCRATCH="${CASTALIA_SCRATCH:-/tmp/castalia-qemu-install}"
SRC_CACHE="$SCRATCH/src"
mkdir -p "$SCRATCH"

WORK=$(mktemp -d)
IMG="$WORK/disk.img"
MNT="$WORK/target"
LOG="$WORK/serial.log"
LOOP=""
QEMU=""
mkdir -p "$MNT"
cleanup() {
    [ -n "$QEMU" ] && kill "$QEMU" 2>/dev/null || :
    umount -lf "$MNT/boot" 2>/dev/null || :
    umount -lf "$MNT" 2>/dev/null || :
    [ -n "$LOOP" ] && { swapoff "${LOOP}p2" 2>/dev/null || :; \
                        losetup -d "$LOOP" 2>/dev/null || :; }
    rm -rf "$WORK"
}
trap cleanup EXIT INT TERM

# ---- 1. a real, bootable source rootfs (cached) -----------------------------
if [ ! -e "$SRC_CACHE/vmlinuz" ] && \
   ! ls "$SRC_CACHE"/boot/vmlinuz-* >/dev/null 2>&1; then
    echo "qemu-install: debootstrapping a bootable source (first run, slow)…"
    rm -rf "$SRC_CACHE"
    debootstrap --arch=amd64 \
        --include=linux-image-amd64,grub-pc \
        bookworm "$SRC_CACHE" http://deb.debian.org/debian
    # serial console so the QEMU boot is capturable headlessly
    {
        echo 'GRUB_CMDLINE_LINUX="console=tty0 console=ttyS0,115200"'
        echo 'GRUB_TERMINAL="console serial"'
        echo 'GRUB_SERIAL_COMMAND="serial --unit=0 --speed=115200"'
        echo 'GRUB_TIMEOUT=2'
    } >> "$SRC_CACHE/etc/default/grub"
    echo "qemu-install: source ready ($(du -sh "$SRC_CACHE" | cut -f1))"
else
    echo "qemu-install: reusing cached source at $SRC_CACHE"
fi

# ---- 2. a blank target disk -------------------------------------------------
echo "qemu-install: creating an 8.6 GiB target disk image"
truncate -s 8600M "$IMG"
LOOP=$(losetup -fP --show "$IMG")
echo "qemu-install: target = $LOOP"

# ---- 3. the REAL installer, full run (GRUB + user in the chroot) ------------
echo "qemu-install: running castalia-install (full)…"
printf 'castalia\n' | PYTHONPATH="$HERE" python3 -m castalia_installer \
    --disk "$LOOP" --disk-size-mib 8600 \
    --source-root "$SRC_CACHE" --mount-root "$MNT" \
    --hostname pc-castalia --user dave --ram-mib 1024 \
    --password-stdin --confirm-erase "$LOOP"

sync
losetup -d "$LOOP"; LOOP=""

# ---- 4. boot the INSTALLED disk in QEMU, assert userspace -------------------
echo "qemu-install: booting the installed disk in QEMU (TCG, ${TIMEOUT}s)…"
MARKER="pc-castalia login:|Debian GNU/Linux.*ttyS0|Reached target.*Multi-User|login:"
qemu-system-x86_64 \
    -m 1024 -smp 1 -no-reboot \
    -drive file="$IMG",format=raw,if=ide \
    -serial "file:$LOG" -display none -vga none \
    -net none < /dev/null > /dev/null 2>&1 &
QEMU=$!
# Poll the serial log; stop the instant we reach userspace (or time out).
i=0
while [ "$i" -lt "$TIMEOUT" ]; do
    grep -Eq "$MARKER" "$LOG" 2>/dev/null && break
    kill -0 "$QEMU" 2>/dev/null || break
    sleep 3; i=$((i + 3))
done
kill "$QEMU" 2>/dev/null || :
wait "$QEMU" 2>/dev/null || :
QEMU=""

if grep -Eq "$MARKER" "$LOG"; then
    echo "qemu-install: PASS — the installed system booted to userspace"
    grep -Eom1 "$MARKER" "$LOG" | sed 's/^/qemu-install:   marker: /'
    cp "$LOG" "$SCRATCH/last-serial.log"
    exit 0
fi
echo "qemu-install: FAIL — no userspace marker in ${TIMEOUT}s. Serial tail:" >&2
tail -40 "$LOG" >&2
cp "$LOG" "$SCRATCH/last-serial.log" 2>/dev/null || :
exit 1
