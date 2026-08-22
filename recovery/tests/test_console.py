"""The recovery console, run for real (Bible §18 Phase 5, §23.7 #4).

§23.7 #4 asks that a deliberately broken update be recoverable "in the
recovery env". The Restore Points engine has been provable for a while; what
was missing was the environment — somewhere to run it from when the system it
would repair will not start.

These tests execute the console as a program, driving it by piping menu
choices at it, with CASTALIA_RECOVERY_DRYRUN=1 so the actions print the exact
commands instead of running them. That is deliberate: the console is shell,
and shell that is only read is shell that is only hoped about. The commands it
would run are asserted verbatim, because the difference between `fsck -y` on
an unmounted device and on a mounted one is somebody's filesystem.
"""
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
CONSOLE = REPO / "recovery" / "boot" / "castalia-recovery-console"
PREMOUNT = REPO / "recovery" / "boot" / "init-premount"
HOOK = REPO / "recovery" / "boot" / "initramfs-hook"


class ConsoleRunner(unittest.TestCase):
    """Runs the console against a fake mounted root."""

    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp, True)
        self.root = self.tmp / "castalia-root"
        (self.root / "etc").mkdir(parents=True)
        (self.root / "usr" / "lib" / "castalia" / "recovery").mkdir(
            parents=True)

    def run_console(self, keys, *, root=None, device="/dev/sda3"):
        env = dict(os.environ)
        env.update({
            "CASTALIA_RECOVERY_ROOT": str(self.root if root is None else root),
            "CASTALIA_RECOVERY_DEVICE": device,
            "CASTALIA_RECOVERY_DRYRUN": "1",
        })
        return subprocess.run(
            ["sh", str(CONSOLE)], input="\n".join(keys) + "\n",
            env=env, capture_output=True, text=True, timeout=30)


class MenuTest(ConsoleRunner):
    def test_it_offers_the_five_things_a_broken_machine_needs(self):
        out = self.run_console(["5"]).stdout
        for needle in ("Restaurar un punto", "Comprobar y reparar el disco",
                       "Reparar el menú de arranque", "Abrir una consola",
                       "Reiniciar"):
            self.assertIn(needle, out, needle)

    def test_it_says_where_the_system_is(self):
        out = self.run_console(["5"]).stdout
        self.assertIn(str(self.root), out)
        self.assertIn("/dev/sda3", out)

    def test_it_says_so_when_the_system_could_not_be_mounted(self):
        # The central case: the root filesystem will not mount. The menu must
        # still come up and must not pretend there is a system in there.
        out = self.run_console(["5"], root=self.tmp / "nothing-here").stdout
        self.assertIn("no se ha podido montar", out.lower())

    def test_an_unknown_choice_is_not_an_action(self):
        proc = self.run_console(["9", "5"])
        self.assertIn("no reconocida", proc.stderr)
        self.assertEqual(proc.returncode, 0)

    def test_end_of_input_exits_rather_than_spinning(self):
        # A console whose stdin closes must stop, not loop forever printing
        # a menu into a dead terminal.
        proc = self.run_console([])
        self.assertEqual(proc.returncode, 0)


class RestoreTest(ConsoleRunner):
    def test_restore_runs_the_installed_tool_in_the_mounted_system(self):
        # NOT a reimplementation. If the thing that restores a point is not
        # the same code that took it, one of them is wrong and nobody finds
        # out which until it matters.
        out = self.run_console(["1", "20260822-010203-000001", "SI", "5"]).stdout
        self.assertIn(f"chroot {self.root} /usr/bin/castalia-restore list",
                      out)
        self.assertIn(f"chroot {self.root} /usr/bin/castalia-restore restore "
                      f"20260822-010203-000001 --confirm", out)

    def test_a_restore_needs_a_typed_confirmation(self):
        out = self.run_console(["1", "20260822-010203-000001", "yes", "5"]).stdout
        self.assertIn("Cancelado", out)
        self.assertNotIn("--confirm", out)

    def test_an_empty_id_goes_back_without_restoring_anything(self):
        out = self.run_console(["1", "", "5"]).stdout
        self.assertNotIn("--confirm", out)

    def test_it_promises_home_is_untouched(self):
        # The engine already excludes /home; the console has to say so,
        # because someone about to type SI is deciding whether to.
        out = self.run_console(["1", "X", "SI", "5"]).stdout
        self.assertIn("/home", out)

    def test_restore_is_refused_when_the_system_is_not_there(self):
        proc = self.run_console(["1", "5"], root=self.tmp / "nothing")
        self.assertIn("No hay puntos de restauración", proc.stderr)


