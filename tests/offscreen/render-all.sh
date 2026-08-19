#!/bin/sh
# render-all.sh — render every first-party app offscreen in every theme
# (Bible §8, §9, §19). This is the CI design-system gate as a reusable,
# locally-runnable script: each app from tests/apps.manifest (plus the panel,
# Start Menu, explorer and desktop planes) draws one real frame per theme via
# QT_QPA_PLATFORM=offscreen, and the PNG must be non-empty. A widget that
# crashes, hangs or paints nothing in ANY theme fails the gate.
#
# Usage:
#   sh tests/offscreen/render-all.sh [--bindir DIR] [--repo PATH] [--out DIR]
#                                    [--themes "a b c"]
#
# Exit 0 = every app rendered in every theme. Requires a built shell tree
# (cmake) and the exported theme QSS (tools/theme_export.py).

set -eu

BINDIR=build/out/shell-build
REPO=.
OUT=/tmp/castalia-render
THEMES="human classic azul oliva plata medianoche high-contrast"
while [ $# -gt 0 ]; do
    case "$1" in
        --bindir) BINDIR=${2:?}; shift 2 ;;
        --repo)   REPO=${2:?}; shift 2 ;;
        --out)    OUT=${2:?}; shift 2 ;;
        --themes) THEMES=${2:?}; shift 2 ;;
        *) echo "render-all: unknown option: $1" >&2; exit 2 ;;
    esac
done

REPO=$(cd "$REPO" && pwd)
BINDIR=$(cd "$BINDIR" && pwd)
MANIFEST="$REPO/tests/apps.manifest"
[ -r "$MANIFEST" ] || { echo "render-all: no manifest: $MANIFEST" >&2; exit 2; }
mkdir -p "$OUT"

export QT_QPA_PLATFORM=offscreen

shots=0
render() {  # render BIN PNG ARGS... — run offscreen, then require a real PNG
    bin=$1 png=$2; shift 2
    "$BINDIR/$bin" "$@" --screenshot "$png"
    [ -s "$png" ] || { echo "render-all: empty render: $png" >&2; exit 1; }
    shots=$((shots + 1))
}

for t in $THEMES; do
    echo "render-all: theme $t"

    # Shell planes first: panel (+ Start Menu shot) and the desktop, which
    # composites the freshly rendered panel PNG like the live session does.
    "$BINDIR/panel/castalia-panel" --theme "$t" --repo "$REPO" \
        --screenshot "$OUT/panel-$t.png" --menu-shot "$OUT/menu-$t.png" \
        --switcher-shot "$OUT/switcher-$t.png"
    [ -s "$OUT/panel-$t.png" ] && [ -s "$OUT/menu-$t.png" ] \
        && [ -s "$OUT/switcher-$t.png" ] || {
        echo "render-all: empty render: panel/menu/switcher ($t)" >&2
        exit 1; }
    shots=$((shots + 3))
    render desktop/castalia-desktop "$OUT/desktop-$t.png" \
        --theme "$t" --repo "$REPO" --size 1024x768 \
        --panel-png "$OUT/panel-$t.png"

    while IFS='|' read -r name bin _live_args render_args; do
        case "$name" in ''|'#'*) continue ;; esac
        eval "set -- $render_args"
        render "$bin" "$OUT/$name-$t.png" --theme "$t" --repo "$REPO" "$@"
    done < "$MANIFEST"
done

echo "render-all: PASS — $shots renders, all themes, no empty frames"
