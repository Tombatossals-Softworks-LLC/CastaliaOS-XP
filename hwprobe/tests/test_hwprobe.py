"""hwprobe against captured machines (Bible §6.15, §18 Phase 1).

The difficulty with hardware code is that the machine running the tests is
never the machine the code is for. So every test here builds a sysfs tree on
disk — the same handful of tiny text files the kernel exposes — and points the
probe at it. Two of the trees are copies of what §19's reference machines
actually present.

The other half of these tests is about what the report is allowed to *claim*.
A hardware report that overstates what it knows is worse than no report:
"suspend: safe" on a machine nobody suspended is a lost session, and it looks
identical to a machine somebody tested.
"""
import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HERE))

from castalia_hwprobe import (  # noqa: E402
    SUSPEND_UNKNOWN,
    SUSPEND_UNSAFE,
    Quirk,
    decide,
    load_quirks,
    probe_machine,
    read_report,
    write_report,
)
from castalia_hwprobe.probe import REPORT_VERSION, probe_pci  # noqa: E402
from castalia_hwprobe.quirks import DEFAULT_QUIRKS  # noqa: E402


class FakeSysfs:
    """Builds the few files sysfs exposes per PCI device."""

    def __init__(self, root: Path):
        self.root = root
        (root / "sys/bus/pci/devices").mkdir(parents=True)
        (root / "proc").mkdir(parents=True)

    def device(self, address, vendor, device, pci_class, driver=""):
        path = self.root / "sys/bus/pci/devices" / address
        path.mkdir()
        (path / "vendor").write_text(f"0x{vendor}\n")
        (path / "device").write_text(f"0x{device}\n")
        (path / "class").write_text(f"0x{pci_class}\n")
        if driver:
            target = self.root / "sys/bus/pci/drivers" / driver
            target.mkdir(parents=True, exist_ok=True)
            (path / "driver").symlink_to(target)
        return self

    def cpu(self, model="Intel(R) Pentium(R) 4 CPU 2.80GHz",
            flags="fpu vme de pse tsc msr pae mce cx8 sse sse2"):
        (self.root / "proc/cpuinfo").write_text(
            f"processor\t: 0\nmodel name\t: {model}\nflags\t\t: {flags}\n")
        return self

    def ram(self, mib=512):
        (self.root / "proc/meminfo").write_text(
            f"MemTotal:       {mib * 1024} kB\nMemFree: 100000 kB\n")
        return self


