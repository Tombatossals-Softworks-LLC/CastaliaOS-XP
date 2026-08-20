# Where the §16.2 memory floor actually is

*Measured 2026-08-20. Raw numbers from CI runs 32381205320 (amd64) and
32384615318 (i386). Reproduce with `sh tests/run.sh perf`, or for the 32-bit
column `docker run --rm -v "$PWD:/src" -w /src i386/debian:bookworm sh
ci/perf-i386.sh`.*

The §16 gate went red on its first run. This is what was behind it, written
down here because §16.4 makes changing a FLOOR budget a signed-off decision
and a decision needs evidence that outlives a CI log.

## The measurements

PSS, at 800×600 with animations off — the FLOOR configuration §16 brackets.
"Own cost" is the app's figure minus an empty Castalia window.

| | amd64 | i386 | own cost (i386) | §16.2 budget | |
|---|---|---|---|---|---|
| Empty window (the clock) | 31.0 MB | 29.7 MB | — | *none* | |
| Control Center | 31.8 MB | 29.8 MB | **+0.1 MB** | 25 MB | ✗ |
| Explorer | 32.5 MB | 32.1 MB | **+2.4 MB** | 35 MB | ✓ |
| Panel | 38.3 MB | 36.6 MB | **+6.9 MB** | — | |
| Desktop plane | 38.8 MB | 37.3 MB | **+7.6 MB** | — | |
| Shell (panel + desktop) | 77.1 MB | 73.9 MB | — | 60 MB | ✗ |

## What it says

**The floor is the toolkit, not our code.** An empty Castalia window — Qt5,
libcastalia-ui, the theme QSS, one window, no work — costs 29.7 MB. Against
that, the Control Center's nine settings panels add 0.1 MB and Explorer adds
2.4 MB. There is no plausible optimisation of a program whose own footprint is
one hundred kilobytes.

**The 25 MB Control Center budget is below that floor.** An empty window is
119% of it. It is not a demanding target; it is an unreachable one, and it was
unreachable on the day it was written.

**The 60 MB shell budget is exactly the floor.** Two Qt windows sharing one
copy of the libraries cost about 60 MB between them before either does
anything. Panel and desktop are the only components with a real own cost
(~7 MB each), so even reducing both to nothing lands on the budget with zero
margin.

**The architecture is not the explanation.** This was the leading hypothesis —
FLOOR is i686 and the gate runs on amd64, so 64-bit pointers looked like the
obvious culprit. Building the shell for i386 and measuring it moves PSS by 4%
(RSS by ~10%): real, and nowhere near enough. Qt's resident set is mostly code
and toolkit data, which do not halve with the pointer width.

**Explorer passes on luck.** Its 35 MB budget clears the 29.7 MB floor by
5.3 MB, and Explorer happens to use 2.4 of that. It is not evidence that the
budgets are sound.

## What this does not decide

Nothing here changes a budget, and the gate does not either. Per §16.4 that is
a signed-off decision, because the FLOOR *is* the product's promise. The
options it leaves open, none of which this evidence picks:

1. **Re-budget §16.2 for the toolkit that was chosen.** The per-app numbers
   would start from a measured empty-window cost instead of an estimate.
   Cheapest, and it makes the budgets mean something again.
2. **Change the metric.** §16.2's column is headed RSS; the gate reads PSS
   because summing RSS across the shell double-counts the shared Qt. In RSS
   the same processes read 72–74 MB each, so the two readings tell very
   different stories and the Bible should say which one it means.
3. **Reopen §12.2.** The Qt5-vs-GTK3 decision was taken on other grounds, and
   this is the first measurement of what it costs per window. Revisiting it
   this late would be a large call, and it is listed for completeness rather
   than recommended.
4. **Accept the breach as a known deviation**, recorded with a date and a
   reason, rather than a red build nobody can act on.

Whichever is taken, the §16.1 boot budgets and the §16.3 menu latencies are
still unmeasured (the gate prints that on every run). They need the FLOOR QEMU
image driven end to end, which does not exist yet.
