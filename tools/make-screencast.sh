#!/bin/sh
# make-screencast.sh — record the Castalia OS marketing demo.
#
# A REAL driven session: Xvfb + Openbox with the Castalia decorations + the
# shell (desktop + panel) + first-party apps, with xdotool playing the part of
# a user, all recorded with ffmpeg. This is the same real session the CI
# e2e/hero captures use — no mock-ups, no compositing, nothing staged that the
# machine does not actually do.
#
# The storyboard, in order:
#   1. the Start menu, and **typing to search it** (§7.3)
#   2. Pintura — two brush strokes
#   3. Buscaminas — a few cells
#   4. **Alt+Tab** through the open windows, Castalia's own switcher (§7.6)
#   5. the panel clock opens the Calendario; a note is typed
#   6. a **notification toast** arrives from the real server (§7.4)
#   7. the **Centro de redes** — Wi-Fi in plain language (§9)
#   8. Control Center → wallpaper picker, then the desktop re-skins live
#
# Beats 1, 4, 6 and 7 are the ones the previous cut predated: the menu search,
# the switcher, notifications and the finished §9 system tools.
#
# Requires: Xvfb, openbox, xdotool, ffmpeg, dbus-daemon (see docs). Build the
# shell first (cmake) and export the theme QSS/Openbox themercs
# (tools/theme_export.py).
#
# Usage:  sh tools/make-screencast.sh [--bindir DIR] [--out DIR] [--theme ID]
#                                     [--gif]
# Output: <out>/castalia-os-demo.mp4 (+ frame_*.png stills, and
#         castalia-os-demo.gif with --gif). The names are the press kit's,
#         so refreshing presskit/video/ is a copy and not a rename.
set -u
REPO=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BIN=$REPO/build/out/shell-build
THEME=human
DISP=:99
OUT=$REPO/build/out/screencast
GIF=0
while [ $# -gt 0 ]; do
    case "$1" in
        --bindir) BIN=${2:?}; shift 2 ;;
        --out)    OUT=${2:?}; shift 2 ;;
        --theme)  THEME=${2:?}; shift 2 ;;
        --gif)    GIF=1; shift ;;
        *) echo "make-screencast: unknown option: $1" >&2; exit 2 ;;
    esac
done
mkdir -p "$OUT"
HOME_DIR=$(mktemp -d)

export DISPLAY=$DISP HOME=$HOME_DIR CASTALIA_REPO=$REPO QT_QPA_PLATFORM=xcb
export CASTALIA_THEME=$THEME

# Put every built castalia-* binary on PATH so the panel's own launches
# (clock→Calendario, tray→volume, Start-menu items) actually work — the
# session behaves exactly like an installed one.
BINPATH=$HOME_DIR/bin; mkdir -p "$BINPATH"
find "$BIN" -type f -name 'castalia-*' | while read -r f; do
    ln -sf "$f" "$BINPATH/$(basename "$f")"
done
export PATH="$BINPATH:$PATH"

OB=Castalia-$THEME
mkdir -p "$HOME_DIR/.local/share/themes/$OB/openbox-3" \
         "$HOME_DIR/.config/openbox" "$HOME_DIR/.config/castalia" \
         "$HOME_DIR/Documentos" "$HOME_DIR/Escritorio"
cp "$REPO/build/out/themes/$THEME/openbox-3/themerc" \
   "$HOME_DIR/.local/share/themes/$OB/openbox-3/themerc"
printf '[meta]\nid = "human"\n' > "$HOME_DIR/.config/castalia/theme.conf"
# The SHIPPED Openbox configuration, not a minimal one written here. The
# first cut wrote its own four-line rc.xml — and an rc.xml with no <keyboard>
# section makes Openbox fall back to its BUILT-IN defaults, which bind
# Alt+Tab. Openbox starts before the panel, so it won the grab and the demo
# filmed Openbox's switcher instead of Castalia's (§7.6). Using the real file
# also means the video shows the real keyboard map.
sed "s|<name>Castalia-human</name>|<name>$OB</name>|" \
    "$REPO/shell/session/openbox-rc.xml" \
    > "$HOME_DIR/.config/openbox/rc.xml"

XVFB= OBP= DESK= PANEL= NOTI=
cleanup() {
    kill $NOTI $OBP $DESK $PANEL $XVFB 2>/dev/null || :
    rm -rf "$HOME_DIR"
}
trap cleanup EXIT INT TERM

