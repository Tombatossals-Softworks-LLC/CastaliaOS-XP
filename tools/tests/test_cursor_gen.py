"""Tests for the Xcursor theme generator (tools/cursor_gen.py).

The cursor theme ships as real Xcursor binaries, so the container format has
to be exactly right or X silently falls back to the default pointer. These
tests parse what `pack_xcursor` writes back out with an independent `struct`
reader — deliberately not reusing the writer's own constants — and check the
theme's shape (every source has a hotspot, aliases point at real shapes).

Pure Python by design: the lint/unit CI job has no image tooling at all, so
nothing here rasterises — the PNG decoder is exercised against bytes written
by a tiny encoder in this file. The rasterised output is validated separately
against the real libXcursor during local verification.
"""

import struct
import sys
import unittest
import zlib
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools"))

import cursor_gen  # noqa: E402

SRC = REPO / "themes" / "cursors" / "src"


def make_image(size, xhot=0, yhot=0, delay=0, fill=0xFF804020):
    """A synthetic square image whose pixels are all `fill`."""
    return {
        "size": size, "width": size, "height": size,
        "xhot": xhot, "yhot": yhot, "delay": delay,
        "pixels": struct.pack("<I", fill) * (size * size),
    }


def parse_xcursor(data):
    """Independently parse an Xcursor file into a list of image dicts."""
    magic, header, version, ntoc = struct.unpack_from("<4sIII", data, 0)
    assert magic == b"Xcur", magic
    assert header == 16, header
    assert version == 0x00010000, version
    out = []
    for i in range(ntoc):
        ctype, subtype, pos = struct.unpack_from("<III", data, 16 + 12 * i)
        (chdr, ctype2, subtype2, cver, w, h, xhot, yhot,
         delay) = struct.unpack_from("<IIIIIIIII", data, pos)
        assert chdr == 36, chdr
        assert ctype2 == ctype, (ctype, ctype2)
        assert subtype2 == subtype, (subtype, subtype2)
        px_off = pos + 36
        pixels = data[px_off:px_off + w * h * 4]
        out.append({
            "type": ctype, "size": subtype, "version": cver,
            "width": w, "height": h, "xhot": xhot, "yhot": yhot,
            "delay": delay, "pixels": pixels,
        })
    return out


class PackXcursorTest(unittest.TestCase):
    def test_roundtrips_a_single_image(self):
        img = make_image(24, xhot=1, yhot=2, delay=0, fill=0xFF123456)
        got = parse_xcursor(cursor_gen.pack_xcursor([img]))
        self.assertEqual(len(got), 1)
        one = got[0]
        self.assertEqual(one["size"], 24)
        self.assertEqual((one["width"], one["height"]), (24, 24))
        self.assertEqual((one["xhot"], one["yhot"]), (1, 2))
        self.assertEqual(one["version"], 1)
        self.assertEqual(one["pixels"], img["pixels"])

    def test_roundtrips_every_size_in_one_file(self):
        sizes = [24, 32, 48]
        data = cursor_gen.pack_xcursor([make_image(s) for s in sizes])
        got = parse_xcursor(data)
        self.assertEqual([g["size"] for g in got], sizes)
        for g in got:
            self.assertEqual(len(g["pixels"]), g["width"] * g["height"] * 4)

    def test_chunks_are_contiguous_and_in_bounds(self):
        data = cursor_gen.pack_xcursor([make_image(s) for s in (24, 32)])
        _, _, _, ntoc = struct.unpack_from("<4sIII", data, 0)
        positions = [struct.unpack_from("<III", data, 16 + 12 * i)[2]
                     for i in range(ntoc)]
        self.assertEqual(positions, sorted(positions))
        self.assertGreaterEqual(positions[0], 16 + 12 * ntoc)
        # every chunk (header + pixels) lies inside the file
        for pos in positions:
            w, h = struct.unpack_from("<II", data, pos + 16)
            self.assertLessEqual(pos + 36 + w * h * 4, len(data))
        self.assertEqual(positions[-1] + 36 + 32 * 32 * 4, len(data))

    def test_image_chunk_type_is_the_spec_value(self):
        got = parse_xcursor(cursor_gen.pack_xcursor([make_image(24)]))
        self.assertEqual(got[0]["type"], 0xFFFD0002)


class PremultiplyTest(unittest.TestCase):
    def test_opaque_pixel_is_unchanged_and_byte_order_is_bgra(self):
        # RGBA in → little-endian ARGB (B,G,R,A on disk) out
        out = cursor_gen.premultiplied_argb(bytes([0x10, 0x20, 0x30, 0xFF]))
        self.assertEqual(out, bytes([0x30, 0x20, 0x10, 0xFF]))

    def test_transparent_pixel_premultiplies_to_zero(self):
        out = cursor_gen.premultiplied_argb(bytes([0xFF, 0xFF, 0xFF, 0x00]))
        self.assertEqual(out, bytes([0, 0, 0, 0]))

    def test_half_alpha_scales_the_colour(self):
        out = cursor_gen.premultiplied_argb(bytes([0xFF, 0x00, 0x00, 0x80]))
        b, g, r, a = out
        self.assertEqual((b, g, a), (0, 0, 0x80))
        self.assertEqual(r, (0xFF * 0x80 + 127) // 255)

    def test_channels_never_exceed_alpha(self):
        # premultiplied ARGB is invalid if any colour channel > alpha
        for alpha in (0, 1, 64, 128, 254, 255):
            out = cursor_gen.premultiplied_argb(
                bytes([0xFF, 0xFF, 0xFF, alpha]))
            self.assertLessEqual(max(out[0], out[1], out[2]), out[3])


def encode_png(width, height, rgba, colour=6, filters=None):
    """Minimal PNG encoder used to feed the decoder known bytes."""
    channels = 4 if colour == 6 else 3
    stride = width * channels
    raw = bytearray()
    for y in range(height):
        ftype = 0 if filters is None else filters[y]
        raw.append(ftype)
        row = bytearray()
        for x in range(width):
            s = (y * width + x) * 4
            row += bytes(rgba[s:s + channels])
        if ftype == 0:
            raw += row
        elif ftype == 1:        # Sub
            enc = bytearray(row)
            for i in range(stride - 1, channels - 1, -1):
                enc[i] = (row[i] - row[i - channels]) & 0xFF
            raw += enc
        elif ftype == 2:        # Up
            prev_row = bytearray()
            for x in range(width):
                s = ((y - 1) * width + x) * 4
                prev_row += bytes(rgba[s:s + channels]) if y else b"\0" * channels
            raw += bytes((row[i] - prev_row[i]) & 0xFF for i in range(stride))
        else:
            raise AssertionError("test encoder supports filters 0/1/2")

    def chunk(tag, body):
        return (struct.pack(">I", len(body)) + tag + body
                + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF))

    ihdr = struct.pack(">IIBBBBB", width, height, 8, colour, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr)
            + chunk(b"IDAT", zlib.compress(bytes(raw)))
            + chunk(b"IEND", b""))


