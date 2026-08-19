"""Tests for the canonical app manifest + the QA harnesses that consume it.

tests/apps.manifest is the single source of truth for "what ships": the
offscreen render gate, the live E2E suite and the .deb packaging all iterate
it. These tests keep it well-formed and in sync with the ISO hook's install
list, so an app cannot ship untested or be tested but not shipped.
"""

import subprocess
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
MANIFEST = REPO / "tests" / "apps.manifest"
HOOK = REPO / "build" / "hooks" / "desktop-amd64.sh"
SCRIPTS = [
    REPO / "tests" / "run.sh",
    REPO / "tests" / "offscreen" / "render-all.sh",
    REPO / "tests" / "e2e" / "apps-live.sh",
    REPO / "tests" / "e2e" / "session-smoke.sh",
]


def entries():
    rows = []
    for line in MANIFEST.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("#"):
            continue
        rows.append(line.split("|"))
    return rows


class ManifestTest(unittest.TestCase):
    def test_shape(self):
        rows = entries()
        self.assertGreaterEqual(len(rows), 25, "the app suite shrank?")
        for row in rows:
            self.assertEqual(len(row), 4, row)
            name, bin_path, _live, _render = row
            self.assertRegex(name, r"^[a-z][a-z0-9-]*$", name)
            self.assertRegex(bin_path, r"^[a-z0-9/-]+/castalia-[a-z0-9-]+$",
                             bin_path)

    def test_no_duplicates(self):
        names = [row[0] for row in entries()]
        bins = [row[1] for row in entries()]
        self.assertEqual(len(names), len(set(names)))
        self.assertEqual(len(bins), len(set(bins)))

    def test_every_app_has_sources(self):
        # each manifest binary corresponds to a real source component
        for _name, bin_path, _live, _render in entries():
            comp = bin_path.rsplit("/", 1)[0]
            src = {
                "installer-gui": REPO / "installer" / "gui",
                "recovery-gui": REPO / "recovery" / "gui",
                "explorer": REPO / "shell" / "explorer",
            }.get(comp, REPO / comp)
            self.assertTrue(src.is_dir(), f"{bin_path}: no sources at {src}")

    def test_matches_the_iso_hook_install_list(self):
        # what the live-desktop ISO installs == what the manifest tests
        hook = HOOK.read_text(encoding="utf-8")
        hook_bins = set()
        for line in hook.splitlines():
            if "/tmp/shellbuild/" in line and "install -Dm755" in line:
                hook_bins.add(line.split("/tmp/shellbuild/")[1].split()[0])
        manifest_bins = {row[1] for row in entries()}
        # the hook additionally installs the two shell planes
        self.assertEqual(
            hook_bins - manifest_bins,
            {"panel/castalia-panel", "desktop/castalia-desktop"},
            "ISO hook and manifest disagree on the app list")
        self.assertEqual(manifest_bins - hook_bins, set(),
                         "manifest tests apps the ISO does not install")


class HarnessScriptsTest(unittest.TestCase):
    def test_posix_syntax(self):
        for script in SCRIPTS:
            self.assertTrue(script.is_file(), script)
            proc = subprocess.run(["sh", "-n", str(script)],
                                  capture_output=True, text=True)
            self.assertEqual(proc.returncode, 0, f"{script}: {proc.stderr}")

    def test_unknown_option_fails_clearly(self):
        for script in SCRIPTS:
            proc = subprocess.run(["sh", str(script), "--no-such-flag"],
                                  capture_output=True, text=True, cwd=REPO)
            self.assertEqual(proc.returncode, 2, script)

    def test_runner_knows_its_tiers(self):
        text = (REPO / "tests" / "run.sh").read_text(encoding="utf-8")
        for tier in ("lint", "unit", "gates", "build", "render", "e2e",
                     "iso", "quick", "full"):
            self.assertIn(tier, text)


if __name__ == "__main__":
    unittest.main()