class ProbeTest(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp, True)
        self.fs = FakeSysfs(self.tmp)

    def test_a_floor_machine_reads_correctly(self):
        # The §16 FLOOR reference: a Pentium 4 with Intel integrated
        # graphics, 512 MB, an Intel NIC and an AC'97 codec.
        self.fs.cpu().ram(512)
        self.fs.device("0000:00:02.0", "8086", "2582", "030000", "i915")
        self.fs.device("0000:00:1e.2", "8086", "266e", "040100", "snd_intel8x0")
        self.fs.device("0000:00:1f.1", "8086", "24db", "01018a", "ata_piix")
        self.fs.device("0000:02:08.0", "8086", "1229", "020000", "e100")
        m = decide(probe_machine(str(self.tmp)))
        self.assertEqual(m.ram_mib, 512)
        self.assertTrue(m.has_sse2)
        self.assertEqual([d.kind for d in m.gpus], ["gpu"])
        self.assertEqual(m.xorg_driver, "modesetting")
        self.assertEqual(len(m.of_kind("net")), 1)
        self.assertEqual(len(m.of_kind("multimedia")), 1)

    def test_the_primary_gpu_is_the_first_in_bus_order(self):
        # sysfs lists in directory order; bus order is what the firmware
        # posted on, which is what "primary" means to a person looking at a
        # screen that either lit up or did not.
        self.fs.cpu().ram(2048)
        self.fs.device("0000:01:00.0", "10de", "0a65", "030000", "nouveau")
        self.fs.device("0000:00:02.0", "8086", "0102", "030000", "i915")
        m = decide(probe_machine(str(self.tmp)))
        self.assertEqual(m.gpus[0].address, "0000:00:02.0")
        self.assertEqual(m.xorg_driver, "modesetting")

    def test_a_device_with_no_driver_is_reported_as_such(self):
        # The single most useful thing a first-boot probe can say.
        self.fs.cpu().ram(512)
        self.fs.device("0000:00:02.0", "8086", "2582", "030000", "i915")
        self.fs.device("0000:02:00.0", "14e4", "4315", "028000")
        m = decide(probe_machine(str(self.tmp)))
        self.assertEqual([d.ident for d in m.unbound()], ["14e4:4315"])
        self.assertTrue(any("Sin controlador: 14e4:4315" in n
                            for n in m.notes), m.notes)

    def test_a_machine_without_sse2_is_flagged(self):
        # §18 keeps a Legacy build for exactly these, as a stretch goal. The
        # probe has to be able to say which machine it is looking at.
        self.fs.cpu(model="AMD Athlon(tm) XP 2400+", flags="fpu tsc sse")
        self.fs.ram(512)
        m = probe_machine(str(self.tmp))
        self.assertFalse(m.has_sse2)

    def test_unreadable_hardware_is_skipped_not_guessed_at(self):
        self.fs.cpu().ram(512)
        path = self.tmp / "sys/bus/pci/devices/0000:00:02.0"
        path.mkdir()
        (path / "vendor").write_text("garbage\n")
        (path / "device").write_text("0x2582\n")
        (path / "class").write_text("0x030000\n")
        self.assertEqual(probe_pci(str(self.tmp)), [])

    def test_a_machine_with_no_sysfs_at_all_is_not_a_crash(self):
        empty = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, empty, True)
        m = decide(probe_machine(str(empty)))
        self.assertEqual(m.devices, [])
        self.assertEqual(m.ram_mib, 0)
        self.assertEqual(m.suspend, SUSPEND_UNKNOWN)


class WhatItRefusesToClaimTest(unittest.TestCase):
    """A report that overstates what it knows is worse than no report."""

    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp, True)
        self.fs = FakeSysfs(self.tmp).cpu().ram(512)

    def test_an_unknown_machine_gets_unknown_suspend(self):
        self.fs.device("0000:00:02.0", "8086", "2582", "030000", "i915")
        m = decide(probe_machine(str(self.tmp)))
        self.assertEqual(m.suspend, SUSPEND_UNKNOWN)

    def test_no_shipped_quirk_claims_suspend_is_safe_without_a_machine(self):
        # v0 ships three rows and none of them says "safe", because nobody
        # has certified a machine yet (§19). The day one does, this test
        # changes with it — and that is the point: it makes claiming "safe"
        # a deliberate edit rather than a default.
        claimed = [q.match for q in DEFAULT_QUIRKS if q.suspend == "safe"]
        self.assertEqual(claimed, [],
                         "a quirk claims suspend is safe; which machine was "
                         "it tested on? (§19)")

    def test_an_unknown_gpu_vendor_gets_no_driver_override(self):
        # Not guessing is the right answer: modern Xorg picks correctly
        # nearly always, and overriding it blindly is how a machine that
        # worked stops working.
        self.fs.device("0000:00:02.0", "beef", "1234", "030000", "")
        m = decide(probe_machine(str(self.tmp)))
        self.assertEqual(m.xorg_driver, "")