Xvfb $DISP -screen 0 1024x768x24 -nolisten tcp >"$HOME_DIR/xvfb.log" 2>&1 & XVFB=$!
sleep 2
# A session bus, so beat 6 is the REAL org.freedesktop.Notifications server
# taking a real Notify call — not a toast drawn for the camera.
if command -v dbus-daemon >/dev/null 2>&1; then
    DBUS_SESSION_BUS_ADDRESS=$(dbus-daemon --session --print-address --fork)
    export DBUS_SESSION_BUS_ADDRESS
fi
openbox >"$HOME_DIR/ob.log" 2>&1 & OBP=$!
sleep 1
"$BIN/desktop/castalia-desktop" --theme $THEME --repo "$REPO" >/dev/null 2>&1 & DESK=$!
sleep 1
"$BIN/panel/castalia-panel" --theme $THEME --repo "$REPO" >/dev/null 2>&1 & PANEL=$!
sleep 1
if [ -n "${DBUS_SESSION_BUS_ADDRESS:-}" ]; then
    "$BIN/apps/notificaciones/castalia-notificaciones" --theme $THEME \
        --repo "$REPO" >/dev/null 2>&1 & NOTI=$!
fi
sleep 2

# ---- helpers -------------------------------------------------------------
CX=512; CY=300
glide() {  # target_x target_y [steps]
    tx=$1; ty=$2; n=${3:-18}; i=0
    while [ $i -le $n ]; do
        x=$(( CX + (tx-CX)*i/n )); y=$(( CY + (ty-CY)*i/n ))
        xdotool mousemove $x $y; i=$((i+1)); sleep 0.018
    done
    CX=$tx; CY=$ty
}
clickat() { glide "$1" "$2" "${3:-18}"; sleep 0.15; xdotool click 1; sleep 0.3; }
findwin() {  # name -> echoes window id (polls up to ~4s)
    t=0
    while [ $t -lt 40 ]; do
        wid=$(xdotool search --name "$1" 2>/dev/null | head -1)
        [ -n "$wid" ] && { echo "$wid"; return; }
        sleep 0.1; t=$((t+1))
    done
}
winplace() { # name x y  (raise + move so the beat is unobstructed)
    wid=$(findwin "$1")
    [ -n "$wid" ] && { xdotool windowactivate "$wid" 2>/dev/null
        xdotool windowmove "$wid" "$2" "$3" 2>/dev/null
        xdotool windowraise "$wid" 2>/dev/null; }
    sleep 0.5
}
closeapp() { pkill -f "$1" 2>/dev/null || :; sleep 0.7; }   # reliable close
# Takes a wallpaper file name WITH its extension — the shipped default is a
# JPEG, the per-theme ones are SVG.
setwall() { printf 'wallpaper = "branding/wallpapers/%s"\n' "$1" \
    > "$HOME_DIR/.config/castalia/desktop.conf"; }
launch() { "$@" --theme $THEME --repo "$REPO" >/dev/null 2>&1 & sleep 1.6; }

# ---- start recording -----------------------------------------------------
ffmpeg -y -f x11grab -draw_mouse 1 -video_size 1024x768 -framerate 20 \
    -i $DISP -codec:v libx264 -preset veryfast -pix_fmt yuv420p \
    "$OUT/castalia-os-demo.mp4" >"$HOME_DIR/ff.log" 2>&1 & FF=$!
sleep 1.5

# ---- BEAT 1: the Start menu, and typing to search it ---------------------
# "Press the launch key, type, Enter" (§7.3). The menu rises 6 px and fades
# in; the caret is already in the search box, so the typing is the point.
glide 512 300; sleep 0.6
clickat 44 752 22            # the "Castalia" launch button
sleep 1.2                    # menu rises + fades in
glide 150 470 16             # cursor drifts over the app list
sleep 0.8
xdotool type --delay 90 "pint"
sleep 1.3                    # the roster collapses to the match
xdotool key ctrl+a; xdotool key BackSpace; sleep 0.8
xdotool key Escape; sleep 0.5

# ---- BEAT 2: Pintura — draw a couple of strokes, then close --------------
launch "$BIN/apps/paint/castalia-pintura"
winplace "Pintura" 150 60
glide 320 300 14; xdotool mousedown 1
for p in "380 260" "440 320" "500 270" "560 330" "620 285"; do
    set -- $p; glide "$1" "$2" 6
done
xdotool mouseup 1; sleep 0.3
glide 340 400 10; xdotool mousedown 1
for p in "410 430" "480 400" "550 440" "620 405"; do
    set -- $p; glide "$1" "$2" 6
done
xdotool mouseup 1; sleep 1.2
closeapp castalia-pintura

# ---- BEAT 3: Buscaminas — a quick cameo, click a few cells ---------------
launch "$BIN/apps/buscaminas/castalia-buscaminas"
winplace "Buscaminas" 380 170
sleep 0.5
# the grid sits below the counter row; click a spread of it
for p in "430 320" "454 320" "478 344" "502 320" "430 392" "478 416"; do
    set -- $p; clickat "$1" "$2" 8
