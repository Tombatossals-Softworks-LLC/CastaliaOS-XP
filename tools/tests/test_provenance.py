"""Unit tests for castalia_qa.provenance (the §3.9 legal gate)."""

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from castalia_qa import provenance  # noqa: E402

HEADER = (
    "asset_path,type,description,source,author,license,"
    "license_url,added_commit,notes\n"
)


class ProvenanceTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)
        (self.root / "legal").mkdir()
        (self.root / "branding" / "logo").mkdir(parents=True)
        self.addCleanup(self._tmp.cleanup)

    def write_ledger(self, body: str = "") -> None:
        ledger = self.root / "legal" / "ASSET_PROVENANCE.csv"
        ledger.write_text(HEADER + "# comment line is skipped\n" + body,
                          encoding="utf-8")

    def add_asset(self, rel: str) -> None:
        path = self.root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b"<svg/>")

    def test_untracked_asset_fails(self):
        self.write_ledger()
        self.add_asset("branding/logo/mark.svg")
        errors = provenance.check(self.root)
        self.assertTrue(any("NO row" in e for e in errors))

    def test_tracked_asset_passes(self):
        self.add_asset("branding/logo/mark.svg")
        self.write_ledger(
            "branding/logo/mark.svg,icon,Test mark,original,"
            "Tombatossals Softworks,Original-Castalia,,pending,\n"
        )
        self.assertEqual(provenance.check(self.root), [])

    def test_stale_ledger_row_fails(self):
        self.write_ledger(
            "branding/logo/ghost.svg,icon,Gone,original,"
            "Tombatossals Softworks,Original-Castalia,,pending,\n"
        )
        errors = provenance.check(self.root)
        self.assertTrue(any("does not exist" in e for e in errors))

    def test_row_missing_license_fails(self):
        self.add_asset("branding/logo/mark.svg")
        self.write_ledger(
            "branding/logo/mark.svg,icon,Test mark,original,"
            "Tombatossals Softworks,,,pending,\n"
        )
        errors = provenance.check(self.root)
        self.assertTrue(any("missing required field(s): license" in e
                            for e in errors))

    def test_missing_ledger_fails(self):
        (self.root / "legal").rmdir()
        (self.root / "legal").mkdir()  # legal/ exists, csv does not
        self.add_asset("branding/logo/mark.svg")
        errors = provenance.check(self.root)
        self.assertTrue(any("ledger file is missing" in e for e in errors))

    def test_non_asset_files_are_ignored(self):
        self.write_ledger()
        readme = self.root / "branding" / "README.md"
        readme.write_text("docs, not an asset", encoding="utf-8")
        self.assertEqual(provenance.check(self.root), [])

    def test_repo_ledger_is_clean(self):
        repo = Path(__file__).resolve().parents[2]
        errors = provenance.check(repo)
        self.assertEqual(errors, [], "\n".join(errors))


if __name__ == "__main__":
    unittest.main()
