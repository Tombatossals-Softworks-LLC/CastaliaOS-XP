"""Unit tests for the boot-menu background baker and boot menu configs."""

import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))

import bootbg_gen  # noqa: E402

REPO = bootbg_gen.REPO


class BootBgTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.px = bootbg_gen.render()

    def test_dimensions(self):
        self.assertEqual(len(self.px), bootbg_gen.H)
        self.assertEqual(len(self.px[0]), bootbg_gen.W)

    def test_deterministic(self):
        self.assertEqual(self.px[240][320], bootbg_gen.render()[240][320])

    def test_lit_window_present(self):
        flat = {c for row in self.px for c in row}
        self.assertIn(bootbg_gen.WINDOW, flat)
        self.assertIn(bootbg_gen.WAVE_LINE, flat)
        self.assertIn(bootbg_gen.KEEP, flat)

    def test_png_structure(self):
        out = REPO / "iso" / "boot-bg" / "splash.png"
        data = out.read_bytes()
        self.assertTrue(data.startswith(b"\x89PNG\r\n\x1a\n"))
        w, h = struct.unpack(">II", data[16:24])
        self.assertEqual((w, h), (bootbg_gen.W, bootbg_gen.H))
        # IDAT decompresses to exactly H * (1 + W*3) filtered bytes
        idat_start = data.index(b"IDAT") + 4
        idat_len = struct.unpack(">I", data[data.index(b"IDAT") - 4:
                                            data.index(b"IDAT")])[0]
        raw = zlib.decompress(data[idat_start:idat_start + idat_len])
        self.assertEqual(len(raw), bootbg_gen.H * (1 + bootbg_gen.W * 3))


class BootMenuConfigTest(unittest.TestCase):
    def test_no_orphan_grub_cfg(self):
        # There used to be an iso/grub/grub.cfg here, and a test right on this
        # spot that read it and asserted the Bible's entries were present. It
        # passed for as long as it existed; the file was never installed on
        # anything, and its kernel paths were wrong for the separate-/boot
        # layout the installer creates. The menu now comes from grub-mkconfig
        # (iso/grub/README.md), so the file must stay gone — the test is worth
        # more as a guard against it coming back than it ever was as a check.
        self.assertFalse((REPO / "iso" / "grub" / "grub.cfg").exists(),
                         "iso/grub/grub.cfg is back; nothing installs it")

    def test_safe_mode_generator_is_shipped_and_executable(self):
        gen = REPO / "iso" / "grub" / "11_castalia_safe"
        self.assertTrue(gen.is_file(), gen)
        # grub-mkconfig only runs the executable files in /etc/grub.d.
        self.assertTrue(gen.stat().st_mode & 0o111, f"{gen} not executable")
        text = gen.read_text(encoding="utf-8")
        self.assertEqual(text.splitlines()[0], "#!/bin/sh")
        for needle in ("castalia.safemode=1", "nomodeset", "maxcpus=1",
                       "grub-mkconfig_lib", "--id castalia-safe"):
            self.assertIn(needle, text, needle)

    def test_safe_mode_generator_is_posix(self):
        gen = REPO / "iso" / "grub" / "11_castalia_safe"
        proc = subprocess.run(["sh", "-n", str(gen)],
                              capture_output=True, text=True)
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertNotIn("[[ ", gen.read_text(encoding="utf-8"))   # §12

    def test_installer_writes_the_boot_identity(self):
        # The Castalia menu reaches a machine through /etc/default/grub, so
        # that is where the Bible's boot rules have to be asserted now.
        sys.path.insert(0, str(REPO / "installer"))
        from castalia_installer import plan  # noqa: PLC0415

        cfg = plan.render_default_grub({})
        self.assertEqual(plan.GRUB_DROPIN,
                         "/etc/default/grub.d/50-castalia.cfg")
        for needle in ('GRUB_DISTRIBUTOR="Castalia OS"',   # branding
                       "GRUB_TIMEOUT=4",                   # §6.2 short
                       "GRUB_DEFAULT=saved",               # §6.2 remembered
                       "GRUB_SAVEDEFAULT=true",
                       "GRUB_THEME=/boot/grub/themes/castalia/theme.txt",
                       "GRUB_DISABLE_OS_PROBER=false"):    # §14.3
            self.assertIn(needle, cfg, needle)

    def test_grub_theme_uses_castalia_colors(self):
        theme = (REPO / "iso" / "grub" / "theme" / "theme.txt")\
            .read_text(encoding="utf-8")
        for needle in ("splash.png", "#3E82B6", "boot_menu",
                       "Tombatossals Softworks", "C A S T A L I A"):
            self.assertIn(needle, theme, needle)

    def test_isolinux_menu_has_the_bible_entries(self):
        # The template mkiso.sh renders, plus the install entries it splices
        # in. This used to read iso/isolinux/isolinux.cfg — a file the build
        # ignored — so it happily passed on "LABEL memtest" and
        # "castalia.installer=text" while neither ever reached an ISO.
        cfg = (REPO / "iso" / "isolinux" / "isolinux.cfg.in")\
            .read_text(encoding="utf-8")
        cfg += (REPO / "iso" / "isolinux" / "entries-install.cfg")\
            .read_text(encoding="utf-8")
        for needle in ("UI vesamenu.c32", "MENU BACKGROUND splash.png",
                       "LABEL live", "LABEL livesafe", "LABEL textinstall",
                       "LABEL install", "nomodeset",
                       "castalia.installer=text", "castalia.installer=gui"):
            self.assertIn(needle, cfg, needle)

    def test_isolinux_selection_uses_azure(self):
        cfg = (REPO / "iso" / "isolinux" / "isolinux.cfg.in")\
            .read_text(encoding="utf-8")
        self.assertIn("#FF2C6699", cfg)   # selection bar = azure deep