class QuirksTest(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp, True)
        self.fs = FakeSysfs(self.tmp).cpu().ram(1024)

    def test_a_device_specific_row_beats_a_vendor_wide_one(self):
        # Vendor-wide says "this generation tends to"; device-specific says
        # "this one does". The observation wins over the generalisation.
        self.fs.device("0000:00:02.0", "1013", "00b8", "030000", "cirrus")
        quirks = (
            Quirk(match="1013", note="vendor", xorg_driver="cirrus",
                  suspend=SUSPEND_UNKNOWN),
            Quirk(match="1013:00b8", note="device", xorg_driver="vesa",
                  suspend=SUSPEND_UNSAFE),
        )
        m = decide(probe_machine(str(self.tmp)), quirks)
        self.assertEqual(m.xorg_driver, "vesa")
        self.assertEqual(m.suspend, SUSPEND_UNSAFE)
        self.assertIn("vendor", m.notes)
        self.assertIn("device", m.notes)

    def test_order_in_the_table_does_not_change_the_answer(self):
        self.fs.device("0000:00:02.0", "1013", "00b8", "030000", "cirrus")
        rows = (
            Quirk(match="1013:00b8", xorg_driver="vesa", note="device"),
            Quirk(match="1013", xorg_driver="cirrus", note="vendor"),
        )
        self.assertEqual(decide(probe_machine(str(self.tmp)), rows)
                         .xorg_driver, "vesa")

    def test_a_quirk_for_hardware_that_is_not_here_does_nothing(self):
        self.fs.device("0000:00:02.0", "8086", "2582", "030000", "i915")
        m = decide(probe_machine(str(self.tmp)),
                   (Quirk(match="10de", xorg_driver="nouveau", note="nope"),))
        self.assertEqual(m.xorg_driver, "modesetting")
        self.assertNotIn("nope", m.notes)

    def test_blacklisted_modules_are_collected_without_duplicates(self):
        self.fs.device("0000:02:00.0", "14e4", "4315", "028000", "b43")
        quirks = (
            Quirk(match="14e4", modules_blacklist=("b43", "ssb")),
            Quirk(match="14e4:4315", modules_blacklist=("b43",)),
        )
        m = decide(probe_machine(str(self.tmp)), quirks)
        self.assertEqual(m.modules_blacklist, ("b43", "ssb"))


class QuirksFileTest(unittest.TestCase):
    """The table ships as data and gets corrected in the field."""

    def load(self, text):
        tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, tmp, True)
        path = tmp / "quirks.json"
        path.write_text(text, encoding="utf-8")
        return load_quirks(str(path))

    def test_a_well_formed_table_loads(self):
        rows = self.load(json.dumps({"quirks": [
            {"match": "10DE", "note": "n", "xorg_driver": "nouveau",
             "suspend": "unsafe", "modules_blacklist": ["nvidia"]},
        ]}))
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0].match, "10de")   # normalised
        self.assertEqual(rows[0].modules_blacklist, ("nvidia",))

    def test_a_bare_list_is_accepted_too(self):
        self.assertEqual(len(self.load('[{"match": "8086"}]')), 1)

    def test_a_broken_file_loses_the_table_and_not_the_boot(self):
        # This runs at first boot. A typo in shipped data must degrade to
        # "we know less about this machine", never to a machine that stops.
        for text in ("", "{", "null", '{"quirks": "no"}', "[1, 2, 3]"):
            self.assertEqual(self.load(text), (), repr(text))

    def test_a_row_without_a_match_is_skipped_and_the_rest_survive(self):
        rows = self.load('[{"note": "orphan"}, {"match": "8086"}]')
        self.assertEqual([r.match for r in rows], ["8086"])

    def test_a_missing_file_is_empty_not_an_exception(self):
        self.assertEqual(load_quirks("/nonexistent/quirks.json"), ())


