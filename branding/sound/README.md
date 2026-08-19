# Castalia sound palette — specification

One short, warm motif unifies every system sound (Bible §21.4): a rising
**E–G♯–B** arpeggio in **E Phrygian dominant** (E F G♯ A B C D) — the
"Spanish" mode. The palette's Mediterranean tint lives in the mode's color
tones **F** and **C**, heard plainly in the error sound's minor-second bite.

**The spec is data:** [`palette.toml`](palette.toml) defines the notes,
events, envelopes and levels. [`tools/sound_gen.py`](../../tools/sound_gen.py)
renders it **deterministically** (no randomness — identical input, identical
bytes) to [`wav/`](wav/) and builds the interactive board at
[`docs/sound-preview.html`](../../docs/sound-preview.html).

## The family

| Sound | When | Design |
|---|---|---|
| `startup` (1.8 s) | Session start | Low E pad swells under the rising motif, crowned an octave up |
| `shutdown` (1.6 s) | Session end | The motif descends home, resolving onto the low pad |
| `notify` (0.55 s) | Toast/message | Two soft notes, question-and-answer, deliberately quiet |
| `error` (0.7 s) | Blocking error | The Phrygian bite (F + C against E's world) — clear, brief, never harsh |
| `device-in` (0.45 s) | USB/media inserted | A rising fifth: something arrived |
| `device-out` (0.45 s) | USB/media removed | The same fifth falling: safely gone |
| `empty-trash` (0.7 s) | Bin emptied | Three quick steps down to home |

## Engineering rules

1. **Every sound ≤ 2 s** and normalized to **−3 dBFS** peak (enforced by
   `tools/tests/test_sound.py`).
2. **Mono, 44.1 kHz, 16-bit WAV** as the build artifact; OGG transcodes are
   produced at package time.
3. **The global mute is law** — no Castalia component ever plays a sound past
   the user's mute (Bible §8.6).
4. **Original compositions only** (Bible §3.5): nothing sampled from, or
   recreated "by ear" from, any Microsoft sound. Every rendered file carries a
   provenance ledger row.
5. Voices: *pluck* = decaying partial stack with a +0.3 % detuned twin
   (warmth); *pad* = slow sine swell. Deliberately simple — the motif, not the
   patch, is the identity.
