#!/bin/sh
# session-smoke.sh — end-to-end test of the REAL session entry point
# (Bible §6.6, §7.1). The live ISO boots into `castalia-live-session`, which execs
# `castalia-session` under X. This test runs that exact script — not the
# pieces it launches — against a staged /opt/castalia-style prefix on a real
# X server, and asserts the contract the desktop depends on:
#
#   1. the session brings up the WM (EWMH check window appears),
#   2. the shell planes map: a DESKTOP window (wallpaper) and a DOCK (panel),
#   3. the CASTALIA_DEMO welcome window opens (the live first impression),
#   4. SUPERVISION: killing the panel process gets it restarted ~1 s later
#      (a crash never blacks out the desktop), and
#   5. SIGTERM ends the session script itself (clean logout), and
#   6. SAFE MODE: relaunched with castalia.safemode=1's effect, the session
#      hands its children reduced-motion/silent/high-contrast and leaves the
#      optional services unstarted (§6.2).
#
# Usage:
#   sh tests/e2e/session-smoke.sh [--bindir DIR] [--repo PATH] [--display :N]
#
# Exit 0 = the session honoured the contract. Requires: Xvfb, openbox, xprop.

set -u

BINDIR=build/out/shell-build
REPO=.
DISP=:95
while [ $# -gt 0 ]; do
    case "$1" in
        --bindir)  BINDIR=${2:?}; shift 2 ;;
        --repo)    REPO=${2:?}; shift 2 ;;
        --display) DISP=${2:?}; shift 2 ;;
        *) echo "session-smoke: unknown option: $1" >&2; exit 2 ;;
    esac
done

REPO=$(cd "$REPO" && pwd)
BINDIR=$(cd "$BINDIR" && pwd)
DNUM=${DISP#:}

for tool in Xvfb openbox xprop; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "session-smoke: missing required tool: $tool" >&2; exit 2; }
done
for b in panel/castalia-panel desktop/castalia-desktop \
         apps/welcome/castalia-bienvenida \
         apps/notificaciones/castalia-notificaciones; do
    [ -x "$BINDIR/$b" ] || {
        echo "session-smoke: missing binary: $BINDIR/$b (build the shell)" >&2
        exit 2; }
done

HOME_DIR=$(mktemp -d)
PREFIX_DIR=$(mktemp -d)
XVFB= SESSION=
cleanup() {
    kill "$SESSION" 2>/dev/null || :
    # the session's children survive it by design (X teardown reaps them on
    # a real system); here WE are the X teardown.
    pkill -f "$PREFIX_DIR/bin/" 2>/dev/null || :
    pkill -f "openbox.*$DISP" 2>/dev/null || :
    kill "$XVFB" 2>/dev/null || :
    rm -rf "$HOME_DIR" "$PREFIX_DIR"
}
trap cleanup EXIT INT TERM

# Stage the installed-system layout the session expects (§17.2 / the desktop
# ISO hook): $PREFIX/bin with the shell binaries, assets read from the repo.
mkdir -p "$PREFIX_DIR/bin"
cp "$BINDIR/panel/castalia-panel"            "$PREFIX_DIR/bin/castalia-panel"
cp "$BINDIR/desktop/castalia-desktop"        "$PREFIX_DIR/bin/castalia-desktop"
cp "$BINDIR/apps/welcome/castalia-bienvenida" \
   "$PREFIX_DIR/bin/castalia-bienvenida"
cp "$BINDIR/apps/notificaciones/castalia-notificaciones" \
   "$PREFIX_DIR/bin/castalia-notificaciones"

# Simulate the INSTALLED greeter path (Exec=castalia-session from an
# xsession), NOT the live path: a display manager launches the session with
# no CASTALIA_REPO in the environment. The only way the session can find the
# asset tree is its own $PREFIX/share/castalia fallback — so stage that
# symlink exactly as packages/mkdeb.sh ships it. If the session fails to
# resolve AND export the repo, the panel's Start Menu would launch apps with
# "--repo ." (CastaliaMenu.cpp reads CASTALIA_REPO from the environment).
mkdir -p "$PREFIX_DIR/share"
ln -sf "$REPO" "$PREFIX_DIR/share/castalia"

