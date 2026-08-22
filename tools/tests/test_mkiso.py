"""Tests for the ISO pipeline skeleton (build/mkiso.sh + profiles)."""

import subprocess
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
MKISO = REPO / "build" / "mkiso.sh"
PROFILES = REPO / "build" / "profiles"
EDITIONS = ["classic64", "classic32", "min", "live-i386"]

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

    def test_every_profile_is_covered_by_this_file(self):
        # A profile that nothing checks is a profile that quietly rots: the
        # mkiso dry-run gate iterates the directory, but the *contents* are
        # only asserted for the editions named here. Adding one and
        # forgetting to name it should fail, not pass silently.
        on_disk = sorted(p.stem for p in PROFILES.glob("*.conf"))
        named = sorted(set(EDITIONS) | {"live-amd64", "live-compat-amd64",
                                        "live-desktop-amd64"})
        self.assertEqual(on_disk, named,
                         "a build profile is not covered by test_mkiso.py")

    def test_the_boot_proof_editions_are_the_two_architectures(self):
        # The gap this closed: every ISO built until live-i386 existed was
        # amd64, on a product whose FLOOR (§4.1, §16) is a 32-bit Pentium 4.
        # The architecture the promise is about was the one nothing built.
        i386 = (PROFILES / "live-i386.conf").read_text(encoding="utf-8")
        amd64 = (PROFILES / "live-amd64.conf").read_text(encoding="utf-8")
        self.assertIn('ARCH="i386"', i386)
        self.assertIn('ARCH="amd64"', amd64)
        # Same shape, so the two prove the same pipeline rather than two.
        for key in ("SUITE", "COMPRESSION"):
            self.assertEqual(
                [ln for ln in i386.splitlines() if ln.startswith(key)],
                [ln for ln in amd64.splitlines() if ln.startswith(key)], key)

    def test_the_i386_boot_gate_uses_a_32_bit_only_emulator(self):
        # qemu-system-x86_64 boots a 32-bit kernel perfectly well, which is
        # exactly why it cannot prove one: a component that quietly needed
        # 64-bit would sail through it.
        for wf in ("ci.yml", "nightly.yml"):
            text = (REPO / ".github" / "workflows" / wf)\
                .read_text(encoding="utf-8")
            if "live-i386" not in text:
                continue
            block = text.split("live-i386", 1)[1]
            self.assertIn("qemu-system-i386", block, wf)


if __name__ == "__main__":
    unittest.main()
