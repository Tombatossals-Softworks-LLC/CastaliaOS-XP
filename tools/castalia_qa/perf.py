"""The §16 performance budgets, enforced as code (Bible §16.4, §19.1).

§16 opens with "budgets are **enforced in CI** ... not merely aspired to", and
until now nothing measured any of them. This is the half that can be measured
per-commit: memory, and the launch latencies that do not need a synthetic
input event. Boot time and menu-open latency are **not** here — see
:data:`NOT_MEASURED_YET` — and saying so in the report is deliberate, because
a perf gate that quietly covers a third of the budgets while printing a green
tick is worse than no gate at all.

Measurement lives in ``tests/perf/measure.sh``, which drives real binaries
under a real X server and writes a JSON report. Everything here is a pure
function of that report, so the rules are unit-testable without a desktop.

Why the numbers still mean something when they are read on a CI runner rather
than on a Pentium 4:

* **Memory** is the honest one. RSS/PSS depends on the toolkit and the word
  size, not on how fast the machine is, and the runner is amd64 while FLOOR is
  i686 — 64-bit pointers make our reading the *pessimistic* one. Under budget
  here means under budget there.
  One honest caveat, flagged rather than resolved here: §16.2's column is
  headed *RSS*, and this gates *PSS*. For the shell row that is unavoidable —
  the panel and the desktop share one copy of Qt, and adding their RSS counts
  it twice — but for a single app PSS is the more generous of the two, so a
  breach measured this way is a real breach and then some. Whether the §16.2
  numbers were ever meant as RSS on a 64-bit build is a budget question, and
  §16.4 makes budget changes a signed-off decision on purpose. The report
  prints both numbers so the decision can be made on evidence.

* **Latency** is not. A cloud runner is many times faster than the FLOOR
  machine, so passing here says nothing about passing there; §19.2 keeps that
  promise on real hardware. What this catches is the regression that turns a
  300 ms launch into a 4-second one — a class of bug that is otherwise found
  by a user. It is a smoke alarm, not a stopwatch.
"""
from __future__ import annotations

import json
import sys
from dataclasses import dataclass

#: Budgets that this gate does NOT cover, named so the report can say so.
#: §16.4's full gate wants a QEMU FLOOR image booting and being driven; these
#: need that harness (or an instrumentation hook in the panel) to exist first.
NOT_MEASURED_YET = (
    "boot time (GRUB -> greeter -> desktop, §16.1) — needs the FLOOR QEMU "
    "image driven end to end",
    "launch-menu open and menu-search latency (§16.3) — needs the panel to "
    "report its own first paint; there is no way to click it from a script",
    "idle desktop RSS including kernel and services (§16.2) — this measures "
    "the shell processes, not a booted system",
    "Explorer 1k-entry listing, Alt+Tab and drag latency (§16.3)",
)


@dataclass(frozen=True)
class Budget:
    """One row of §16.2/§16.3.

    *floor* is the number the Bible commits to on the FLOOR machine. *ci* is
    the ceiling this gate actually enforces, which for latency is deliberately
    looser (see the module docstring): the runner is not a Pentium 4, and
    pretending otherwise would either flake or lie.
    """

    key: str
    what: str
    unit: str
    floor: float
    ci: float
    section: str

    def breach(self, value: float) -> bool:
        return value > self.ci


#: §16.2 — memory. Enforced at the FLOOR number itself: see the docstring.
MEMORY = (
    Budget("shell_pss_mb", "Shell alone (panel + desktop + session)", "MB",
           floor=60, ci=60, section="§16.2"),
    Budget("control_center_pss_mb", "Control Center", "MB",
           floor=25, ci=25, section="§16.2"),
    Budget("explorer_pss_mb", "Explorer window", "MB",
           floor=35, ci=35, section="§16.2"),
)

#: §16.3 — launch latency. The CI ceiling is 4x the FLOOR budget: a runner
#: should beat FLOOR by more than that, so anything at this level is a real
#: regression rather than a noisy neighbour. Tightening it to the FLOOR
#: number would be a gate that fails for reasons that are not the code's.
LATENCY = (
    Budget("explorer_launch_ms", "Explorer launch (window visible)", "ms",
           floor=600, ci=2400, section="§16.3"),
    Budget("control_center_launch_ms", "Control Center open", "ms",
           floor=400, ci=1600, section="§16.3"),
)

BUDGETS = MEMORY + LATENCY
BY_KEY = {b.key: b for b in BUDGETS}


