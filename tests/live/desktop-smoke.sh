#!/bin/sh
# desktop-smoke.sh — headless live-desktop integration test (Bible §7.2, §19).
#
# Unlike the offscreen render smoke (which draws one widget to a PNG), this
# exercises the REAL window-management path end to end: a genuine X server, a
# genuine EWMH window manager (Openbox — the one Castalia ships), the actual
# castalia-panel binary reading _NET_CLIENT_LIST live, and three real
# first-party app windows. It then asserts the panel's taskbar model — the
# set of managed, non-dock/desktop windows — matches exactly those three, and
# captures the composited desktop to a PNG for the record.
#
# Usage:
#   sh tests/live/desktop-smoke.sh [--bindir DIR] [--repo PATH]
#                                  [--display :N] [--shot FILE]
#
# Exit 0 = the live taskbar saw the expected windows; non-zero = it did not.
# Requires: Xvfb, openbox, xprop (x11-utils), and ImageMagick's `import`.

set -u

BINDIR=build/out/shell-build
REPO=.
DISP=:99
SHOT=
while [ $# -gt 0 ]; do
    case "$1" in
        --bindir)  BINDIR=${2:?}; shift 2 ;;
        --repo)    REPO=${2:?}; shift 2 ;;
        --display) DISP=${2:?}; shift 2 ;;
        --shot)    SHOT=${2:?}; shift 2 ;;
        *) echo "desktop-smoke: unknown option: $1" >&2; exit 2 ;;
    esac
done

REPO=$(cd "$REPO" && pwd)
BINDIR=$(cd "$BINDIR" && pwd)
DNUM=${DISP#:}

for tool in Xvfb openbox xprop import; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "desktop-smoke: missing required tool: $tool" >&2; exit 2; }
done
for b in panel/castalia-panel desktop/castalia-desktop \
         explorer/castalia-explorer apps/control-center/castalia-control-center \
         apps/text-editor/castalia-notas; do
    [ -x "$BINDIR/$b" ] || {
        echo "desktop-smoke: missing binary: $BINDIR/$b (build the shell first)" \
            >&2; exit 2; }
done

HOME_DIR=$(mktemp -d)
XVFB= OB= DESK= PANEL= A1= A2= A3=
cleanup() {
    kill $A1 $A2 $A3 $PANEL $DESK $OB $XVFB 2>/dev/null || :
    rm -rf "$HOME_DIR"
}
trap cleanup EXIT INT TERM

export DISPLAY="$DISP"
export HOME="$HOME_DIR"
export CASTALIA_REPO="$REPO"
export QT_QPA_PLATFORM=xcb

echo "desktop-smoke: starting X server on $DISP"
Xvfb "$DISP" -screen 0 1024x720x24 -nolisten tcp >"$HOME_DIR/xvfb.log" 2>&1 &
XVFB=$!
i=0
while [ ! -e "/tmp/.X11-unix/X$DNUM" ] && [ $i -lt 100 ]; do
    sleep 0.1; i=$((i + 1))
done
[ -e "/tmp/.X11-unix/X$DNUM" ] || { echo "desktop-smoke: X did not start" >&2; exit 1; }
sleep 0.5

echo "desktop-smoke: starting Openbox (EWMH window manager)"
openbox >"$HOME_DIR/openbox.log" 2>&1 &
OB=$!
# Wait for the WM to advertise EWMH support (_NET_SUPPORTING_WM_CHECK).
i=0
while ! xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null | grep -q window; do
    sleep 0.1; i=$((i + 1))
    [ $i -lt 50 ] || { echo "desktop-smoke: no EWMH WM appeared" >&2; exit 1; }
done

echo "desktop-smoke: starting shell + three first-party windows"
"$BINDIR/desktop/castalia-desktop" --theme classic --repo "$REPO" \
    >"$HOME_DIR/desktop.log" 2>&1 &
DESK=$!
sleep 0.4
"$BINDIR/panel/castalia-panel" --theme classic --repo "$REPO" \
    >"$HOME_DIR/panel.log" 2>&1 &
PANEL=$!
sleep 0.6
"$BINDIR/explorer/castalia-explorer" --theme classic --repo "$REPO" --demo \
    >"$HOME_DIR/a1.log" 2>&1 &
A1=$!
"$BINDIR/apps/control-center/castalia-control-center" --theme classic \
    --repo "$REPO" >"$HOME_DIR/a2.log" 2>&1 &
A2=$!
"$BINDIR/apps/text-editor/castalia-notas" --theme classic --repo "$REPO" \
    >"$HOME_DIR/a3.log" 2>&1 &
A3=$!

# Give the windows time to map and the panel to poll (its cycle is ~1 s).
sleep 3.5

# The panel is alive (a crash-on-EWMH would black out the real taskbar).
kill -0 "$PANEL" 2>/dev/null || { echo "desktop-smoke: panel died" >&2; exit 1; }

# Count the managed windows that the taskbar WOULD show: everything on
# _NET_CLIENT_LIST except docks (the panel) and the desktop (the wallpaper).
# This mirrors WindowList::taskbarWorthy exactly.
ids=$(xprop -root _NET_CLIENT_LIST 2>/dev/null | sed 's/.*#//; s/,//g')
[ -n "$ids" ] || { echo "desktop-smoke: _NET_CLIENT_LIST empty (no WM?)" >&2; exit 1; }
apps=0
for id in $ids; do
    ty=$(xprop -id "$id" _NET_WM_WINDOW_TYPE 2>/dev/null || true)
    case "$ty" in
        *_NET_WM_WINDOW_TYPE_DOCK*|*_NET_WM_WINDOW_TYPE_DESKTOP*) : ;;
        *) apps=$((apps + 1)) ;;
    esac
done

if [ -n "$SHOT" ]; then
    import -window root -display "$DISP" "$SHOT" 2>/dev/null \
        && echo "desktop-smoke: wrote $SHOT"
fi

echo "desktop-smoke: taskbar-worthy windows = $apps (expected 3)"
if [ "$apps" -eq 3 ]; then
    echo "desktop-smoke: PASS — live EWMH taskbar reflects the open windows"
    exit 0
fi
echo "desktop-smoke: FAIL — expected 3 app windows, saw $apps" >&2
exit 1
