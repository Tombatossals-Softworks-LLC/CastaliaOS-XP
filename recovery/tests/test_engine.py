"""Unit tests for the Restore Points engine (Bible §9, P8)."""

import unittest

from castalia_recovery.engine import (
    DryRunner,
    RestoreRefused,
    create_point,
    list_points,
    prune,
    restore_argv,
    restore_point,
    snapshot_argv,
)
from castalia_recovery.model import RecoveryConfig


class Argv(unittest.TestCase):
    def setUp(self):
        self.cfg = RecoveryConfig(source_root="/", store_dir="/srv/rp")

    def test_snapshot_first_has_no_linkdest(self):
        a = snapshot_argv(self.cfg, "P1", None)
        self.assertNotIn("--link-dest", a)
        self.assertEqual(a[0], "rsync")
        self.assertIn("--delete", a)
        self.assertTrue(a[-1].endswith("/srv/rp/P1/"))
        self.assertEqual(a[-2], "/")  # source_root "/" (trailing-slash form)

    def test_snapshot_links_to_previous(self):
        a = snapshot_argv(self.cfg, "P2", "P1")
        i = a.index("--link-dest")
        self.assertEqual(a[i + 1], "/srv/rp/P1")

    def test_excludes_user_data(self):
        # A restore point must never capture or roll back /home.
        a = snapshot_argv(self.cfg, "P1", None)
        self.assertIn("/home/*", a)

    def test_restore_argv_direction(self):
        a = restore_argv(self.cfg, "P1")
        self.assertEqual(a[-2], "/srv/rp/P1/")   # from the snapshot
        self.assertEqual(a[-1], "/")             # back onto the system


class Flow(unittest.TestCase):
    def setUp(self):
        self.cfg = RecoveryConfig(source_root="/", store_dir="/srv/rp", keep=2)

    def test_create_writes_metadata_and_snapshots(self):
        r = DryRunner()
        create_point(r, self.cfg, point_id="P1", label="hola",
                     reason="manual", created="2026-07-09 10:00")
        self.assertIn("P1", r.store)
        self.assertIn("label=hola", r.store["P1"])
        self.assertTrue(any(c[0] == "rsync" for c in r.calls))

    def test_second_point_links_to_first(self):
        r = DryRunner()
        create_point(r, self.cfg, point_id="P1")
        create_point(r, self.cfg, point_id="P2")
        rsyncs = [c for c in r.calls if c and c[0] == "rsync"]
        self.assertIn("--link-dest", rsyncs[1])
        self.assertIn("/srv/rp/P1", rsyncs[1])

    def test_list_newest_first(self):
        r = DryRunner()
        for pid in ("20260101-0000", "20260201-0000", "20260301-0000"):
            create_point(r, self.cfg, point_id=pid)
        ids = [p.id for p in list_points(r, self.cfg)]
        self.assertEqual(ids, ["20260301-0000", "20260201-0000",
                               "20260101-0000"])

    def test_prune_keeps_newest(self):
        r = DryRunner()
        for pid in ("A1", "A2", "A3", "A4"):
            create_point(r, self.cfg, point_id=pid)
        removed = prune(r, self.cfg)   # keep=2
        self.assertEqual(sorted(removed), ["A1", "A2"])
        remaining = [p.id for p in list_points(r, self.cfg)]
        # prune's DryRunner rm is recorded but store isn't mutated; assert the
        # rm commands targeted the oldest two.
        rms = [c for c in r.calls if c and c[0] == "rm"]
        self.assertTrue(any("A1" in c[-1] for c in rms))
        self.assertTrue(any("A2" in c[-1] for c in rms))
        self.assertEqual(len(remaining), 4)  # store not mutated by fake rm

    def test_restore_refused_without_confirm(self):
        r = DryRunner()
        create_point(r, self.cfg, point_id="P1")
        with self.assertRaises(RestoreRefused):
            restore_point(r, self.cfg, "P1", confirm=False)

    def test_restore_refused_unknown_point(self):
        r = DryRunner()
        with self.assertRaises(RestoreRefused):
            restore_point(r, self.cfg, "NOPE", confirm=True)

    def test_restore_takes_pre_point_then_restores(self):
        r = DryRunner()
        create_point(r, self.cfg, point_id="P1")
        restore_point(r, self.cfg, "P1", confirm=True, pre_id="PRE")
        # a pre-restore point was created, then a restore rsync ran
        self.assertIn("PRE", r.store)
        self.assertIn("reason=pre-restore", r.store["PRE"])
        last = r.calls[-1]
        self.assertEqual(last[0], "rsync")
        self.assertEqual(last[-2], "/srv/rp/P1/")  # restoring FROM the point


if __name__ == "__main__":
    unittest.main()
