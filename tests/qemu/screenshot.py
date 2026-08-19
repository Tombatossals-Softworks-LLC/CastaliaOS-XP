#!/usr/bin/env python3
"""Boot a Castalia ISO in QEMU with a real VGA and capture the framebuffer.

Used to prove the graphical desktop (Bible §18 Phase 2). QEMU renders the
guest's std-VGA display to an internal surface even with no display frontend;
the HMP `screendump` command writes it as PPM, which this tool converts to
PNG with the standard library only.

Usage:
    python3 tests/qemu/screenshot.py ISO --out shot.png [--wait 120] [--mem 768]

Exit 0 on a saved PNG, 1 otherwise. Also copies the serial log next to --out
(<out>.serial.log) for diagnosis.
"""

from __future__ import annotations

import argparse
import socket
import struct
import subprocess
import sys
import time
import zlib
from pathlib import Path


def ppm_to_png(ppm: Path, png: Path) -> tuple[int, int]:
    data = ppm.read_bytes()
    if not data.startswith(b"P6"):
        raise ValueError("not a P6 PPM")
    # parse header: P6 <w> <h> <maxval> then a single whitespace, then raw RGB
    idx = 2
    fields = []
    while len(fields) < 3:
        while idx < len(data) and data[idx] in b" \t\n\r":
            idx += 1
        if idx < len(data) and data[idx:idx + 1] == b"#":
            while idx < len(data) and data[idx] not in b"\n":
                idx += 1
            continue
        start = idx
        while idx < len(data) and data[idx] not in b" \t\n\r":
            idx += 1
        fields.append(int(data[start:idx]))
    w, h, _maxval = fields
    idx += 1  # single whitespace after maxval
    rgb = data[idx:idx + w * h * 3]

    raw = bytearray()
    for y in range(h):
        raw.append(0)
        raw += rgb[y * w * 3:(y + 1) * w * 3]

    def chunk(tag: bytes, payload: bytes) -> bytes:
        return (struct.pack(">I", len(payload)) + tag + payload
                + struct.pack(">I", zlib.crc32(tag + payload)))

    ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)
    png.write_bytes(b"\x89PNG\r\n\x1a\n"
                    + chunk(b"IHDR", ihdr)
                    + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
                    + chunk(b"IEND", b""))
    return w, h


def monitor_cmd(sock_path: str, command: str) -> None:
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(sock_path)
    time.sleep(0.3)
    try:
        s.recv(4096)  # HMP banner
    except OSError:
        pass
    s.sendall((command + "\n").encode())
    time.sleep(0.6)
    s.close()


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("iso")
    ap.add_argument("--out", required=True)
    ap.add_argument("--wait", type=int, default=120,
                    help="seconds to let the guest boot before capturing")
    ap.add_argument("--mem", type=int, default=768)
    args = ap.parse_args(argv[1:])

    iso = Path(args.iso)
    if not iso.is_file():
        print(f"screenshot: no such ISO: {iso}", file=sys.stderr)
        return 1
    out = Path(args.out)
    ppm = out.with_suffix(".ppm")
    mon = f"/tmp/castalia-mon-{iso.stem}.sock"
    serial = out.with_suffix(".serial.log")

    cmd = [
        "qemu-system-x86_64", "-m", str(args.mem), "-smp", "1", "-no-reboot",
        "-cdrom", str(iso), "-boot", "d",
        "-vga", "std", "-display", "none",
        "-serial", f"file:{serial}",
        "-monitor", f"unix:{mon},server,nowait",
    ]
    print(f"screenshot: booting {iso.name} (mem={args.mem}M), "
          f"capturing after {args.wait}s")
    proc = subprocess.Popen(cmd, stdin=subprocess.DEVNULL,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        time.sleep(args.wait)
        if proc.poll() is not None:
            print("screenshot: QEMU exited early", file=sys.stderr)
            return 1
        monitor_cmd(mon, f"screendump {ppm}")
        for _ in range(20):
            if ppm.exists() and ppm.stat().st_size > 0:
                break
            time.sleep(0.5)
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()

    if not ppm.exists() or ppm.stat().st_size == 0:
        print("screenshot: no framebuffer captured", file=sys.stderr)
        return 1
    w, h = ppm_to_png(ppm, out)
    ppm.unlink()
    print(f"screenshot: wrote {out} ({w}x{h})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
