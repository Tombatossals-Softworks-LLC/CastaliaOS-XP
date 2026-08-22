#!/bin/sh
# measure.sh — take the §16 numbers off real binaries (Bible §16.4, §19.1).
#
# Launches the shell and the measured apps under a real X server and writes a
# JSON report for `python3 -m castalia_qa.perf` to score. Two things are
# measured, and both are measured the way a user would experience them:
#
#   * MEMORY, as PSS rather than RSS. The panel and the desktop plane are two
#     processes sharing one copy of Qt; adding their RSS counts that copy
#     twice and invents ~20 MB the machine never spent. PSS divides shared
#     pages among the processes actually sharing them, which is the number
#     that answers "how much of this 512 MB is gone".
#   * LAUNCH LATENCY, as the wall clock from exec to the window appearing in
#     _NET_CLIENT_LIST — "window visible" in §16.3's words, not "process
#     started". Best of N runs: the first launch pays for a cold page cache
#     that a real desktop pays once per boot, and the minimum is far steadier
#     than the mean on a shared runner.
#
# Usage: sh tests/perf/measure.sh [--bindir DIR] [--repo PATH] [--display :N]
#                                 [--runs N] [--out FILE]
#
# Requires: Xvfb, openbox, xprop. Exit 0 = a report was written (it does not
# judge the numbers; castalia_qa.perf does that).

set -u

BINDIR=build/out/shell-build
REPO=.
DISP=:97
RUNS=3
OUT=-
while [ $# -gt 0 ]; do
    case "$1" in
        --bindir)  BINDIR=${2:?}; shift 2 ;;
        --repo)    REPO=${2:?}; shift 2 ;;
        --display) DISP=${2:?}; shift 2 ;;
        --runs)    RUNS=${2:?}; shift 2 ;;
        --out)     OUT=${2:?}; shift 2 ;;
        *) echo "measure: unknown option: $1" >&2; exit 2 ;;
    esac
done

REPO=$(cd "$REPO" && pwd)
BINDIR=$(cd "$BINDIR" && pwd)
DNUM=${DISP#:}

for tool in Xvfb openbox xprop; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "measure: missing required tool: $tool" >&2; exit 2; }
done

PANEL="$BINDIR/panel/castalia-panel"
DESKTOP="$BINDIR/desktop/castalia-desktop"
EXPLORER="$BINDIR/explorer/castalia-explorer"
CONTROL="$BINDIR/apps/control-center/castalia-control-center"
# The smallest first-party window there is: a clock face and nothing else,
# standing in for what a Castalia window costs BEFORE it does anything — Qt5,
# libcastalia-ui, the theme QSS, one window. Every §16.2 app budget is that
# number plus the app's own work, so it is the only way to tell "this app is
# heavy" from "the floor is here". §16.5 (2026-08-22) promoted it from a
# diagnostic to a budget of its own: it is the row where a memory regression
# that hits every app at once actually shows up.
BASELINE="$BINDIR/apps/clock/castalia-reloj"
for b in "$PANEL" "$DESKTOP" "$EXPLORER" "$CONTROL" "$BASELINE"; do
    [ -x "$b" ] || { echo "measure: missing binary: $b" >&2; exit 2; }
done

WORK=$(mktemp -d)
XVFB=
cleanup() {
    pkill -f "$BINDIR/" 2>/dev/null || :
    pkill -f "openbox.*$DISP" 2>/dev/null || :
    if [ -n "$XVFB" ]; then kill "$XVFB" 2>/dev/null || :; fi
    rm -rf "$WORK"
}
trap cleanup EXIT INT TERM

export DISPLAY="$DISP"
export HOME="$WORK"
export XDG_RUNTIME_DIR="$WORK"
export CASTALIA_REPO="$REPO"
export QT_QPA_PLATFORM=xcb
# Measure the desktop the FLOOR machine actually runs: §16 brackets FLOOR at
# 800x600 with the compositor off and animations off (§16.3 — "animations off
# on FLOOR"), so an animated first paint is not what we are timing.
export CASTALIA_REDUCE_MOTION=1
export CASTALIA_NO_SOUND=1