class ReportTest(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp, True)
        fs = FakeSysfs(self.tmp).cpu().ram(512)
        fs.device("0000:00:02.0", "8086", "2582", "030000", "i915")
        self.machine = decide(probe_machine(str(self.tmp)))
        self.path = str(self.tmp / "var/lib/castalia/hwprobe/report.json")

    def test_it_round_trips(self):
        write_report(self.machine, self.path, probed_at="2026-08-22 01:00")
        doc = read_report(self.path)
        self.assertEqual(doc["version"], REPORT_VERSION)
        self.assertEqual(doc["ram_mib"], 512)
        self.assertEqual(doc["xorg_driver"], "modesetting")
        self.assertEqual(doc["devices"][0]["id"], "8086:2582")

    def test_a_report_from_a_version_we_do_not_know_is_not_read(self):
        # A Hardware Center from a newer release meeting an older machine's
        # report is ordinary, not an error — but guessing at the fields is
        # how it shows somebody wrong information about their own computer.
        write_report(self.machine, self.path)
        doc = json.loads(Path(self.path).read_text(encoding="utf-8"))
        doc["version"] = REPORT_VERSION + 99
        Path(self.path).write_text(json.dumps(doc), encoding="utf-8")
        self.assertEqual(read_report(self.path), {})

    def test_a_truncated_report_is_not_read(self):
        Path(self.path).parent.mkdir(parents=True, exist_ok=True)
        Path(self.path).write_text('{"version": 1, "cpu"',
                                   encoding="utf-8")
        self.assertEqual(read_report(self.path), {})

    def test_no_report_at_all_is_an_empty_dict(self):
        self.assertEqual(read_report(self.path), {})

    def test_the_write_is_atomic(self):
        # First boot on a machine somebody may switch off during it. A
        # half-written file that happens to parse is worse than none.
        write_report(self.machine, self.path)
        leftovers = list(Path(self.path).parent.glob("*.tmp"))
        self.assertEqual(leftovers, [])


class CliTest(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp, True)
        FakeSysfs(self.tmp).cpu().ram(512).device(
            "0000:00:02.0", "8086", "2582", "030000", "i915")

    def run_cli(self, *argv):
        import io
        from contextlib import redirect_stderr, redirect_stdout

        from castalia_hwprobe.__main__ import main

        out, err = io.StringIO(), io.StringIO()
        with redirect_stdout(out), redirect_stderr(err):
            rc = main(list(argv))
        return rc, out.getvalue(), err.getvalue()

    def test_dry_run_writes_nothing(self):
        out_path = self.tmp / "report.json"
        rc, out, _ = self.run_cli("--root", str(self.tmp), "--dry-run",
                                  "--out", str(out_path))
        self.assertEqual(rc, 0)
        self.assertFalse(out_path.exists())
        self.assertEqual(json.loads(out)["xorg_driver"], "modesetting")

    def test_it_writes_and_can_show_what_it_wrote(self):
        out_path = self.tmp / "report.json"
        rc, _, _ = self.run_cli("--root", str(self.tmp), "--out",
                                str(out_path))
        self.assertEqual(rc, 0)
        rc, out, _ = self.run_cli("--show", "--out", str(out_path))
        self.assertEqual(rc, 0)
        self.assertIn("Pentium", out)
        self.assertIn("512 MiB", out)

    def test_show_with_no_report_says_so_rather_than_printing_nothing(self):
        rc, _, err = self.run_cli("--show", "--out",
                                  str(self.tmp / "absent.json"))
        self.assertEqual(rc, 1)
        self.assertIn("no hay ningún informe", err)

    def test_a_broken_quirks_file_warns_and_carries_on(self):
        bad = self.tmp / "bad.json"
        bad.write_text("{", encoding="utf-8")
        rc, out, err = self.run_cli("--root", str(self.tmp), "--dry-run",
                                    "--quirks", str(bad))
        self.assertEqual(rc, 0)
        self.assertIn("aviso", err)
        self.assertEqual(json.loads(out)["xorg_driver"], "modesetting")

    def test_the_human_report_names_devices_with_no_driver(self):
        wifi = self.tmp / "sys/bus/pci/devices/0000:02:00.0"
        wifi.mkdir()
        (wifi / "vendor").write_text("0x14e4\n")
        (wifi / "device").write_text("0x4315\n")
        (wifi / "class").write_text("0x028000\n")
        out_path = self.tmp / "report.json"
        self.run_cli("--root", str(self.tmp), "--out", str(out_path))
        _, out, _ = self.run_cli("--show", "--out", str(out_path))
        self.assertIn("SIN CONTROLADOR", out)


if __name__ == "__main__":
    unittest.main()
