# Known issues

*Last verified on version 0.1.1.*

What is wrong with Castalia right now, kept current rather than written at
release time. §23.7 makes an honest version of this file a condition of going
public at all, so entries are removed when they are fixed and not before.

Severity uses the Bible's labels: `sev:data-loss` and `sev:brick` are release
blockers (§19.4).

---

## Not yet implemented

These are absences, not bugs. They are here because the difference is not
visible from the outside.

| Area | What is missing | Where it is tracked |
|---|---|---|
| Installer | Manual partitioning. The three guided modes are the only options. | §14.3 |
| Installer | **GPT.** Only msdos labels, which hold four primary partitions, and a Castalia layout needs three. **A disk that already has two partitions can host neither dual-boot mode** — the only offer is whole-disk. | §14.3 |
| Recovery | A graphical Recovery Center. Restore Points are reachable from the Control Center and from the boot-time console; there is no separate GUI. | §9 |
| Hardware | `hwprobe` records its decisions but applies none of them. Nothing writes an `xorg.conf.d` snippet or a modprobe blacklist. | §6.15 |
| Hardware | The quirks table has three rows, and none claims suspend is safe on anything, because no machine has been certified yet. | §19 |
| Help | The **Help Center app** (`castalia-bienvenida`, F1) still carries its own short built-in topics and does not link the full manual. The manual ships and is reachable with `castalia-manual`; wiring the app to it is a C++ change that has not been made. | §20 |
| Performance | Boot time (§16.1) and launch-menu latency (§16.3) are **not measured**. The perf gate prints this on every run. | §16.4 |
| Performance | Idle desktop memory (≤170 MB, §16.2) is unmeasured, and since §16.5 raised the shell budget to 84 MB it is the binding constraint. | §16.2 |

## Open issues

| Severity | Area | Issue |
|---|---|---|
| — | — | *No open `sev:data-loss`, `sev:brick` or `type:legal` issues.* |

## Hardware

**No machine has been certified on real hardware.** Every claim Castalia makes
about booting, installing and running is currently backed by QEMU, plus the
loopback and shrink smokes on real block devices. §19.2's certification matrix
is empty, and §23.7 #1 and #6 cannot be met until it is not.

That is the single biggest gap between this project and a public alpha, and it
is not one that more code fixes.

## Things that behave surprisingly, on purpose

Not bugs. Listed because they generate the same support question repeatedly.

**The installer refuses to resize a hibernated Windows.** A Windows left in
hibernation or with Fast Startup enabled leaves its filesystem marked dirty,
and Castalia declines rather than resizing under it. Turn off Fast Startup
(`powercfg /h off`), reboot Windows, and shut it *down* rather than
restarting. See [the dual-boot guide](../install/dual-boot.md).

**A shrink sometimes demands you free *more* space, not less.** On a partition
that reaches past the first 128 GiB of the disk, freeing too little would leave
`/boot` where a vintage BIOS cannot read the kernel (§6.2). The installer says
so on the same screen as the question.

**"Install alongside" is not offered on an empty disk.** Technically it would
work — the whole disk is free space — but it would be a choice between two
spellings of the same install, one named after a neighbour that does not exist.

**Restore Points do not restore `/home`.** By design (§9). Restoring fixes the
system and does not undo your work.
