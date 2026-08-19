"""Tests for the distribution pipeline skeleton (mkdeb.sh + mkrepo.sh).

Bible §13/§17.2: compiled shell tree -> castalia-desktop .deb -> signed apt
overlay repo. Real builds need dpkg tooling; the --dry-run plans are the
CI-per-commit gate, mirroring how build/mkiso.sh is tested.
"""

import subprocess
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
MKDEB = REPO / "packages" / "mkdeb.sh"
MKREPO = REPO / "build" / "mkrepo.sh"
VERSION = REPO / "VERSION"


def run(script, *args):
    return subprocess.run(["sh", str(script), *args],
                          capture_output=True, text=True, cwd=REPO)


class VersionTest(unittest.TestCase):
    def test_version_is_a_debian_ish_version(self):
        text = VERSION.read_text(encoding="utf-8").strip()
        self.assertRegex(text, r"^\d+\.\d+(\.\d+)?(~[a-z0-9.]+)?$")


class MkDebTest(unittest.TestCase):
    def test_posix_syntax(self):
        proc = subprocess.run(["sh", "-n", str(MKDEB)],
                              capture_output=True, text=True)
        self.assertEqual(proc.returncode, 0, proc.stderr)

    def test_dry_run_plans_the_whole_desktop(self):
        proc = run(MKDEB, "--dry-run")
        self.assertEqual(proc.returncode, 0, proc.stderr)
        out = proc.stdout
        # every manifest binary is in the plan, plus the shell planes
        manifest = (REPO / "tests" / "apps.manifest").read_text("utf-8")
        bins = [line.split("|")[1] for line in manifest.splitlines()
                if line and not line.startswith("#")]
        for b in bins + ["panel/castalia-panel", "desktop/castalia-desktop"]:
            self.assertIn(b, out, f"{b} missing from the mkdeb plan")
        for marker in ("castalia-session", "xsessions", "theme.conf",
                       "installer", "recovery", "plan complete"):
            self.assertIn(marker, out)

    def test_dry_run_never_writes(self):
        out_dir = REPO / "build" / "out" / "deb"
        before = set(out_dir.rglob("*")) if out_dir.exists() else set()
        run(MKDEB, "--dry-run")
        after = set(out_dir.rglob("*")) if out_dir.exists() else set()
        self.assertEqual(before, after)

    def test_version_comes_from_the_version_file(self):
        want = VERSION.read_text(encoding="utf-8").strip()
        proc = run(MKDEB, "--dry-run")
        self.assertIn(f"castalia-desktop {want}", proc.stdout)

    def test_bogus_version_fails_clearly(self):
        proc = run(MKDEB, "--dry-run", "--version", "not-a-version")
        self.assertNotEqual(proc.returncode, 0)
        self.assertIn("version", proc.stderr)

    def test_unknown_option_fails_clearly(self):
        proc = run(MKDEB, "--no-such-flag")
        self.assertEqual(proc.returncode, 2)


class MkRepoTest(unittest.TestCase):
    def test_posix_syntax(self):
        proc = subprocess.run(["sh", "-n", str(MKREPO)],
                              capture_output=True, text=True)
        self.assertEqual(proc.returncode, 0, proc.stderr)

    def test_dry_run_plans_pool_index_release(self):
        proc = run(MKREPO, "--dry-run")
        self.assertEqual(proc.returncode, 0, proc.stderr)
        for marker in ("pool", "Packages", "Release", "plan complete"):
            self.assertIn(marker, proc.stdout)

    def test_unsigned_plan_says_so(self):
        proc = run(MKREPO, "--dry-run")
        self.assertIn("SKIPPED", proc.stdout)

    def test_signed_plan_names_the_key(self):
        proc = run(MKREPO, "--dry-run", "--sign", "CAFEBABE")
        self.assertIn("CAFEBABE", proc.stdout)
        self.assertIn("InRelease", proc.stdout)

    def test_unknown_option_fails_clearly(self):
        proc = run(MKREPO, "--no-such-flag")
        self.assertEqual(proc.returncode, 2)


if __name__ == "__main__":
    unittest.main()
