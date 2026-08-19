#!/bin/sh
# apps-live.sh — end-to-end live test of the WHOLE first-party app suite
# (Bible §9, §19). Where the offscreen render gate proves each app can paint
# one frame, this proves each app survives contact with the real desktop: a
# genuine X server (Xvfb), the genuine EWMH window manager Castalia ships
# (Openbox), the real castalia-desktop + castalia-panel underneath — then
# every app from tests/apps.manifest is launched in turn and must:
#
#   1. map at least one taskbar-worthy EWMH window (so the live taskbar,
#      Alt-Tab and the WM all see it),
#   2. still be running while its window is up (no crash-after-map), and
#   3. exit on SIGTERM and unmap its windows (clean logout path, §7.1).
#
# The panel must survive the whole churn of 46 windows coming and going —
# a crash in its EWMH tracking fails the run even if every app passed.
#
# Usage:
#   sh tests/e2e/apps-live.sh [--bindir DIR] [--repo PATH] [--display :N]
#                             [--theme NAME] [--shotdir DIR] [--only a,b,c]
#
# Exit 0 = every app passed; 1 = at least one failed (details on stderr).
# Requires: Xvfb, openbox, xprop (x11-utils); ImageMagick's `import` only
# when --shotdir is given.

set -u

BINDIR=build/out/shell-build
REPO=.
DISP=:96
THEME=classic
SHOTDIR=
ONLY=
while [ $# -gt 0 ]; do
    case "$1" in
        --bindir)  BINDIR=${2:?}; shift 2 ;;
        --repo)    REPO=${2:?}; shift 2 ;;
        --display) DISP=${2:?}; shift 2 ;;
        --theme)   THEME=${2:?}; shift 2 ;;
        --shotdir) SHOTDIR=${2:?}; shift 2 ;;
        --only)    ONLY=${2:?}; shift 2 ;;
        *) echo "apps-live: unknown option: $1" >&2; exit 2 ;;
    esac
done

REPO=$(cd "$REPO" && pwd)
BINDIR=$(cd "$BINDIR" && pwd)
MANIFEST="$REPO/tests/apps.manifest"
DNUM=${DISP#:}

for tool in Xvfb openbox xprop; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "apps-live: missing required tool: $tool" >&2; exit 2; }
done
[ -z "$SHOTDIR" ] || command -v import >/dev/null 2>&1 || {
    echo "apps-live: --shotdir needs ImageMagick's import" >&2; exit 2; }
[ -r "$MANIFEST" ] || { echo "apps-live: no manifest: $MANIFEST" >&2; exit 2; }
[ -z "$SHOTDIR" ] || mkdir -p "$SHOTDIR"

HOME_DIR=$(mktemp -d)
XVFB= OB= DESK= PANEL= APP=
cleanup() {
    kill $APP $PANEL $DESK $OB $XVFB 2>/dev/null || :
    rm -rf "$HOME_DIR"
}
trap cleanup EXIT INT TERM

export DISPLAY="$DISP"
export HOME="$HOME_DIR"
export CASTALIA_REPO="$REPO"
export QT_QPA_PLATFORM=xcb

# Managed windows the taskbar would show: everything on _NET_CLIENT_LIST
# except docks (panel) and the desktop (wallpaper) — mirrors
# shell/panel/src/WindowList.cpp taskbarWorthy.
taskbar_worthy() {
    ids=$(xprop -root _NET_CLIENT_LIST 2>/dev/null | sed 's/.*#//; s/,//g')
    n=0
    for id in $ids; do
        ty=$(xprop -id "$id" _NET_WM_WINDOW_TYPE 2>/dev/null || true)
        case "$ty" in
            *_NET_WM_WINDOW_TYPE_DOCK*|*_NET_WM_WINDOW_TYPE_DESKTOP*) : ;;
            *) n=$((n + 1)) ;;
        esac
    done
    echo "$n"
}

# wait_for_count OP N BUDGET_TENTHS — poll until `taskbar_worthy OP N`.
wait_for_count() {
    op=$1; want=$2; budget=$3
    i=0
    while [ $i -lt "$budget" ]; do
        have=$(taskbar_worthy)
        [ "$have" "$op" "$want" ] && return 0
        sleep 0.2; i=$((i + 1))
    done
    return 1
}