export DISPLAY="$DISP"
export HOME="$HOME_DIR"
export XDG_RUNTIME_DIR="$HOME_DIR"
export CASTALIA_PREFIX="$PREFIX_DIR"
unset CASTALIA_REPO           # the greeter does not set it — the session must
export CASTALIA_DEMO=1
export QT_QPA_PLATFORM=xcb

echo "session-smoke: starting X server on $DISP"
Xvfb "$DISP" -screen 0 1024x768x24 -nolisten tcp >"$HOME_DIR/xvfb.log" 2>&1 &
XVFB=$!
i=0
while [ ! -e "/tmp/.X11-unix/X$DNUM" ] && [ $i -lt 100 ]; do
    sleep 0.1; i=$((i + 1))
done
[ -e "/tmp/.X11-unix/X$DNUM" ] || { echo "session-smoke: X did not start" >&2; exit 1; }
sleep 0.5

echo "session-smoke: launching castalia-session (the real entry point)"
sh "$REPO/shell/session/castalia-session" >"$HOME_DIR/session.log" 2>&1 &
SESSION=$!

# 1. the WM comes up
i=0
while ! xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null | grep -q window; do
    sleep 0.2; i=$((i + 1))
    [ $i -lt 75 ] || { echo "session-smoke: FAIL — no EWMH WM in 15s" >&2
                       tail -20 "$HOME_DIR/session.log" >&2; exit 1; }
done
echo "session-smoke: WM is up (EWMH check window present)"

# window-type census over _NET_CLIENT_LIST
count_type() {
    want=$1
    ids=$(xprop -root _NET_CLIENT_LIST 2>/dev/null | sed 's/.*#//; s/,//g')
    n=0
    for id in $ids; do
        ty=$(xprop -id "$id" _NET_WM_WINDOW_TYPE 2>/dev/null || true)
        case "$ty" in *"$want"*) n=$((n + 1)) ;; esac
    done
    echo "$n"
}
wait_type() {  # wait_type TYPE MIN BUDGET_TENTHS
    i=0
    while [ $i -lt "$3" ]; do
        [ "$(count_type "$1")" -ge "$2" ] && return 0
        sleep 0.2; i=$((i + 1))
    done
    return 1
}

# 2. desktop plane + dock map
wait_type _NET_WM_WINDOW_TYPE_DESKTOP 1 75 || {
    echo "session-smoke: FAIL — no DESKTOP window (wallpaper plane)" >&2; exit 1; }
wait_type _NET_WM_WINDOW_TYPE_DOCK 1 75 || {
    echo "session-smoke: FAIL — no DOCK window (panel)" >&2; exit 1; }
echo "session-smoke: shell planes mapped (desktop + panel)"

# 2b. The panel must have INHERITED an exported CASTALIA_REPO pointing at the
# real asset tree — this is what its Start Menu hands to every app it spawns.
# Read it straight from the live process environment (/proc/PID/environ).
panel_pid=$(pgrep -f "$PREFIX_DIR/bin/castalia-panel" | head -n 1)
[ -n "$panel_pid" ] || { echo "session-smoke: FAIL — no panel process" >&2; exit 1; }
panel_repo=$(tr '\0' '\n' < "/proc/$panel_pid/environ" 2>/dev/null \
    | sed -n 's/^CASTALIA_REPO=//p')
if [ -z "$panel_repo" ] || [ ! -d "$panel_repo/themes" ]; then
    echo "session-smoke: FAIL — panel env CASTALIA_REPO='$panel_repo' does not" \
         "resolve to the asset tree; Start-Menu apps would get '--repo .'" >&2
    exit 1
fi
echo "session-smoke: panel inherited CASTALIA_REPO=$panel_repo (Start Menu OK)"

# 3. the demo welcome window appears (launched ~3 s into the session)
wait_type _NET_WM_WINDOW_TYPE_NORMAL 1 100 || {
    echo "session-smoke: FAIL — demo welcome window never appeared" >&2; exit 1; }
echo "session-smoke: welcome window mapped (CASTALIA_DEMO)"

# 4. supervision: kill the panel, expect a NEW panel process + dock back
old_pid=$(pgrep -f "$PREFIX_DIR/bin/castalia-panel" | head -n 1)
[ -n "$old_pid" ] || { echo "session-smoke: FAIL — no panel process found" >&2
                       exit 1; }