class FsckTest(ConsoleRunner):
    def test_the_filesystem_is_unmounted_before_it_is_checked(self):
        # Checking a mounted filesystem is how a disk with one recoverable
        # error acquires several that are not. The order is the test.
        out = self.run_console(["2", "5"]).stdout
        lines = [ln.strip() for ln in out.splitlines() if ln.strip().startswith("$")]
        umount = next(i for i, ln in enumerate(lines) if ln.startswith("$ umount"))
        fsck = next(i for i, ln in enumerate(lines) if ln.startswith("$ fsck"))
        self.assertLess(umount, fsck, lines)

    def test_it_checks_the_device_not_the_mount_point(self):
        out = self.run_console(["2", "5"]).stdout
        self.assertIn("$ fsck -y /dev/sda3", out)

    def test_it_mounts_the_system_back_afterwards(self):
        out = self.run_console(["2", "5"]).stdout
        self.assertIn(f"$ mount /dev/sda3 {self.root}", out)

    def test_it_refuses_when_it_does_not_know_what_to_check(self):
        proc = self.run_console(["2", "5"], device="")
        self.assertIn("No se sabe qué dispositivo", proc.stderr)


class BootRepairTest(ConsoleRunner):
    def test_it_installs_grub_to_the_disk_not_to_the_partition(self):
        out = self.run_console(["3", "5"]).stdout
        self.assertIn("grub-install --target=i386-pc --recheck /dev/sda", out)
        self.assertNotIn("--recheck /dev/sda3", out)

    def test_it_handles_nvme_style_names(self):
        out = self.run_console(["3", "5"], device="/dev/nvme0n1p3").stdout
        self.assertIn("--recheck /dev/nvme0n1", out)

    def test_it_binds_and_unbinds_the_kernel_filesystems(self):
        # update-grub in a chroot without /dev, /proc and /sys produces a
        # menu with no entries in it, which is a machine that boots to a
        # GRUB prompt.
        out = self.run_console(["3", "5"]).stdout
        for d in ("dev", "proc", "sys"):
            self.assertIn(f"$ mount --rbind /{d} {self.root}/{d}", out)
            self.assertIn(f"$ umount -lf {self.root}/{d}", out)

    def test_it_regenerates_the_menu_after_reinstalling_grub(self):
        out = self.run_console(["3", "5"]).stdout
        install = out.index("grub-install")
        update = out.index("update-grub")
        self.assertLess(install, update)


class ItCannotBreakANormalBootTest(unittest.TestCase):
    """The initramfs scripts run on every boot of every installed machine."""

    def test_every_piece_is_posix_sh(self):
        for path in (CONSOLE, PREMOUNT, HOOK):
            proc = subprocess.run(["sh", "-n", str(path)],
                                  capture_output=True, text=True)
            self.assertEqual(proc.returncode, 0, f"{path}: {proc.stderr}")
            text = path.read_text(encoding="utf-8")
            self.assertEqual(text.splitlines()[0], "#!/bin/sh", path)
            self.assertNotIn("[[ ", text, path)      # §12: no bashisms

    def test_the_premount_script_answers_prereqs(self):
        # initramfs-tools calls every script with `prereqs` first and uses
        # the answer to order them. A script that instead did its work would
        # run at the wrong time, or twice.
        proc = subprocess.run(["sh", str(PREMOUNT), "prereqs"],
                              capture_output=True, text=True, timeout=10)
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(proc.stdout.strip(), "")

    def test_the_premount_script_does_nothing_without_the_kernel_flag(self):
        # This runs inside every boot of every installed machine. If it can
        # do anything at all on a normal boot, it is a bug in the boot path
        # of a system that was working fine.
        # Run against this machine's real /proc/cmdline, which does not have
        # the flag: the script must exit 0 having printed and done nothing.
        self.assertNotIn("castalia.recovery=1",
                         Path("/proc/cmdline").read_text(encoding="utf-8"))
        proc = subprocess.run(["sh", str(PREMOUNT)],
                              capture_output=True, text=True, timeout=10)
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(proc.stdout, "")
        self.assertEqual(proc.stderr, "")

    def test_it_matches_the_flag_as_a_word_not_as_a_substring(self):
        # `castalia.recovery=0` and somebody else's parameter that happens to
        # contain the text must not trigger a recovery boot.
        text = PREMOUNT.read_text(encoding="utf-8")
        self.assertIn("castalia.recovery=1)", text)
        self.assertIn("for _word in", text)

    def test_the_hook_never_fails_the_initrd_build(self):
        # An initrd that fails to build is a machine with no initrd, which is
        # a machine that does not boot. Every copy in the hook is fail-open.
        text = HOOK.read_text(encoding="utf-8")
        self.assertIn("exit 0", text)
        self.assertIn("|| :", text)
        self.assertNotIn("set -e", text)

    def test_the_console_never_ships_a_second_restore_implementation(self):
        # The one rule that keeps recovery honest: it drives the tested tool.
        text = CONSOLE.read_text(encoding="utf-8")
        self.assertIn("castalia-restore", text)
        for forbidden in ("rsync", "--link-dest"):
            self.assertNotIn(forbidden, text,
                             f"the console reimplements {forbidden}")


if __name__ == "__main__":
    unittest.main()
