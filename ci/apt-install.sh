#!/bin/sh
# ci/apt-install.sh — install build dependencies on a CI runner (Bible §17.4).
#
# Every job here starts by installing a toolchain from the runner's Ubuntu
# mirror, and that mirror is the least reliable thing in the pipeline: three
# separate runs on 2026-08-19 died in it, one of them 45 minutes into an
# `apt-get update` that never returned. A red build that says nothing about
# the code is worse than no build — it trains you to ignore the light.
#
# So: a hard wall-clock limit on each apt run, retries with backoff, and a
# fall back to the canonical archive when the runner's local mirror is the
# thing that is broken.
#
# The wall-clock limit is the part that does the work, and it is here because
# the version without it did not help: apt's own Acquire::*::Timeout settings
# did not fire during a 44-minute silent hang inside `apt-get update` on
# 2026-08-19 (job 96222176649 — 2679 seconds without a line of output, right
# after contacting the mirror). Those settings bound a stalled *connection*;
# they do not bound the command. Only `timeout` guarantees that a retry ever
# gets to happen, and a retry that never happens is not a retry.
#
# Usage: sh ci/apt-install.sh PACKAGE...
set -eu

[ $# -gt 0 ] || { echo "apt-install: no packages given" >&2; exit 2; }

# Fail fast on a stalled connection so a retry can actually happen, and let
# apt do its own inner retries first. DPkg::Lock::Timeout matters because a
# previous attempt we killed may still be letting go of the lock.
OPTS="-o Acquire::Retries=3
      -o Acquire::http::Timeout=25
      -o Acquire::https::Timeout=25
      -o Acquire::ftp::Timeout=25
      -o DPkg::Lock::Timeout=120"

# Wall-clock ceiling per apt run. Generous — a healthy runner installs the
# heaviest of these toolchains in about a minute — but finite, which is the
# whole point. `timeout` runs UNDER sudo so it signals apt directly rather
# than signalling sudo and hoping it passes it on.
APT_TIMEOUT=${APT_TIMEOUT:-420}

attempt() {
    # shellcheck disable=SC2086  # OPTS is a deliberate list of flags
    sudo timeout -k 30 "$APT_TIMEOUT" apt-get $OPTS "$@"
    rc=$?
    [ "$rc" = 124 ] && echo "apt-install: apt-get $1 hit the ${APT_TIMEOUT}s wall" >&2
    return $rc
}

# The runner's regional mirror is normally the fastest thing available; the
# canonical archive is the fallback for when it is not there at all.
use_fallback_mirror() {
    [ -d /etc/apt/sources.list.d ] || return 1
    echo "apt-install: switching off the runner's mirror to archive.ubuntu.com" >&2
    # Ubuntu 24.04 keeps its sources in deb822 (.sources); older images use
    # sources.list. Rewrite whichever exists, over http and https alike.
    sudo sed -i -E 's|(https?)://[a-z0-9.-]+\.archive\.ubuntu\.com|\1://archive.ubuntu.com|g' \
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
    # On the first failure, not the second: when the runner's mirror is the
    # broken part, a second full attempt against it costs another wall.
    [ "$n" -eq 1 ] && use_fallback_mirror
    if [ "$n" -lt 3 ]; then
        echo "apt-install: attempt $n failed; retrying in $((n * 15))s" >&2
        sleep $((n * 15))
    fi
done

echo "apt-install: could not install after 3 attempts: $*" >&2
exit 1