echo "session-smoke: killing the panel (pid $old_pid) to test supervision"
kill -9 "$old_pid"
i=0
new_pid=
while [ $i -lt 75 ]; do
    new_pid=$(pgrep -f "$PREFIX_DIR/bin/castalia-panel" | head -n 1)
    [ -n "$new_pid" ] && [ "$new_pid" != "$old_pid" ] && break
    sleep 0.2; i=$((i + 1))
done
{ [ -n "$new_pid" ] && [ "$new_pid" != "$old_pid" ]; } || {
    echo "session-smoke: FAIL — panel was not restarted after a crash" >&2
    tail -20 "$HOME_DIR/session.log" >&2; exit 1; }
wait_type _NET_WM_WINDOW_TYPE_DOCK 1 75 || {
    echo "session-smoke: FAIL — restarted panel never re-mapped its dock" >&2
    exit 1; }
echo "session-smoke: supervision works (panel respawned as pid $new_pid)"

# 5. clean logout: TERM the session script, it must exit
kill -TERM "$SESSION"
i=0
while kill -0 "$SESSION" 2>/dev/null && [ $i -lt 75 ]; do
    sleep 0.2; i=$((i + 1))
done
kill -0 "$SESSION" 2>/dev/null && {
    echo "session-smoke: FAIL — session still running 15s after SIGTERM" >&2
    exit 1; }
SESSION=
echo "session-smoke: session exited on SIGTERM (clean logout)"

# ---------------------------------------------------------------- 6. safe --
# The GRUB "Modo seguro" entry (iso/grub/11_castalia_safe) boots with
# castalia.safemode=1, which castalia-session turns into a stripped session.
# Assert it from OUTSIDE the script — the log it prints, and the environment
# its children actually inherit — because the whole point of Safe Mode is what
# the processes end up doing, not what the script says it will do.
echo "session-smoke: restarting the session in SAFE MODE"
pkill -f "$PREFIX_DIR/bin/" 2>/dev/null || :
pkill -f "openbox.*$DISP" 2>/dev/null || :
sleep 1
CASTALIA_SAFE_MODE=1 sh "$REPO/shell/session/castalia-session" \
    >"$HOME_DIR/safe.log" 2>&1 &
SESSION=$!

i=0
safe_pid=
while [ $i -lt 100 ]; do
    safe_pid=$(pgrep -f "$PREFIX_DIR/bin/castalia-panel" | head -n 1)
    [ -n "$safe_pid" ] && break
    sleep 0.2; i=$((i + 1))
done
[ -n "$safe_pid" ] || { echo "session-smoke: FAIL — no panel in safe mode" >&2
                        tail -20 "$HOME_DIR/safe.log" >&2; exit 1; }

safe_env=$(tr '\0' '\n' < "/proc/$safe_pid/environ" 2>/dev/null)
for var in CASTALIA_SAFE_MODE=1 CASTALIA_REDUCE_MOTION=1 CASTALIA_NO_SOUND=1 \
           CASTALIA_THEME=high-contrast; do
    printf '%s\n' "$safe_env" | grep -qx "$var" || {
        echo "session-smoke: FAIL — safe mode did not export $var to the shell" >&2
        tail -20 "$HOME_DIR/safe.log" >&2; exit 1; }
done
echo "session-smoke: safe mode exported reduced-motion, silent, high-contrast"

# §6.2 "minimal services": the notification server is staged in $PREFIX/bin and
# is started on a normal boot, so its absence here is a decision, not a gap.
sleep 2
if pgrep -f "$PREFIX_DIR/bin/castalia-notificaciones" >/dev/null 2>&1; then
    echo "session-smoke: FAIL — safe mode started the notification server" >&2
    exit 1
fi
echo "session-smoke: safe mode left the optional services alone"

kill -TERM "$SESSION"
i=0
while kill -0 "$SESSION" 2>/dev/null && [ $i -lt 75 ]; do
    sleep 0.2; i=$((i + 1))
done
kill -0 "$SESSION" 2>/dev/null && {
    echo "session-smoke: FAIL — safe-mode session ignored SIGTERM" >&2; exit 1; }
SESSION=

echo "session-smoke: PASS — boot, shell, demo, supervision, safe mode, logout"
exit 0
