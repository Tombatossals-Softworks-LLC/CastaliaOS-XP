"""Tests for the §16 performance gate (tools/castalia_qa/perf.py).

The gate's own failure mode is the interesting one. A perf gate is a thing
nobody looks at while it is green, so the ways it can be green while measuring
nothing are worth more tests than the arithmetic is.
"""
import io
import json
import sys
import tempfile
import unittest
from contextlib import redirect_stdout, redirect_stderr
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))

from castalia_qa import perf  # noqa: E402

REPO = TOOLS.parent


def report(**over):
    m = {
        "shell_pss_mb": 41.0,
        "explorer_pss_mb": 28.0,
        "explorer_launch_ms": 300,
        "control_center_pss_mb": 22.0,
        "control_center_launch_ms": 250,
    }
    m.update(over)
    return {"measurements": m}


def run_main(doc):
    with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as fh:
        json.dump(doc, fh)
        path = fh.name
    out, err = io.StringIO(), io.StringIO()
    with redirect_stdout(out), redirect_stderr(err):
        rc = perf.main([path])
    Path(path).unlink()
    return rc, out.getvalue(), err.getvalue()


class BudgetTableTest(unittest.TestCase):
    def test_floor_numbers_match_the_bible(self):
        # §16.2/§16.3. If a budget is edited here it must be edited there
        # too: §16.4 says raising a FLOOR budget needs sign-off, because the
        # FLOOR is the product's promise.
        expected = {
            "shell_pss_mb": 60, "control_center_pss_mb": 25,
            "explorer_pss_mb": 35, "explorer_launch_ms": 600,
            "control_center_launch_ms": 400,
        }
        self.assertEqual({k: b.floor for k, b in perf.BY_KEY.items()},
                         {k: float(v) for k, v in expected.items()})

    def test_memory_is_gated_at_the_floor_number_itself(self):
        # PSS does not get bigger because a machine is slower, and the runner
        # is amd64 while FLOOR is i686 — our reading is the pessimistic one,
        # so there is no reason to allow slack.
        for b in perf.MEMORY:
            self.assertEqual(b.ci, b.floor, b.key)

    def test_latency_gate_is_looser_than_the_floor_and_says_why(self):
        # A cloud runner is much faster than a Pentium 4; gating at the FLOOR
        # number would be a test that passes for the wrong reason and flakes
        # for another. The docstring has to keep admitting that.
        for b in perf.LATENCY:
            self.assertGreater(b.ci, b.floor, b.key)
        self.assertIn("smoke alarm", perf.__doc__)

    def test_every_budget_is_covered_by_a_measurement_key(self):
        harness = (REPO / "tests" / "perf" / "measure.sh") \
            .read_text(encoding="utf-8")
        for b in perf.BUDGETS:
            self.assertIn(f'"{b.key}"', harness,
                          f"{b.key} is gated but measure.sh never emits it")


class EvaluateTest(unittest.TestCase):
    def test_a_good_report_passes(self):
        rc, out, _ = run_main(report())
        self.assertEqual(rc, 0)
        self.assertIn("perf-gate: OK", out)

    def test_memory_over_budget_fails(self):
        rc, _, err = run_main(report(shell_pss_mb=61.0))
        self.assertEqual(rc, 1)
        self.assertIn("Shell alone", err)

    def test_exactly_on_budget_passes(self):
        rc, _, _ = run_main(report(shell_pss_mb=60.0))
        self.assertEqual(rc, 0)

    def test_a_catastrophic_launch_regression_fails(self):
        # 4 s to open Explorer: well past even the loose CI ceiling. This is
        # the class of regression the latency half exists to catch.
        rc, _, err = run_main(report(explorer_launch_ms=4000))
        self.assertEqual(rc, 1)
        self.assertIn("Explorer launch", err)

    def test_a_launch_slower_than_floor_but_fast_for_a_runner_passes(self):
        rc, _, _ = run_main(report(explorer_launch_ms=800))
        self.assertEqual(rc, 0)


class DoesNotLieTest(unittest.TestCase):
    """The gate must not report success for something it did not measure."""

    def test_a_missing_metric_fails_rather_than_passing(self):
        doc = report()
        del doc["measurements"]["explorer_launch_ms"]
        rc, _, err = run_main(doc)
        self.assertEqual(rc, 1)
        self.assertIn("not measured", err)
        self.assertIn("explorer_launch_ms", err)

    def test_the_harness_failure_sentinel_fails(self):
        # measure.sh writes -1 when an app never mapped a window. That must
        # not sail through as "0 ms, well under budget".
        rc, _, err = run_main(report(explorer_launch_ms=-1))
        self.assertEqual(rc, 1)
        self.assertIn("not measured", err)

    def test_an_empty_report_fails_every_budget(self):
        rc, _, err = run_main({"measurements": {}})
        self.assertEqual(rc, 1)
        self.assertIn(f"{len(perf.BUDGETS)} metric(s) were not measured", err)

    def test_a_non_numeric_value_is_not_a_pass(self):
        rc, _, err = run_main(report(shell_pss_mb="lots"))
        self.assertEqual(rc, 1)
        self.assertIn("not measured", err)

    def test_the_report_names_what_it_does_not_cover(self):
        # Everything §16 asks for that this gate cannot reach has to appear in
        # the output. A gate that covers half the budgets while printing an
        # unqualified green tick is how a budget quietly stops being enforced.
        _, out, _ = run_main(report())
        self.assertIn("Not covered by this gate", out)
        for note in perf.NOT_MEASURED_YET:
            self.assertIn(note.split(" — ")[0], out)
        self.assertTrue(any("boot time" in n for n in perf.NOT_MEASURED_YET))


if __name__ == "__main__":
    unittest.main()