@dataclass(frozen=True)
class Result:
    budget: Budget
    value: float | None      # None when the harness could not measure it

    @property
    def missing(self) -> bool:
        return self.value is None

    @property
    def failed(self) -> bool:
        return self.value is not None and self.budget.breach(self.value)

    @property
    def headroom_pct(self) -> float | None:
        """How far under the FLOOR budget we are, as a percentage."""
        if self.value is None or not self.budget.floor:
            return None
        return (1.0 - self.value / self.budget.floor) * 100.0


def evaluate(report: dict) -> list[Result]:
    """Score a measurement report against every budget, in declared order.

    A key the harness did not produce is a :attr:`Result.missing`, not a pass:
    a measurement that silently stopped happening must not read as a green
    tick, which is the failure mode this whole module exists to avoid.
    """
    measured = report.get("measurements", {})
    out = []
    for budget in BUDGETS:
        raw = measured.get(budget.key)
        value = None
        if isinstance(raw, (int, float)) and raw >= 0:
            value = float(raw)
        out.append(Result(budget, value))
    return out


def format_diagnostics(report: dict) -> str:
    """Numbers the gate does not judge but a person reading a breach wants.

    A failing total tells you the shell grew; it does not tell you which
    plane grew, or whether the gap between PSS and RSS is where the argument
    actually is. Both belong next to the verdict, not in a separate hunt.
    """
    diag = report.get("diagnostics") or {}
    if not diag:
        return ""
    rows = "  ".join(f"{k.replace('_mb', '')}={v}MB"
                     for k, v in sorted(diag.items()))
    out = [f"\nBreakdown (not gated): {rows}"]

    # The baseline turns "this app is over budget" into an answerable
    # question. Every §16.2 app number is the cost of one Castalia window
    # plus what that app does; if the first term alone is already near the
    # budget, no amount of work on the second term will get under it, and
    # the honest response is a §16.4 budget conversation rather than a
    # doomed optimisation.
    base = diag.get("baseline_window_pss_mb")
    if isinstance(base, (int, float)) and base > 0:
        out.append(
            f"An empty Castalia window (Qt5 + libcastalia-ui + theme) costs "
            f"{base:.1f}MB before the app does anything.")
        tightest = min((b for b in MEMORY if "shell" not in b.key),
                       key=lambda b: b.floor, default=None)
        if tightest is not None and base > tightest.floor * 0.8:
            out.append(
                f"  That is {base / tightest.floor:.0%} of the tightest app "
                f"budget ({tightest.what}, {tightest.floor:.0f}MB), so that "
                f"budget is set below the toolkit's floor on this build. "
                f"§16.4 makes changing it a signed-off decision — this is "
                f"the evidence for that conversation, not a licence to "
                f"raise it here.")
    return "\n".join(out)


def format_report(results: list[Result]) -> str:
    lines = [
        f"{'metric':<44} {'measured':>10} {'budget':>9} "
        f"{'gate':>9}  status",
        "-" * 88,
    ]
    for r in results:
        b = r.budget
        if r.missing:
            shown, status = "—", "NOT MEASURED"
        else:
            shown = f"{r.value:,.1f}"
            if r.failed:
                status = "OVER BUDGET"
            else:
                head = r.headroom_pct
                status = "ok" + (f"  ({head:+.0f}% vs floor)"
                                 if head is not None else "")
        lines.append(
            f"{b.section + ' ' + b.what:<44} {shown:>10} "
            f"{b.floor:>8,.0f}{b.unit} {b.ci:>8,.0f}{b.unit}  {status}")
    lines.append("")
    lines.append("Not covered by this gate (Bible §16.4 wants all of it):")
    lines += [f"  * {n}" for n in NOT_MEASURED_YET]
    return "\n".join(lines)


def main(argv: list[str]) -> int:
    if len(argv) != 1:
        print("usage: python3 -m castalia_qa.perf REPORT.json", file=sys.stderr)
        return 2
    with open(argv[0], encoding="utf-8") as fh:
        report = json.load(fh)

    results = evaluate(report)
    print(format_report(results))
    diag = format_diagnostics(report)
    if diag:
        print(diag)

    over = [r for r in results if r.failed]
    missing = [r for r in results if r.missing]
    if over:
        print("\nperf-gate: FAIL — %d budget(s) exceeded:" % len(over),
              file=sys.stderr)
        for r in over:
            print(f"  {r.budget.what}: {r.value:,.1f}{r.budget.unit} "
                  f"> {r.budget.ci:,.0f}{r.budget.unit}", file=sys.stderr)
        return 1
    if missing:
        print("\nperf-gate: FAIL — %d metric(s) were not measured:"
              % len(missing), file=sys.stderr)
        for r in missing:
            print(f"  {r.budget.key} ({r.budget.what})", file=sys.stderr)
        return 1
    print(f"\nperf-gate: OK ({len(results)} budget(s) within §16)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
