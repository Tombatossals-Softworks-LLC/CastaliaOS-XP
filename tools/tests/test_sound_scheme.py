"""Tests that the sound scheme stays consistent end to end.

branding/sound/palette.toml is the spec, tools/sound_gen.py renders it to
branding/sound/wav/*.wav, legal/ASSET_PROVENANCE.csv must account for every
shipped WAV, and libcastalia-ui's Sound.h names the same ids for the apps.
Four places that can drift apart — these tests keep them honest.
"""

import re
import tomllib
import unittest
import wave
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
PALETTE = REPO / "branding" / "sound" / "palette.toml"
WAVDIR = REPO / "branding" / "sound" / "wav"
PROVENANCE = REPO / "legal" / "ASSET_PROVENANCE.csv"
SOUND_H = REPO / "shell" / "libcastalia-ui" / "Sound.h"
SOUND_CPP = REPO / "shell" / "libcastalia-ui" / "Sound.cpp"
SESSION = REPO / "shell" / "session" / "castalia-session"


def palette():
    with PALETTE.open("rb") as fh:
        return tomllib.load(fh)


def sound_ids():
    return [s["id"] for s in palette()["sound"]]


class PaletteTest(unittest.TestCase):
    def test_every_sound_has_the_fields_the_renderer_needs(self):
        for s in palette()["sound"]:
            for key in ("id", "name", "when", "duration", "events"):
                self.assertIn(key, s, s.get("id"))
            self.assertRegex(s["id"], r"^[a-z][a-z0-9-]*$")
            self.assertTrue(s["events"], f"{s['id']}: no events")

    def test_ids_are_unique(self):
        ids = sound_ids()
        self.assertEqual(len(ids), len(set(ids)))

    def test_every_event_note_is_defined_and_fits_the_duration(self):
        p = palette()
        notes = p["notes"]
        for s in p["sound"]:
            for ev in s["events"]:
                self.assertIn(ev["note"], notes, f"{s['id']}: {ev['note']}")
                self.assertLessEqual(
                    ev["at"], s["duration"],
                    f"{s['id']}: event starts after the sound ends")

    def test_sounds_respect_the_two_second_budget(self):
        # Bible §8.6: every system sound is short.
        limit = palette()["global"]["max_duration_s"]
        for s in palette()["sound"]:
            self.assertLessEqual(s["duration"], limit, s["id"])


class RenderedWavTest(unittest.TestCase):
    def test_every_sound_has_a_rendered_wav(self):
        for sid in sound_ids():
            self.assertTrue((WAVDIR / f"{sid}.wav").is_file(),
                            f"{sid}.wav not rendered (run tools/sound_gen.py)")

    def test_no_orphan_wavs(self):
        rendered = {p.stem for p in WAVDIR.glob("*.wav")}
        self.assertEqual(rendered - set(sound_ids()), set(),
                         "WAV with no entry in palette.toml")

    def test_wavs_match_the_declared_format_and_length(self):
        g = palette()["global"]
        for s in palette()["sound"]:
            with wave.open(str(WAVDIR / f"{s['id']}.wav")) as w:
                self.assertEqual(w.getframerate(), g["samplerate"], s["id"])
                self.assertEqual(w.getnchannels(), g["channels"], s["id"])
                self.assertEqual(w.getsampwidth() * 8, g["bit_depth"], s["id"])
                seconds = w.getnframes() / w.getframerate()
                self.assertAlmostEqual(seconds, s["duration"], places=2,
                                       msg=f"{s['id']}: unexpected length")


class ProvenanceTest(unittest.TestCase):
    def test_every_wav_is_provenance_tracked(self):
        rows = PROVENANCE.read_text(encoding="utf-8")
        for sid in sound_ids():
            self.assertIn(f"branding/sound/wav/{sid}.wav", rows,
                          f"{sid}.wav has no provenance row (Bible §3.9)")


class PlaybackWiringTest(unittest.TestCase):
    """The C++ helper and the sh session must know the same sound ids."""

    def cpp_ids(self):
        return set(re.findall(r'return QStringLiteral\("([a-z0-9-]+)"\);',
                              SOUND_CPP.read_text(encoding="utf-8")))

    def test_cpp_enum_covers_every_palette_sound(self):
        self.assertEqual(self.cpp_ids(), set(sound_ids()),
                         "Sound.cpp ids and palette.toml disagree")

    def test_cpp_enum_and_id_mapping_are_the_same_size(self):
        header = SOUND_H.read_text(encoding="utf-8")
        block = header.split("enum class Sound", 1)[1].split("};", 1)[0]
        members = re.findall(r"^\s*([A-Z][A-Za-z]*)\s*,", block, re.M)
        self.assertEqual(len(members), len(sound_ids()),
                         "Sound enum size differs from the palette")

    def test_playback_honours_the_kill_switch_everywhere(self):
        # Both implementations must respect CASTALIA_NO_SOUND, or CI and
        # kiosks would start spawning audio processes.
        self.assertIn("CASTALIA_NO_SOUND",
                      SOUND_CPP.read_text(encoding="utf-8"))
        self.assertIn("CASTALIA_NO_SOUND",
                      SESSION.read_text(encoding="utf-8"))

    def test_offscreen_renders_stay_silent(self):
        # The render gate runs every app; none of them may play audio.
        self.assertIn("offscreen", SOUND_CPP.read_text(encoding="utf-8"))

    def test_session_plays_startup_and_shutdown(self):
        text = SESSION.read_text(encoding="utf-8")
        self.assertIn("play_sound startup", text)
        self.assertIn("play_sound shutdown", text)


if __name__ == "__main__":
    unittest.main()