Xvfb "$DISP" -screen 0 800x600x24 -nolisten tcp >"$WORK/xvfb.log" 2>&1 &
XVFB=$!
i=0
while [ ! -e "/tmp/.X11-unix/X$DNUM" ] && [ $i -lt 100 ]; do
    sleep 0.1; i=$((i + 1))
done
[ -e "/tmp/.X11-unix/X$DNUM" ] || { echo "measure: X did not start" >&2; exit 1; }
openbox >"$WORK/openbox.log" 2>&1 &
sleep 1

now_ms() { date +%s%3N; }

# Every window id currently on _NET_CLIENT_LIST, one per line.
client_list() {
    xprop -root _NET_CLIENT_LIST 2>/dev/null | sed 's/.*#//; s/,//g'
}

# PSS of a process in MB, to one decimal. smaps_rollup is one read for the
# whole address space; without it (older kernels) fall back to RSS and say so
# by reporting a value that is, if anything, too high.
pss_mb() {
    _p=$1
    if [ -r "/proc/$_p/smaps_rollup" ]; then
        awk '/^Pss:/ {print $2/1024; exit}' "/proc/$_p/smaps_rollup"
    elif [ -r "/proc/$_p/statm" ]; then
        awk '{printf "%.3f", $2 * 4 / 1024}' "/proc/$_p/statm"
    else
        echo 0
    fi
}

# RSS of a process in MB. The Bible's §16.2 column says RSS; we gate on PSS
# (see castalia_qa/perf.py) but report both, because they answer different
# questions and the gap between them is itself information.
rss_mb() {
    if [ -r "/proc/$1/statm" ]; then
        awk '{printf "%.1f", $2 * 4 / 1024}' "/proc/$1/statm"
    else
        echo 0
    fi
}

# Sum the PSS of every process matching a pattern.
pss_sum_mb() {
    total=0
    for p in $(pgrep -f "$1" 2>/dev/null); do
        v=$(pss_mb "$p")
        total=$(awk -v a="$total" -v b="$v" 'BEGIN{print a + b}')
    done
    printf '%.1f' "$total"
}

# launch BIN ARGS... -> prints "<ms> <pss_mb>"; empty on failure.
# Waits for a window that was NOT there before, so a stray window from an
# earlier measurement cannot be mistaken for this one's first paint.
launch() {
    _bin=$1; shift
    before=$(client_list | tr '\n' ' ')
    t0=$(now_ms)
    "$_bin" --theme classic --repo "$REPO" "$@" >"$WORK/app.log" 2>&1 &
    _pid=$!
    _ms=
    j=0
    while [ $j -lt 600 ]; do          # 600 * ~10 ms poll = ~6 s ceiling
        for id in $(client_list); do
            case " $before " in
                *" $id "*) continue ;;
            esac
            _ms=$(( $(now_ms) - t0 ))
            break
        done
        [ -n "$_ms" ] && break
        kill -0 "$_pid" 2>/dev/null || break
        j=$((j + 1))
    done
    if [ -z "$_ms" ]; then
        kill "$_pid" 2>/dev/null || :
        wait "$_pid" 2>/dev/null || :
        return 1
    fi
    # Let it settle before reading memory: the number that matters is the one
    # after the window is up and idle, not mid-construction.
    sleep 1
    _pss=$(pss_mb "$_pid")
    kill "$_pid" 2>/dev/null || :
    wait "$_pid" 2>/dev/null || :
    sleep 0.5
    printf '%s %.1f' "$_ms" "$_pss"
}

