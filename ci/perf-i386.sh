#!/bin/sh
# ci/perf-i386.sh — the §16 budgets on the architecture they were written for.
#
# FLOOR (§4.2, §16) is a Pentium 4: **i686**, 32-bit. Every number in §16.2 was
# written for that machine. The per-commit gate measures on the amd64 runner
# because that is what the runner is, and there it found an empty Castalia
# window costing 31 MB — more than the whole 25 MB Control Center budget,
# before the app does anything. Two explanations fit that: the budgets are
# wrong, or the measurement is on the wrong architecture. Sixty-four-bit
# pointers are not free, and nobody had checked.
#
# So this builds the shell for i386 inside a Debian bookworm i386 container —
# the same suite build/profiles/classic32.conf debootstraps — and runs the
# same harness. i386 binaries run natively on an amd64 host, so this is a real
# 32-bit build measured at 32-bit cost, not an emulation estimate.
#
# It is also the first time anything builds the 32-bit edition at all. §18
# Phase 11 ships classic32; CI has only ever built amd64, so "it compiles for
# i386" has been an assumption since the beginning. Now it is a test.
#
# Runs INSIDE the container, with the repo mounted at the working directory.
set -eu
export DEBIAN_FRONTEND=noninteractive

echo "perf-i386: $(uname -m) / $(dpkg --print-architecture)"

echo "perf-i386: installing the toolchain"
apt-get update -o Acquire::Retries=3
apt-get install -y --no-install-recommends \
    build-essential cmake python3 \
    qtbase5-dev libqt5svg5-dev libxcb1-dev \
    xvfb openbox x11-utils fonts-dejavu-core procps

echo "perf-i386: exporting theme QSS from tokens"
PYTHONPATH=tools python3 tools/theme_export.py

echo "perf-i386: building the shell for i386"
cmake -S shell -B build/out/shell-i386 -DCMAKE_BUILD_TYPE=Release
cmake --build build/out/shell-i386 -j"$(nproc)"

# Prove the binaries really are 32-bit before believing a single number off
# them: a silently-amd64 build would reproduce the amd64 result and look like
# a finding.
BIN=build/out/shell-i386/panel/castalia-panel
# bytes 1..4 of an ELF header are "ELF" then the class byte: 01 = 32-bit,
# 02 = 64-bit. (-N5 would also pull in the endianness byte and never match.)
case "$(od -An -t x1 -N4 -j1 "$BIN" | tr -d ' ')" in
    454c4601) echo "perf-i386: confirmed ELF32 binaries" ;;
    *) echo "perf-i386: FAIL — $BIN is not ELF32; this would not be a" \
            "32-bit measurement" >&2
       exit 1 ;;
esac

echo "perf-i386: measuring"
sh tests/perf/measure.sh --bindir build/out/shell-i386 --repo . \
    --runs 3 --display :98 --out /tmp/perf-i386.json

echo
echo "perf-i386: §16 budgets, measured on the FLOOR architecture"
PYTHONPATH=tools python3 -m castalia_qa.perf /tmp/perf-i386.json
