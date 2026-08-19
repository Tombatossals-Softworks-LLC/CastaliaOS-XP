"""Tests for the ISO pipeline skeleton (build/mkiso.sh + profiles)."""

import subprocess
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
MKISO = REPO / "build" / "mkiso.sh"
PROFILES = REPO / "build" / "profiles"
EDITIONS = ["classic64", "classic32", "min"]

REQUIRED_KEYS = ("LABEL", "ARCH", "SUITE", "MIRROR", "PACKAGES",
                 "COMPRESSION")
STAGES = ("deps", "bootstrap", "configure", "packages", "assets",
          "squashfs", "iso")


def dry_run(edition: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        ["sh", str(MKISO), "--edition", edition, "--dry-run"],
        capture_output=True, text=True, cwd=REPO)


class MkIsoTest(unittest.TestCase):
    def test_posix_syntax(self):
        proc = subprocess.run(["sh", "-n", str(MKISO)],
                              capture_output=True, text=True)
        self.assertEqual(proc.returncode, 0, proc.stderr)

    def test_dry_run_prints_every_stage(self):
        for edition in EDITIONS:
            proc = dry_run(edition)
            self.assertEqual(proc.returncode, 0, proc.stderr)
            for stage in STAGES:
                self.assertIn(f"PLAN {stage}", proc.stdout,
                              f"{edition}: stage {stage} missing")
            self.assertIn("plan complete", proc.stdout, edition)

    def test_dry_run_never_writes(self):
        out = REPO / "build" / "out" / "iso"
        before = set(out.rglob("*")) if out.exists() else set()
        dry_run("classic64")
        after = set(out.rglob("*")) if out.exists() else set()
        self.assertEqual(before, after)

    def test_unknown_edition_fails_clearly(self):
        proc = dry_run("does-not-exist")
        self.assertNotEqual(proc.returncode, 0)
        self.assertIn("unknown edition", proc.stderr)

    def test_profiles_have_required_keys(self):
        for edition in EDITIONS:
            text = (PROFILES / f"{edition}.conf").read_text(encoding="utf-8")
            for key in REQUIRED_KEYS:
                self.assertRegex(text, rf'(?m)^{key}="[^"]+"$',
                                 f"{edition}: {key}")

    def test_editions_match_the_bible(self):
        # §4.1: 32-bit is SSE2/PAE-class; 64-bit is amd64; min is the FLOOR
        c32 = (PROFILES / "classic32.conf").read_text(encoding="utf-8")
        c64 = (PROFILES / "classic64.conf").read_text(encoding="utf-8")
        mn = (PROFILES / "min.conf").read_text(encoding="utf-8")
        self.assertIn('ARCH="i386"', c32)
        self.assertIn("686-pae", c32)
        self.assertIn('ARCH="amd64"', c64)
        self.assertIn("runit-init", c64)
        self.assertNotIn("lightdm", mn)      # min stays minimal


if __name__ == "__main__":
    unittest.main()