# best_of RUNS BIN ARGS... -> prints "<min_ms> <pss_at_min>"
best_of() {
    _runs=$1; shift
    _bm=; _bp=
    k=0
    while [ "$k" -lt "$_runs" ]; do
        k=$((k + 1))
        out=$(launch "$@") || continue
        m=${out%% *}; p=${out##* }
        if [ -z "$_bm" ] || [ "$m" -lt "$_bm" ]; then _bm=$m; _bp=$p; fi
    done
    [ -n "$_bm" ] || return 1
    printf '%s %s' "$_bm" "$_bp"
}

echo "measure: shell (panel + desktop plane) at 800x600, animations off" >&2
"$DESKTOP" --theme classic --repo "$REPO" >"$WORK/desktop.log" 2>&1 &
"$PANEL"   --theme classic --repo "$REPO" >"$WORK/panel.log"   2>&1 &
i=0
while [ $i -lt 150 ]; do
    n=$(client_list | grep -c . || echo 0)
    [ "$n" -ge 2 ] && break
    sleep 0.2; i=$((i + 1))
done
sleep 2                       # let both planes settle before reading memory
SHELL_PSS=$(pss_sum_mb "$BINDIR/(panel|desktop)/castalia-")
# Per-process, so a breach says WHICH plane grew rather than just that one did.
PANEL_PID=$(pgrep -f "$BINDIR/panel/castalia-panel" | head -n 1)
DESKTOP_PID=$(pgrep -f "$BINDIR/desktop/castalia-desktop" | head -n 1)
PANEL_PSS=$([ -n "${PANEL_PID:-}" ] && pss_mb "$PANEL_PID" || echo 0)
DESKTOP_PSS=$([ -n "${DESKTOP_PID:-}" ] && pss_mb "$DESKTOP_PID" || echo 0)
PANEL_RSS=$([ -n "${PANEL_PID:-}" ] && rss_mb "$PANEL_PID" || echo 0)
DESKTOP_RSS=$([ -n "${DESKTOP_PID:-}" ] && rss_mb "$DESKTOP_PID" || echo 0)
printf 'measure:   shell PSS = %s MB (panel %.1f, desktop %.1f; RSS %s / %s)\n' \
    "$SHELL_PSS" "$PANEL_PSS" "$DESKTOP_PSS" "$PANEL_RSS" "$DESKTOP_RSS" >&2

echo "measure: Explorer, best of $RUNS" >&2
if EX=$(best_of "$RUNS" "$EXPLORER" --demo); then
    EX_MS=${EX%% *}; EX_PSS=${EX##* }
else
    EX_MS=-1; EX_PSS=-1
fi
echo "measure:   explorer = ${EX_MS} ms, ${EX_PSS} MB" >&2

echo "measure: toolkit floor (smallest window: the clock), best of $RUNS" >&2
if BL=$(best_of "$RUNS" "$BASELINE"); then
    BL_MS=${BL%% *}; BL_PSS=${BL##* }
else
    BL_MS=-1; BL_PSS=-1
fi
echo "measure:   toolkit floor = ${BL_MS} ms, ${BL_PSS} MB" >&2

echo "measure: Control Center, best of $RUNS" >&2
if CC=$(best_of "$RUNS" "$CONTROL"); then
    CC_MS=${CC%% *}; CC_PSS=${CC##* }
else
    CC_MS=-1; CC_PSS=-1
fi
echo "measure:   control-center = ${CC_MS} ms, ${CC_PSS} MB" >&2

REPORT=$(cat <<EOF
{
  "screen": "800x600",
  "runs": $RUNS,
  "diagnostics": {
    "panel_pss_mb": $PANEL_PSS,
    "desktop_pss_mb": $DESKTOP_PSS,
    "panel_rss_mb": $PANEL_RSS,
    "desktop_rss_mb": $DESKTOP_RSS
  },
  "measurements": {
    "toolkit_floor_pss_mb": $BL_PSS,
    "shell_pss_mb": $SHELL_PSS,
    "explorer_pss_mb": $EX_PSS,
    "explorer_launch_ms": $EX_MS,
    "control_center_pss_mb": $CC_PSS,
    "control_center_launch_ms": $CC_MS
  }
}
EOF
)

if [ "$OUT" = "-" ]; then
    printf '%s\n' "$REPORT"
else
    printf '%s\n' "$REPORT" > "$OUT"
    echo "measure: wrote $OUT" >&2
fi
exit 0