echo "apps-live: starting X server on $DISP"
Xvfb "$DISP" -screen 0 1024x768x24 -nolisten tcp >"$HOME_DIR/xvfb.log" 2>&1 &
XVFB=$!
i=0
while [ ! -e "/tmp/.X11-unix/X$DNUM" ] && [ $i -lt 100 ]; do
    sleep 0.1; i=$((i + 1))
done
[ -e "/tmp/.X11-unix/X$DNUM" ] || { echo "apps-live: X did not start" >&2; exit 1; }
sleep 0.5

echo "apps-live: starting Openbox (EWMH window manager)"
openbox >"$HOME_DIR/openbox.log" 2>&1 &
OB=$!
i=0
while ! xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null | grep -q window; do
    sleep 0.1; i=$((i + 1))
    [ $i -lt 50 ] || { echo "apps-live: no EWMH WM appeared" >&2; exit 1; }
done

echo "apps-live: starting the shell (desktop + panel, theme $THEME)"
"$BINDIR/desktop/castalia-desktop" --theme "$THEME" --repo "$REPO" \
    >"$HOME_DIR/desktop.log" 2>&1 &
DESK=$!
sleep 0.4
"$BINDIR/panel/castalia-panel" --theme "$THEME" --repo "$REPO" \
    >"$HOME_DIR/panel.log" 2>&1 &
PANEL=$!
sleep 1

BASE=$(taskbar_worthy)
echo "apps-live: baseline taskbar-worthy windows = $BASE (expected 0)"

pass=0 fail=0 failed=
run_one() {
    name=$1; bin=$2; args=$3

    if [ ! -x "$BINDIR/$bin" ]; then
        echo "apps-live: FAIL $name — missing binary $BINDIR/$bin" >&2
        return 1
    fi

    eval "set -- $args"
    "$BINDIR/$bin" --theme "$THEME" --repo "$REPO" "$@" \
        >"$HOME_DIR/$name.log" 2>&1 &
    APP=$!

    # 1. a taskbar-worthy window must appear (budget 15 s)
    if ! wait_for_count -gt "$BASE" 75; then
        echo "apps-live: FAIL $name — no EWMH window within 15s" >&2
        sed 's/^/apps-live:   log: /' "$HOME_DIR/$name.log" | tail -5 >&2
        kill "$APP" 2>/dev/null; APP=
        wait_for_count -eq "$BASE" 50 || :
        return 1
    fi

    # 2. the process is still alive with its window up
    if ! kill -0 "$APP" 2>/dev/null; then
        echo "apps-live: FAIL $name — process died after mapping" >&2
        sed 's/^/apps-live:   log: /' "$HOME_DIR/$name.log" | tail -5 >&2
        wait_for_count -eq "$BASE" 50 || :
        return 1
    fi

    if [ -n "$SHOTDIR" ]; then
        import -window root -display "$DISP" "$SHOTDIR/e2e-$name.png" \
            2>/dev/null || :
    fi

    # 3. SIGTERM = clean exit + windows unmapped (budget 10 s)
    kill "$APP" 2>/dev/null
    if ! wait_for_count -eq "$BASE" 50; then
        echo "apps-live: FAIL $name — window still mapped 10s after TERM" >&2
        kill -9 "$APP" 2>/dev/null; APP=
        wait_for_count -eq "$BASE" 50 || :
        return 1
    fi
    APP=
    return 0
}

while IFS='|' read -r name bin live_args _render_args; do
    case "$name" in ''|'#'*) continue ;; esac
    if [ -n "$ONLY" ]; then
        case ",$ONLY," in *",$name,"*) : ;; *) continue ;; esac
    fi
    if run_one "$name" "$bin" "$live_args"; then
        echo "apps-live: PASS $name"
        pass=$((pass + 1))
    else
        fail=$((fail + 1)); failed="$failed $name"
    fi
    # The panel must have survived this app's window churn.
    kill -0 "$PANEL" 2>/dev/null || {
        echo "apps-live: FAIL — panel died (after $name)" >&2
        sed 's/^/apps-live:   panel: /' "$HOME_DIR/panel.log" | tail -5 >&2
        exit 1; }
done < "$MANIFEST"

echo "apps-live: $pass passed, $fail failed"
if [ "$fail" -eq 0 ] && [ "$pass" -gt 0 ]; then
    echo "apps-live: PASS — every app mapped, ran and tore down under a real WM"
    exit 0
fi
[ "$pass" -gt 0 ] || echo "apps-live: nothing ran (bad --only filter?)" >&2
[ -z "$failed" ] || echo "apps-live: failed:$failed" >&2
exit 1