done
sleep 1.2

# ---- BEAT 4: Alt+Tab — Castalia's own switcher (§7.6) --------------------
# Two windows are open (Notas and Buscaminas), so the card has something to
# show. Hold Alt, tap Tab twice with a beat between them so the selection
# sliding between rows is visible, then let go and the focus really moves.
launch "$BIN/apps/text-editor/castalia-notas"
winplace "Notas" 120 120
sleep 0.8
xdotool keydown alt
xdotool key Tab;  sleep 1.1
xdotool key Tab;  sleep 1.1
xdotool keyup alt
sleep 1.2
closeapp castalia-notas
closeapp castalia-buscaminas

# ---- BEAT 5: the panel clock opens the Calendario, type a note ----------
clickat 980 752 22           # tray clock (far right of the panel)
winplace "Calendario" 300 150
glide 720 330 14; sleep 0.2; xdotool click 1   # focus the notes pane
xdotool type --delay 55 "Cafe con el equipo, 10:30"
sleep 1.4
closeapp castalia-calendario

# ---- BEAT 6: a real notification toast (§7.4) ---------------------------
# The server has been running since startup and owns
# org.freedesktop.Notifications; this is an ordinary Notify call, the same one
# `notify-send` makes. The toast slides in above the panel strut and expires
# on its own.
if [ -n "${DBUS_SESSION_BUS_ADDRESS:-}" ]; then
    "$BIN/apps/notificaciones/castalia-notificaciones" --send \
        "Copia de seguridad terminada" \
        --body "Punto de restauración creado: 'Antes de actualizar'." \
        --app "Centro de recuperación" --icon shield >/dev/null 2>&1 || :
    glide 700 500 16
    sleep 3.2
fi

# ---- BEAT 7: the Centro de redes — Wi-Fi in plain language (§9) ----------
launch "$BIN/apps/redes/castalia-redes" --demo --tab wifi
winplace "Centro de redes" 180 120
sleep 2.4
glide 400 300 14; sleep 1.2      # the network list, signal and security
closeapp castalia-redes

# ---- BEAT 8: Control Center → the wallpaper picker -----------------------
launch "$BIN/apps/control-center/castalia-control-center" --page 1
winplace "Centro de control" 150 60
sleep 0.5
# Hover a wallpaper thumbnail and pick it (this is what writes desktop.conf).
glide 640 340 16; sleep 0.2
setwall marine-deep.svg
xdotool click 1; sleep 1.6
closeapp castalia-control-center

# ---- BEAT 9: the desktop re-skins live, one wallpaper after another -------
# On the bare desktop the live reload is unmistakable — "one switch changes
# everything." (Exactly what the Control Center pick does.)
glide 512 360 18; sleep 1.6           # Marine Deep now showing
setwall olive-dusk.svg;   sleep 2.0
setwall midnight-keep.svg; sleep 2.0
setwall valle-de-castalia.jpg; sleep 2.2   # home, the closing beauty shot
glide 300 300 16; sleep 1.4

# ---- stop ----------------------------------------------------------------
kill -INT $FF 2>/dev/null; wait $FF 2>/dev/null
echo "=== recorded ==="; ls -la "$OUT/castalia-os-demo.mp4"
# extract a frame a second, so a human can flip through and see every beat
# landed rather than trusting that the script's coordinates still line up
ffmpeg -y -i "$OUT/castalia-os-demo.mp4" -vf "fps=1" "$OUT/frame_%02d.png" \
    >/dev/null 2>&1
echo "frames: $(ls "$OUT"/frame_*.png | wc -l)"

if [ "$GIF" -eq 1 ]; then
    # Two passes with a generated palette: a 256-colour GIF quantised on the
    # fly turns the panel gradient into bands. Half size and 12 fps keep it
    # small enough to sit in a README.
    echo "=== gif ==="
    ffmpeg -y -i "$OUT/castalia-os-demo.mp4" \
        -vf "fps=12,scale=512:-1:flags=lanczos,palettegen=stats_mode=diff" \
        "$OUT/palette.png" >/dev/null 2>&1
    ffmpeg -y -i "$OUT/castalia-os-demo.mp4" -i "$OUT/palette.png" \
        -lavfi "fps=12,scale=512:-1:flags=lanczos[v];[v][1:v]paletteuse=dither=bayer:bayer_scale=3" \
        "$OUT/castalia-os-demo.gif" >/dev/null 2>&1
    rm -f "$OUT/palette.png"
    ls -la "$OUT/castalia-os-demo.gif"
fi
