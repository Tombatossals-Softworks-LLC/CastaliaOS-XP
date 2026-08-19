#!/bin/sh
# ci/apt-install.sh — install build dependencies on a CI runner (Bible §17.4).
#
# Every job here starts by installing a toolchain from the runner's Ubuntu
# mirror, and that mirror is the least reliable thing in the pipeline: three
# separate runs on 2026-08-19 died in it, one of them 45 minutes into an
# `apt-get update` that never returned. A red build that says nothing about
# the code is worse than no build — it trains you to ignore the light.
#
# So: bounded waits instead of unbounded ones (a stalled mirror fails in
# seconds rather than hanging until the job times out), a few retries with
# backoff, and a fall back to the canonical archive when the runner's local
# mirror is the thing that is broken.
#
# Usage: sh ci/apt-install.sh PACKAGE...
set -eu

[ $# -gt 0 ] || { echo "apt-install: no packages given" >&2; exit 2; }

# Fail fast on a stalled connection so a retry can actually happen, and let
# apt do its own inner retries first.
OPTS="-o Acquire::Retries=3
      -o Acquire::http::Timeout=25
      -o Acquire::https::Timeout=25
      -o Acquire::ftp::Timeout=25
      -o DPkg::Lock::Timeout=120"

attempt() {
    # shellcheck disable=SC2086  # OPTS is a deliberate list of flags
    sudo apt-get $OPTS "$@"
}

# The runner's regional mirror is normally the fastest thing available; the
# canonical archive is the fallback for when it is not there at all.
use_fallback_mirror() {
    [ -d /etc/apt/sources.list.d ] || return 1
    echo "apt-install: switching off the runner's mirror to archive.ubuntu.com" >&2
    sudo sed -i 's|http://[a-z0-9.-]*\.archive\.ubuntu\.com|http://archive.ubuntu.com|g' \
        /etc/apt/sources.list /etc/apt/sources.list.d/*.list \
        /etc/apt/sources.list.d/*.sources 2>/dev/null || :
}

n=0
until [ "$n" -ge 3 ]; do
    if attempt update && attempt install -y --no-install-recommends "$@"; then
        echo "apt-install: installed $*"
        exit 0
    fi
    n=$((n + 1))
    [ "$n" -eq 2 ] && use_fallback_mirror
    if [ "$n" -lt 3 ]; then
        echo "apt-install: attempt $n failed; retrying in $((n * 15))s" >&2
        sleep $((n * 15))
    fi
done

echo "apt-install: could not install after 3 attempts: $*" >&2
exit 1
