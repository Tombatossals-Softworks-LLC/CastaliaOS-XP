# Hardware Compatibility Guide

*Last verified on version 0.1.1.*

Which machines Castalia has been tested on, what worked, and what did not.

---

## Nothing has been certified yet

This is the honest state of the guide, and it is stated at the top rather than
implied by an empty table below.

**No machine has completed §19.2's real-hardware certification.** Every claim
Castalia currently makes about booting, installing and running is backed by:

- QEMU boots of both architectures, the 32-bit one on a CPU model that cannot
  execute 64-bit code;
- real block devices for the destructive paths — the loopback install, the
  alongside install with checksums either side, and the NTFS shrink with a
  file deliberately placed past the new partition boundary;
- an install-to-disk-and-boot exercise under QEMU.

That is a great deal more than nothing, and it is not the same as a Pentium 4
in a room. §23.7 #1 and #6 require real hardware, and they are not met.

## The matrix this file will hold (§19)

| Machine class | Role | Status |
|---|---|---|
| Pentium 4 desktop, 512 MB, GMA-class GPU | **FLOOR** (§16) | not tested |
| Core 2 Duo desktop, 2 GB | **TARGET** (§16) | not tested |
| An early laptop (suspend, lid, brightness) | mobility | not tested |
| Intel GMA | GPU | not tested |
| NVIDIA 6/7/8-series (nouveau) | GPU | not tested |
| ATI Radeon 9000/X/HD-2000–4000 (radeon) | GPU | not tested |

Per machine, certification records: boots, installs, sound, wired networking,
Wi-Fi, printing, suspend/resume, and one Wine application running.

## Contributing a result

This is the part that does not need a maintainer — it needs a machine.

1. Boot the live image on it. Do not install yet.
2. Run the hardware probe and keep the output:

   ```sh
   castalia-hwprobe --dry-run > castalia-hwprobe-$(hostname).json
   ```

3. Check, and write down which worked: screen at native resolution, sound,
   wired network, Wi-Fi, USB storage, suspend and resume.
4. If you can spare the disk, install and confirm it boots.
5. Open an issue with the JSON and the list. Anything that did **not** work is
   the more useful half — a report of six things working and one silence is a
   report with a hole in it.

A quirk row only enters the shipped table when a machine is behind it. A row
added without one is a rumour with a version number.