class SafeModeGeneratorTest(unittest.TestCase):
    """Run iso/grub/11_castalia_safe for real, against a stub GRUB.

    This is the one file in the boot path that can make a machine unbootable:
    grub-mkconfig runs under ``set -e`` and aborts the entire menu if a
    generator exits non-zero, and it pastes whatever the generator printed
    straight into grub.cfg. Reading the script is not enough — it has to be
    executed, on the failure paths as well as the happy one.
    """

    GEN = REPO / "iso" / "grub" / "11_castalia_safe"

    LIB = """\
prepare_grub_to_access_device() {
    echo "insmod part_msdos"
    echo "search --no-floppy --fs-uuid --set=root BOOT-UUID"
}
make_system_path_relative_to_its_root() { echo "/${1##*/}"; }
"""

    PROBE = """\
#!/bin/sh
case "$1" in
  --target=fs_uuid) echo "ROOT-UUID" ;;
  --target=device)  echo "/dev/sda1" ;;
esac
"""

    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp, True)
        self.boot = self.tmp / "boot"
        self.boot.mkdir()
        (self.tmp / "lib.sh").write_text(self.LIB, encoding="utf-8")
        binp = self.tmp / "bin"
        binp.mkdir()
        probe = binp / "grub-probe"
        probe.write_text(self.PROBE, encoding="utf-8")
        probe.chmod(0o755)

    def kernel(self, version):
        (self.boot / f"vmlinuz-{version}").touch()
        (self.boot / f"initrd.img-{version}").touch()

    def run_gen(self, *, lib="lib.sh"):
        env = dict(os.environ)
        env.update({
            "PATH": f"{self.tmp / 'bin'}:{env['PATH']}",
            "CASTALIA_GRUB_BOOTDIR": str(self.boot),
            "CASTALIA_GRUB_MKCONFIG_LIB": str(self.tmp / lib),
            "GRUB_DISTRIBUTOR": "Castalia OS",
        })
        return subprocess.run(["sh", str(self.GEN)], env=env,
                              capture_output=True, text=True)

    def test_emits_a_well_formed_safe_mode_entry(self):
        self.kernel("6.1.0-9-amd64")
        proc = self.run_gen()
        self.assertEqual(proc.returncode, 0, proc.stderr)
        out = proc.stdout
        self.assertIn('--id castalia-safe {', out)
        self.assertIn("search --no-floppy --fs-uuid --set=root BOOT-UUID", out)
        self.assertIn("linux /vmlinuz-6.1.0-9-amd64 root=UUID=ROOT-UUID ro "
                      "nomodeset maxcpus=1 castalia.safemode=1", out)
        self.assertIn("initrd /initrd.img-6.1.0-9-amd64", out)
        self.assertEqual(out.count("{"), 1)
        self.assertEqual(out.count("}"), 1)

    def test_picks_the_newest_kernel_by_version_not_by_name(self):
        # The trap: as strings, vmlinuz-6.1.0-10 sorts BEFORE vmlinuz-6.1.0-9,
        # so a plain glob would offer Safe Mode on the older kernel — the one
        # an update just replaced, and the one most likely to be removed next.
        self.kernel("6.1.0-9-amd64")
        self.kernel("6.1.0-10-amd64")
        out = self.run_gen().stdout
        self.assertIn("linux /vmlinuz-6.1.0-10-amd64", out)
        self.assertNotIn("6.1.0-9-amd64", out)

    def test_prints_nothing_when_there_is_no_grub_library(self):
        # The real failure this guards: grub-mkconfig_lib reads variables only
        # grub-mkconfig exports, so sourcing it in a `set -u` shell kills the
        # caller. That exit status would abort update-grub, and with it the
        # install. Silence and exit 0 is the only safe answer.
        self.kernel("6.1.0-9-amd64")
        proc = self.run_gen(lib="does-not-exist")
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(proc.stdout, "")

    def test_prints_nothing_when_there_is_no_kernel(self):
        proc = self.run_gen()
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(proc.stdout, "")

    def test_prints_nothing_when_the_initrd_is_missing(self):
        # Half a boot entry is worse than none: it would appear in the menu
        # and then drop the user at a GRUB error prompt.
        (self.boot / "vmlinuz-6.1.0-9-amd64").touch()
        proc = self.run_gen()
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(proc.stdout, "")

    def test_prints_nothing_when_grub_probe_gives_no_uuid(self):
        self.kernel("6.1.0-9-amd64")
        probe = self.tmp / "bin" / "grub-probe"
        probe.write_text("#!/bin/sh\nexit 1\n", encoding="utf-8")
        probe.chmod(0o755)
        proc = self.run_gen()
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(proc.stdout, "")

    def test_a_library_that_exits_cannot_take_the_generator_with_it(self):
        self.kernel("6.1.0-9-amd64")
        (self.tmp / "hostile.sh").write_text("exit 3\n", encoding="utf-8")
        proc = self.run_gen(lib="hostile.sh")
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(proc.stdout, "")


if __name__ == "__main__":
    unittest.main()
