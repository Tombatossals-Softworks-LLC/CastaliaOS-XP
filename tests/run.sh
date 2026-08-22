#!/bin/sh
# run.sh — the one-command Castalia QA runner (Bible §19). Runs the same
# gates CI runs, locally, in tiers:
#
#   lint    ruff over all Python tooling
#   unit    unit tests: QA tooling, installer backend, Restore Points
#           backend, hardware probe
#   gates   design-system + legal + pipeline gates (theme-lint, provenance,
#           theme/sound export smokes, mkiso/mkdeb/mkrepo/installer dry-runs,
#           Restore Points snapshot smoke)
#   build   compile the Qt shell + run the three head-less self-tests
#   render  offscreen-render every app in every theme (needs `build`)
#   e2e     live end-to-end under Xvfb + Openbox: EWMH taskbar smoke,
#           the full app suite, and the real session entry point (needs
#           `build`; Xvfb/openbox/x11-utils/imagemagick on the host)
#   perf    the §16 budgets: shell/app memory and launch latency, measured
#           off real binaries under Xvfb (needs `build`)
#   iso     REAL live-amd64 ISO build + QEMU boot assert (root + debootstrap
#           + qemu; slow — this is the nightly/release gate)
#
# Presets:  quick = lint unit gates               (no toolchain beyond python3)
#           full  = quick + build render e2e perf (the per-commit CI surface)
#
# Usage:
#   sh tests/run.sh [quick|full|iso|lint|unit|gates|build|render|e2e|perf]...
#
# Default is `quick`. Tiers run in the order given and stop at the first
# failure. Exit 0 = everything requested passed.

set -eu

REPO=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BINDIR="$REPO/build/out/shell-build"
cd "$REPO"

say()  { printf '\nrun[%s]: %s\n' "$1" "$2"; }

tier_lint() {
    say lint "ruff over tools/ installer/ recovery/ hwprobe/"
    ruff check tools installer recovery hwprobe
}

tier_unit() {
    say unit "QA tooling"
    PYTHONPATH=tools python3 -m unittest discover -s tools/tests
    say unit "installer backend (Bible §14)"
    PYTHONPATH=installer python3 -m unittest discover -s installer/tests
    say unit "Restore Points backend (Bible §9/P8)"
    PYTHONPATH=recovery python3 -m unittest discover -s recovery/tests
    say unit "hardware probe (Bible §6.15)"
    PYTHONPATH=hwprobe python3 -m unittest discover -s hwprobe/tests
}

tier_gates() {
    say gates "theme-lint (design system, §8)"
    PYTHONPATH=tools python3 -m castalia_qa.theme_lint themes
    say gates "provenance-check (legal, §3.9)"
    PYTHONPATH=tools python3 -m castalia_qa.provenance .
    say gates "theme-export smoke (§6.16)"
    PYTHONPATH=tools python3 tools/theme_export.py \
        --out /tmp/castalia-theme-export-smoke
    say gates "sound render smoke (§21.4)"
    PYTHONPATH=tools python3 tools/sound_gen.py --out /tmp/castalia-sound-smoke
    say gates "ISO pipeline dry-run, every edition (§17.2)"
    for p in build/profiles/*.conf; do
        e=$(basename "$p" .conf)
        sh build/mkiso.sh --edition "$e" --dry-run > /dev/null
        echo "  mkiso --edition $e: plan OK"
    done
    say gates "package + repo dry-run (§13, §17.2)"
    sh packages/mkdeb.sh --dry-run > /dev/null && echo "  mkdeb: plan OK"
    sh build/mkrepo.sh --dry-run > /dev/null && echo "  mkrepo: plan OK"
    say gates "hardware probe dry-run against this machine (§6.15)"
    PYTHONPATH=hwprobe python3 -m castalia_hwprobe --dry-run \
        --quirks hwprobe/quirks.json > /dev/null
    echo "  castalia-hwprobe: probed OK"

    say gates "installer plan dry-run (§14)"
    PYTHONPATH=installer python3 -m castalia_installer \
        --disk /dev/sda --disk-size-mib 40960 --hostname pc-castalia \
        --user dave --dry-run > /dev/null
    echo "  castalia-install: whole-disk plan OK"
    PYTHONPATH=installer python3 -m castalia_installer \
        --disk /dev/sda --disk-size-mib 40960 --hostname pc-castalia \
        --user dave --mode alongside --dry-run > /dev/null
    echo "  castalia-install: alongside plan OK"
    PYTHONPATH=installer python3 -m castalia_installer \
        --disk /dev/sda --disk-size-mib 204800 --hostname pc-castalia \
        --user dave --mode shrink --dry-run \
        --pretend-partition 1:1:204799:ntfs:40960 > /dev/null
    echo "  castalia-install: shrink plan OK"
    say gates "Restore Points snapshot/restore smoke (§9/P8)"
    sh recovery/tests/snapshot-smoke.sh
}

tier_build() {
    say build "theme QSS from tokens"
    PYTHONPATH=tools python3 tools/theme_export.py
    say build "cmake configure + build (Qt 5.15 / C++17)"
    cmake -S shell -B "$BINDIR" -DCMAKE_BUILD_TYPE=Release
    cmake --build "$BINDIR" -j"$(nproc 2>/dev/null || echo 4)"
    say build "head-less self-tests (libcastalia-ui + the two games, §9.4/§12.3)"
    "$BINDIR/libcastalia-ui/castalia-ui-selftest" .
    "$BINDIR/apps/buscaminas/castalia-buscaminas" --selftest
    "$BINDIR/apps/solitario/castalia-solitario" --selftest
}

tier_render() {
    say render "offscreen render of every app in every theme (§8, §19)"
    sh tests/offscreen/render-all.sh --bindir "$BINDIR" --repo "$REPO"
}

tier_e2e() {
    say e2e "live EWMH taskbar smoke (§7.2)"
    sh tests/live/desktop-smoke.sh --bindir "$BINDIR" --repo "$REPO"
    say e2e "full app suite, live under Openbox (§9, §19)"
    sh tests/e2e/apps-live.sh --bindir "$BINDIR" --repo "$REPO"
    say e2e "real session entry point: boot/supervise/logout (§7.1)"
    sh tests/e2e/session-smoke.sh --bindir "$BINDIR" --repo "$REPO"
}

tier_perf() {
    say perf "§16 budgets: FLOOR memory + launch latency"
    sh tests/perf/measure.sh --bindir "$BINDIR" --repo "$REPO" \
        --runs 3 --out /tmp/castalia-perf.json
    PYTHONPATH=tools python3 -m castalia_qa.perf /tmp/castalia-perf.json
}

tier_iso() {
    say iso "REAL live-amd64 ISO build (root + debootstrap; slow)"
    sh build/mkiso.sh --edition live-amd64
    say iso "QEMU boot assert (FLOOR: 1 vCPU, 512 MB, TCG)"
    sh tests/qemu/boot-smoke.sh build/out/iso/castalia-live-amd64.iso \
        --timeout 300
}

[ $# -gt 0 ] || set -- quick
for arg in "$@"; do
    case "$arg" in
        quick) tier_lint; tier_unit; tier_gates ;;
        full)  tier_lint; tier_unit; tier_gates
               tier_build; tier_render; tier_e2e; tier_perf ;;
        lint|unit|gates|build|render|e2e|perf|iso) "tier_$arg" ;;
        *) echo "run: unknown tier '$arg' (see the header of tests/run.sh)" >&2
           exit 2 ;;
    esac
done

printf '\nrun: PASS — all requested tiers green (%s)\n' "$*"
