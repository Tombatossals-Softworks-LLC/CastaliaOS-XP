#!/bin/sh
# boot-smoke.sh — boot a Castalia ISO headless in QEMU and assert it reaches
# userspace (Bible §19.1 QEMU boot test). Captures the serial console and
# succeeds when the expected marker appears within the timeout.
#
# Usage:
#   sh tests/qemu/boot-smoke.sh ISO [--mem MB] [--timeout SEC] [--marker STR]
#
# Defaults model the FLOOR tier (§16): 1 vCPU, 512 MB, no KVM (TCG), so the
# result reflects slow-CPU behaviour. Exit 0 = booted, 1 = marker not seen.

set -eu

ISO=${1:?usage: boot-smoke.sh ISO [--mem MB] [--timeout SEC] [--marker STR]}
shift || true
MEM=512
TIMEOUT=180
# Userspace-only markers: each requires the kernel to have booted and init to
# have run. Deliberately NOT the bootloader menu title (which also contains
# "Castalia Classic") — that would false-pass on the isolinux screen.
MARKER="arranque de prueba|root@castalia|INIT: Entering runlevel|Debian GNU/Linux[^,]*castalia ttyS0"

while [ $# -gt 0 ]; do
    case "$1" in
        --mem)     MEM=${2:?}; shift 2 ;;
        --timeout) TIMEOUT=${2:?}; shift 2 ;;
        --marker)  MARKER=${2:?}; shift 2 ;;
        *) echo "boot-smoke: unknown option: $1" >&2; exit 2 ;;
    esac
done
[ -f "$ISO" ] || { echo "boot-smoke: no such ISO: $ISO" >&2; exit 2; }

LOG=$(mktemp)
trap 'rm -f "$LOG"' EXIT

echo "boot-smoke: booting $ISO (mem=${MEM}M, 1 vCPU, TCG, timeout=${TIMEOUT}s)"

# The guest was configured with console=ttyS0 + agetty autologin, so a
# successful boot writes the marker to the serial port, captured to a file
# (no stdin/monitor dependency — safe in CI and background runs).
timeout "$TIMEOUT" qemu-system-x86_64 \
    -m "$MEM" -smp 1 -no-reboot \
    -cdrom "$ISO" -boot d \
    -serial "file:$LOG" -display none -vga none \
    < /dev/null > /dev/null 2>&1 || true

if grep -Eq "$MARKER" "$LOG"; then
    echo "boot-smoke: PASS — reached userspace"
    grep -Eom1 "$MARKER" "$LOG" | sed 's/^/boot-smoke:   marker: /'
    exit 0
fi

echo "boot-smoke: FAIL — marker not seen in ${TIMEOUT}s. Tail of console:" >&2
tail -25 "$LOG" >&2
exit 1
