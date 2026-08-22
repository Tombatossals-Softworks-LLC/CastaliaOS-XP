# lib-smoke.sh — shared helpers for the loopback smoke tests.
#
# Sourced, not executed. POSIX sh (Bible §12).

# settle_dev DEVICE — wait for a partition node to be there and stable.
#
# The installer runs `partprobe` as one of its steps, and partprobe tears
# every partition node of the disk down and builds it again. A mount issued
# straight afterwards can arrive in the gap: the node is briefly absent, or
# present and not yet usable, and mount fails with an error that has nothing
# to do with what the test is checking.
#
# That is not a hypothetical race. It is what turned alongside-smoke red on
# CI run 32546031209 with "the existing filesystem no longer mounts" on a
# partition the installer had not touched and whose table entry the same test
# had just confirmed was unchanged.
settle_dev() {
    _dev=$1
    command -v udevadm >/dev/null 2>&1 && udevadm settle --timeout=15 || :
    _i=0
    while [ ! -b "$_dev" ] && [ "$_i" -lt 50 ]; do
        sleep 0.2
        _i=$((_i + 1))
    done
    [ -b "$_dev" ]
}

# try_mount DEVICE MOUNTPOINT — mount, patiently, and say why if it will not.
#
# Returns 0 on success. On failure it prints mount's own words, which the
# tests used to throw away with `2>/dev/null`: a smoke test that reports
# "the filesystem no longer mounts" without saying what mount said sends
# somebody looking for data loss when the answer was a missing device node.
try_mount() {
    _dev=$1
    _at=$2
    settle_dev "$_dev" || {
        echo "       $_dev never appeared as a block device" >&2
        return 1
    }
    _i=0
    while [ "$_i" -lt 5 ]; do
        _err=$(mount "$_dev" "$_at" 2>&1) && return 0
        _i=$((_i + 1))
        sleep 0.4
    done
    echo "       mount $_dev $_at: $_err" >&2
    return 1
}