class DecodePngTest(unittest.TestCase):
    def test_decodes_rgba_unfiltered(self):
        rgba = bytes([255, 0, 0, 255, 0, 255, 0, 128,
                      0, 0, 255, 255, 9, 9, 9, 0])
        w, h, got = cursor_gen.decode_png_rgba(encode_png(2, 2, rgba))
        self.assertEqual((w, h), (2, 2))
        self.assertEqual(got, rgba)

    def test_decodes_rgb_and_fills_opaque_alpha(self):
        rgba = bytes([10, 20, 30, 255, 40, 50, 60, 255])
        w, h, got = cursor_gen.decode_png_rgba(encode_png(2, 1, rgba, colour=2))
        self.assertEqual((w, h), (2, 1))
        self.assertEqual(got, rgba)

    def test_undoes_sub_and_up_filters(self):
        rgba = bytes(range(4 * 3 * 2))  # 3x2 RGBA, varied values
        for filters in ([1, 1], [2, 2], [0, 2], [1, 2]):
            png = encode_png(3, 2, rgba, filters=filters)
            _, _, got = cursor_gen.decode_png_rgba(png)
            self.assertEqual(got, rgba, f"filters={filters}")

    def test_rejects_non_png(self):
        with self.assertRaises(ValueError):
            cursor_gen.decode_png_rgba(b"not a png at all")

    def test_rejects_unsupported_png(self):
        # 16-bit depth is not something our rasterisers emit
        bad = bytearray(encode_png(1, 1, bytes([0, 0, 0, 255])))
        bad[24] = 16  # IHDR bit depth byte
        with self.assertRaises(ValueError):
            cursor_gen.decode_png_rgba(bytes(bad))


class RasteriserTest(unittest.TestCase):
    def test_prefers_librsvg_over_imagemagick(self):
        names = [name for name, _ in cursor_gen.RASTERISERS]
        self.assertEqual(names[0], "rsvg-convert",
                         "rsvg-convert must be preferred: ImageMagick's SVG "
                         "support depends on a delegate CI images may lack")
        self.assertIn("convert", names, "keep an ImageMagick fallback")

    def test_every_rasteriser_builds_a_argv_with_the_size(self):
        for name, argv in cursor_gen.RASTERISERS:
            cmd = argv(Path("/tmp/x.svg"), 32)
            self.assertEqual(cmd[0], name)
            self.assertTrue(any("32" in part for part in cmd), cmd)
            self.assertTrue(any("x.svg" in part for part in cmd), cmd)


class ThemeShapeTest(unittest.TestCase):
    def shapes(self):
        return sorted(p.stem for p in SRC.glob("*.svg"))

    def test_sources_exist(self):
        self.assertGreaterEqual(len(self.shapes()), 9, "cursor set shrank?")

    def test_every_source_has_a_hotspot(self):
        for name in self.shapes():
            self.assertIn(name, cursor_gen.HOTSPOTS,
                          f"{name}.svg has no hotspot in cursor_gen.HOTSPOTS")

    def test_hotspots_are_inside_the_source_grid(self):
        for name, (x, y) in cursor_gen.HOTSPOTS.items():
            self.assertTrue(0 <= x < 24 and 0 <= y < 24, (name, x, y))

    def test_aliases_only_target_real_shapes(self):
        shapes = set(self.shapes())
        for target in cursor_gen.ALIASES:
            self.assertIn(target, shapes,
                          f"alias group targets missing shape {target}")

    def test_aliases_are_unique_and_never_shadow_a_shape(self):
        seen = set()
        shapes = set(self.shapes())
        for target, names in cursor_gen.ALIASES.items():
            for alias in names:
                self.assertNotIn(alias, seen, f"duplicate alias {alias}")
                self.assertNotIn(
                    alias, shapes,
                    f"alias {alias} would overwrite the {alias} cursor")
                seen.add(alias)
                self.assertTrue(target)

    def test_the_pointer_names_applications_need_are_covered(self):
        provided = set(cursor_gen.ALIASES) | {
            a for names in cursor_gen.ALIASES.values() for a in names}
        for required in ("default", "left_ptr", "text", "pointer", "wait",
                         "move", "help", "ew-resize", "ns-resize",
                         "crosshair"):
            self.assertIn(required, provided, f"no cursor named {required}")


if __name__ == "__main__":
    unittest.main()
