#!/bin/sh
# desktop-hero.sh — capture the REAL composited Castalia desktop for a theme
# (Bible §7.2, §8.6, §19). Unlike desktop-smoke.sh (which runs plain Openbox to
# count taskbar windows), this installs the theme's generated Openbox themerc
# and points Openbox at it, so the window decorations are the actual Castalia
# titlebars for that theme — then it opens the shell plus a few first-party
# windows and grabs the screen. Used to produce the evidence/showcase heroes,
# and doubles as a live check that a theme survives a real WM end to end.
#
# Usage:
#   sh tests/live/desktop-hero.sh --theme medianoche --shot out.png
#                                 [--bindir DIR] [--repo PATH] [--display :N]
#
# Requires: Xvfb, openbox, xprop, ImageMagick's import. Run tools/theme_export.py
# first so build/out/themes/<theme>/openbox-3/themerc exists.

set -u

BINDIR=build/out/shell-build
REPO=.
DISP=:98
SHOT=
THEME=classic
while [ $# -gt 0 ]; do
    case "$1" in
        --bindir)  BINDIR=${2:?}; shift 2 ;;
        --repo)    REPO=${2:?}; shift 2 ;;
        --display) DISP=${2:?}; shift 2 ;;
        --shot)    SHOT=${2:?}; shift 2 ;;
        --theme)   THEME=${2:?}; shift 2 ;;
        *) echo "desktop-hero: unknown option: $1" >&2; exit 2 ;;
    esac
done

REPO=$(cd "$REPO" && pwd)
BINDIR=$(cd "$BINDIR" && pwd)
DNUM=${DISP#:}

for tool in Xvfb openbox xprop import; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "desktop-hero: missing required tool: $tool" >&2; exit 2; }
done

OB_SRC="$REPO/build/out/themes/$THEME/openbox-3/themerc"
[ -f "$OB_SRC" ] || {
    echo "desktop-hero: missing $OB_SRC — run tools/theme_export.py first" >&2
    exit 2; }

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

# Install the generated Openbox theme where Openbox looks for it, and select it.
OB_NAME="Castalia-$THEME"
mkdir -p "$HOME_DIR/.local/share/themes/$OB_NAME/openbox-3"
cp "$OB_SRC" "$HOME_DIR/.local/share/themes/$OB_NAME/openbox-3/themerc"
mkdir -p "$HOME_DIR/.config/openbox"
cat > "$HOME_DIR/.config/openbox/rc.xml" <<XML
<?xml version="1.0" encoding="UTF-8"?>
<openbox_config xmlns="http://openbox.org/3.4/rc">
  <theme>
    <name>$OB_NAME</name>
    <titleLayout>NLIMC</titleLayout>
    <keepBorder>yes</keepBorder>
  </theme>
  <placement><policy>Smart</policy></placement>
</openbox_config>
XML

echo "desktop-hero: X server on $DISP"
Xvfb "$DISP" -screen 0 1024x768x24 -nolisten tcp >"$HOME_DIR/xvfb.log" 2>&1 &
XVFB=$!
i=0
while [ ! -e "/tmp/.X11-unix/X$DNUM" ] && [ $i -lt 100 ]; do
    sleep 0.1; i=$((i + 1))
done
[ -e "/tmp/.X11-unix/X$DNUM" ] || { echo "desktop-hero: X did not start" >&2; exit 1; }
sleep 0.5

echo "desktop-hero: Openbox with theme $OB_NAME"
openbox >"$HOME_DIR/openbox.log" 2>&1 &
OB=$!
i=0
while ! xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null | grep -q window; do
    sleep 0.1; i=$((i + 1))
    [ $i -lt 50 ] || { echo "desktop-hero: no EWMH WM appeared" >&2; exit 1; }
done

echo "desktop-hero: shell + first-party windows ($THEME)"
"$BINDIR/desktop/castalia-desktop" --theme "$THEME" --repo "$REPO" \
    --size 1024x768 >"$HOME_DIR/desktop.log" 2>&1 &
DESK=$!
sleep 0.4
"$BINDIR/panel/castalia-panel" --theme "$THEME" --repo "$REPO" \
    >"$HOME_DIR/panel.log" 2>&1 &
PANEL=$!
sleep 0.6
"$BINDIR/apps/software/castalia-software" --theme "$THEME" --repo "$REPO" \
    --demo >"$HOME_DIR/a1.log" 2>&1 &
A1=$!
"$BINDIR/apps/control-center/castalia-control-center" --theme "$THEME" \
    --repo "$REPO" >"$HOME_DIR/a2.log" 2>&1 &
A2=$!
"$BINDIR/apps/clock/castalia-reloj" --theme "$THEME" --repo "$REPO" \
    --time 10:09:36 >"$HOME_DIR/a3.log" 2>&1 &
A3=$!

sleep 3.5
kill -0 "$PANEL" 2>/dev/null || { echo "desktop-hero: panel died" >&2; exit 1; }

if [ -n "$SHOT" ]; then
    import -window root -display "$DISP" "$SHOT" 2>/dev/null \
        && echo "desktop-hero: wrote $SHOT" \
        || { echo "desktop-hero: capture failed" >&2; exit 1; }
fi
echo "desktop-hero: PASS — $THEME desktop composed under a real WM"
