#!/usr/bin/env python3
"""Castalia sound palette renderer.

Reads ``branding/sound/palette.toml`` (the sound spec as data, Bible §21.4)
and deterministically synthesizes the system sounds to
``branding/sound/wav/*.wav`` — plus an optional self-contained HTML sound
board (``docs/sound-preview.html``) with embedded audio and pre-computed
waveform sketches.

Synthesis is intentionally simple and warm: each *pluck* is a small stack of
decaying partials with a gentle detuned second voice; each *pad* swells in
and out. No randomness anywhere — identical input produces identical bytes,
so the WAVs are reproducible build artifacts.

Usage:
    PYTHONPATH=tools python3 tools/sound_gen.py                # render wavs
    PYTHONPATH=tools python3 tools/sound_gen.py --board        # + HTML board
    PYTHONPATH=tools python3 tools/sound_gen.py --out DIR --board-file FILE
"""

from __future__ import annotations

import argparse
import base64
import json
import math
import sys
import tomllib
import wave
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
PALETTE = REPO / "branding" / "sound" / "palette.toml"


def _rel(path: Path) -> str:
    """Display a path relative to the repo when it lives under it (the normal
    case), otherwise as given — so an out-of-tree --out (e.g. /tmp in CI) does
    not raise from Path.relative_to()."""
    try:
        return str(path.relative_to(REPO))
    except ValueError:
        return str(path)

# partial stack for the "pluck" voice: (harmonic multiple, level, decay speed)
PLUCK_PARTIALS = [(1, 1.00, 1.0), (2, 0.45, 1.6), (3, 0.22, 2.2), (4, 0.10, 3.0)]
DETUNE = 1.003          # second voice, +0.3% — gentle chorus warmth
ATTACK_S = 0.008


def load_palette(path: Path = PALETTE) -> dict:
    return tomllib.loads(path.read_text(encoding="utf-8"))


def _note_freq(palette: dict, name: str) -> float:
    try:
        return float(palette["notes"][name])
    except KeyError:
        raise SystemExit(f"sound-gen: unknown note {name!r} in palette")


def _render_event(buf: list[float], sr: int, freq: float, at: float,
                  dur: float, level: float, kind: str) -> None:
    start = int(at * sr)
    n = int(dur * sr)
    attack = max(1, int(ATTACK_S * sr))
    for i in range(n):
        t = i / sr
        if kind == "pad":
            # sine pair with slow swell in/out
            env = math.sin(math.pi * (i / n)) ** 1.5
            s = (math.sin(2 * math.pi * freq * t)
                 + 0.5 * math.sin(2 * math.pi * freq * DETUNE * t))
            sample = 0.6 * env * s
        else:
            # pluck: decaying partial stack, detuned twin
            env_a = min(1.0, i / attack)
            s = 0.0
            for mult, lvl, speed in PLUCK_PARTIALS:
                decay = math.exp(-speed * 5.0 * t / dur)
                s += lvl * decay * math.sin(2 * math.pi * freq * mult * t)
                s += 0.4 * lvl * decay * math.sin(
                    2 * math.pi * freq * mult * DETUNE * t)
            sample = env_a * s / 2.2
        idx = start + i
        if idx < len(buf):
            buf[idx] += level * sample


def render_sound(palette: dict, sound: dict) -> list[float]:
    """Render one sound to a float buffer, normalized to the peak target."""
    g = palette["global"]
    sr = int(g["samplerate"])
    total = int(float(sound["duration"]) * sr)
    buf = [0.0] * total
    for ev in sound["events"]:
        _render_event(buf, sr, _note_freq(palette, ev["note"]),
                      float(ev["at"]), float(ev["dur"]),
                      float(ev["level"]), ev.get("type", "pluck"))
    peak_target = 10 ** (float(g["peak_dbfs"]) / 20.0)
    peak = max(1e-9, max(abs(s) for s in buf))
    scale = peak_target / peak
    return [s * scale for s in buf]


