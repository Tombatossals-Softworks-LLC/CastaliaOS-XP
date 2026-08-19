"""Unit tests for the sound palette renderer."""

import sys
import tempfile
import unittest
import wave
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))

import sound_gen  # noqa: E402


class SoundGenTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.palette = sound_gen.load_palette()
        cls.g = cls.palette["global"]
        cls.rendered = {
            s["id"]: sound_gen.render_sound(cls.palette, s)
            for s in cls.palette["sound"]
        }

    def test_all_seven_sounds_defined(self):
        ids = {s["id"] for s in self.palette["sound"]}
        self.assertEqual(ids, {"startup", "shutdown", "notify", "error",
                               "device-in", "device-out", "empty-trash"})

    def test_durations_within_budget(self):
        for s in self.palette["sound"]:
            self.assertLessEqual(float(s["duration"]),
                                 float(self.g["max_duration_s"]), s["id"])

    def test_peak_normalized_to_target(self):
        target = 10 ** (float(self.g["peak_dbfs"]) / 20.0)
        for sid, samples in self.rendered.items():
            peak = max(abs(v) for v in samples)
            self.assertAlmostEqual(peak, target, places=3, msg=sid)

    def test_no_clipping(self):
        for sid, samples in self.rendered.items():
            self.assertLessEqual(max(abs(v) for v in samples), 1.0, sid)

    def test_deterministic(self):
        snd = self.palette["sound"][0]
        again = sound_gen.render_sound(self.palette, snd)
        self.assertEqual(self.rendered[snd["id"]], again)

    def test_wav_files_are_valid(self):
        with tempfile.TemporaryDirectory() as tmp:
            sr = int(self.g["samplerate"])
            for sid, samples in self.rendered.items():
                path = Path(tmp) / f"{sid}.wav"
                sound_gen.write_wav(path, samples, sr)
                with wave.open(str(path)) as w:
                    self.assertEqual(w.getnchannels(), 1, sid)
                    self.assertEqual(w.getsampwidth(), 2, sid)
                    self.assertEqual(w.getframerate(), sr, sid)
                    self.assertEqual(w.getnframes(), len(samples), sid)

    def test_waveform_peaks_compact_and_bounded(self):
        peaks = sound_gen.waveform_peaks(self.rendered["startup"])
        self.assertEqual(len(peaks), 160)
        self.assertTrue(all(0.0 <= p <= 1.0 for p in peaks))

    def test_unknown_note_is_a_clear_error(self):
        bad = {"id": "x", "duration": 0.2,
               "events": [{"note": "Z9", "at": 0, "dur": 0.1, "level": 0.5}]}
        with self.assertRaises(SystemExit):
            sound_gen.render_sound(self.palette, bad)


if __name__ == "__main__":
    unittest.main()