def write_wav(path: Path, samples: list[float], sr: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    frames = bytearray()
    for s in samples:
        v = max(-1.0, min(1.0, s))
        frames += int(v * 32767).to_bytes(2, "little", signed=True)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes(bytes(frames))


def waveform_peaks(samples: list[float], buckets: int = 160) -> list[float]:
    """Compact per-bucket peak sketch for the board's canvas drawing."""
    if not samples:
        return []
    size = max(1, len(samples) // buckets)
    return [round(max(abs(s) for s in samples[i:i + size]), 3)
            for i in range(0, size * buckets, size)]


# ------------------------------------------------------------------ board --

BOARD_CSS = """
:root{--bg:#101519;--ink:#E8E4DC;--ink2:#93A0AC;--card:#171E25;
  --line:#26303A;--accent:#6FA7C4}
:root[data-theme="light"]{--bg:#ECEDEA;--ink:#22262B;--ink2:#5D6873;
  --card:#F7F7F4;--line:#D3D6D0;--accent:#2C6699}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--ink);
  font-family:Verdana,'DejaVu Sans',Geneva,Tahoma,sans-serif;
  font-size:14px;line-height:1.55}
.page{max-width:980px;margin:0 auto;padding:26px 20px 48px;
  display:flex;flex-direction:column;gap:20px}
h1{font-size:22px;margin:0;letter-spacing:-.01em}
.eyebrow{margin:0;font-size:11px;text-transform:uppercase;
  letter-spacing:.14em;color:var(--accent)}
.sub{margin:0;color:var(--ink2);max-width:72ch;font-size:13px}
code{font-family:'Cascadia Mono','DejaVu Sans Mono',Consolas,monospace;
  font-size:.92em}
.motif{display:flex;gap:10px;align-items:center;flex-wrap:wrap;
  background:var(--card);border:1px solid var(--line);border-radius:12px;
  padding:14px 18px}
.motif .m-note{display:inline-flex;align-items:center;justify-content:center;
  min-width:52px;height:36px;border-radius:8px;font-weight:700;
  background:linear-gradient(180deg,#3E82B6,#2C6699);color:#fff}
.motif .m-lab{font-size:12px;color:var(--ink2)}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(280px,1fr));gap:14px}
.snd{background:var(--card);border:1px solid var(--line);border-radius:12px;
  padding:14px 16px;display:flex;flex-direction:column;gap:9px}
.snd h3{margin:0;font-size:14.5px;display:flex;align-items:baseline;gap:8px}
.snd h3 code{color:var(--ink2);font-weight:400;font-size:11px}
.snd .when{font-size:11px;color:var(--accent);text-transform:uppercase;
  letter-spacing:.08em}
.snd p{margin:0;font-size:12px;color:var(--ink2)}
.row{display:flex;gap:10px;align-items:center}
.play{flex:none;width:42px;height:42px;border-radius:50%;cursor:pointer;
  border:1px solid var(--accent);background:linear-gradient(180deg,#3E82B6,#2C6699);
  color:#fff;font-size:15px;display:flex;align-items:center;justify-content:center}
.play:focus-visible{outline:2px solid var(--accent);outline-offset:2px}
.play.playing{box-shadow:0 0 0 4px rgba(93,155,200,.3)}
canvas{width:100%;height:46px;display:block;border-radius:6px;
  background:rgba(62,130,182,.08)}
.meta{display:flex;gap:8px;flex-wrap:wrap}
.meta code{background:rgba(62,130,182,.14);border-radius:4px;
  padding:1px 8px;font-size:11px}
.foot{font-size:12px;color:var(--ink2);max-width:80ch}
"""

BOARD_JS = """
document.querySelectorAll('.play').forEach(function(btn){
  var audio=new Audio(btn.getAttribute('data-src'));
  btn.addEventListener('click',function(){
    document.querySelectorAll('.play.playing').forEach(function(b){
      b.classList.remove('playing');});
    audio.currentTime=0; audio.play();
    btn.classList.add('playing');
    audio.onended=function(){btn.classList.remove('playing');};
  });
});
document.querySelectorAll('canvas[data-peaks]').forEach(function(cv){
  var peaks=JSON.parse(cv.getAttribute('data-peaks'));
  var dpr=window.devicePixelRatio||1;
  var w=cv.clientWidth*dpr,h=cv.clientHeight*dpr;
  cv.width=w; cv.height=h;
  var ctx=cv.getContext('2d');
  var accent=getComputedStyle(document.documentElement)
    .getPropertyValue('--accent').trim()||'#6FA7C4';
  ctx.fillStyle=accent;
  var bw=w/peaks.length;
  peaks.forEach(function(p,i){
    var bh=Math.max(1*dpr,p*h*0.92);
    ctx.fillRect(i*bw, (h-bh)/2, Math.max(1,bw*0.6), bh);
  });
});
"""


def build_board(palette: dict, rendered: dict[str, list[float]]) -> str:
    g = palette["global"]
    sr = int(g["samplerate"])
    motif = " ".join(
        f'<span class="m-note">{n}</span>' for n in g.get("motif", []))
    cards = []
    for snd in palette["sound"]:
        sid = snd["id"]
        samples = rendered[sid]
        wav_path = REPO / "branding" / "sound" / "wav" / f"{sid}.wav"
        b64 = base64.b64encode(wav_path.read_bytes()).decode("ascii")
        peaks = json.dumps(waveform_peaks(samples))
        notes = " · ".join(dict.fromkeys(e["note"] for e in snd["events"]))
        cards.append(f"""
  <div class="snd">
    <span class="when">{snd['when']}</span>
    <h3>{snd['name']} <code>{sid}.wav</code></h3>
    <p>{snd['description']}</p>
    <div class="row">
      <button class="play" data-src="data:audio/wav;base64,{b64}"
              aria-label="Reproducir {snd['name']}">▶</button>
      <canvas data-peaks='{peaks}' aria-hidden="true"></canvas>
    </div>
    <div class="meta"><code>{notes}</code>
      <code>{float(snd['duration']):.2f}&nbsp;s</code></div>
  </div>""")

    return f"""<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Castalia OS — Paleta de sonido</title>
<style>{BOARD_CSS}</style>
</head>
<body>
<div class="page">
  <header>
    <p class="eyebrow">Tombatossals Softworks · paleta de sonido · §21.4</p>
    <h1>Castalia OS — Paleta de sonido</h1>
    <p class="sub">Un único motivo une todos los sonidos del sistema: un arpegio ascendente
    en <strong>{g['mode']}</strong> — el modo «español», cuyo color mediterráneo aparece en
    las notas F y C (escúchalo en el mordisco del sonido de error). Cada archivo se
    sintetiza de forma <em>determinista</em> desde <code>branding/sound/palette.toml</code>
    por <code>tools/sound_gen.py</code>: la especificación es datos, el audio es un
    artefacto de build reproducible.</p>
  </header>

  <div class="motif">{motif}
    <span class="m-lab">el motivo — pícalo en cualquier sonido ·
    pico normalizado a {g['peak_dbfs']} dBFS · todo ≤ {g['max_duration_s']} s ·
    {sr} Hz mono</span></div>

  <div class="grid">{''.join(cards)}
  </div>

  <p class="foot">Composiciones originales de Tombatossals Softworks — nada muestreado,
  nada recreado «de oído» de Microsoft (Bible §3.5). El interruptor global de silencio
  del sistema siempre se respeta. Regenerar: <code>PYTHONPATH=tools python3
  tools/sound_gen.py --board</code>.</p>
</div>
<script>{BOARD_JS}</script>
</body>
</html>
"""


# ------------------------------------------------------------------- main --

def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path,
                        default=REPO / "branding" / "sound" / "wav")
    parser.add_argument("--board", action="store_true",
                        help="also write the HTML sound board")
    parser.add_argument("--board-file", type=Path,
                        default=REPO / "docs" / "sound-preview.html")
    args = parser.parse_args(argv[1:])

    palette = load_palette()
    sr = int(palette["global"]["samplerate"])
    max_dur = float(palette["global"]["max_duration_s"])

    rendered: dict[str, list[float]] = {}
    for snd in palette["sound"]:
        if float(snd["duration"]) > max_dur:
            raise SystemExit(
                f"sound-gen: {snd['id']} exceeds max_duration_s={max_dur}")
        samples = render_sound(palette, snd)
        rendered[snd["id"]] = samples
        out = args.out / f"{snd['id']}.wav"
        write_wav(out, samples, sr)
        print(f"sound-gen: wrote {_rel(out)} "
              f"({len(samples)/sr:.2f}s, {out.stat().st_size//1024} KiB)")

    if args.board:
        html = build_board(palette, rendered)
        args.board_file.parent.mkdir(parents=True, exist_ok=True)
        args.board_file.write_text(html, encoding="utf-8")
        print(f"sound-gen: wrote {_rel(args.board_file)} "
              f"({args.board_file.stat().st_size//1024} KiB)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
