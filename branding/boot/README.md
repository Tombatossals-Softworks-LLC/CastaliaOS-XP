# Castalia boot splash — specification

The boot splash is the product's first breath. It must feel **calm, premium,
and instant** — and it must never slow the machine down or strand a user on a
GPU the framebuffer can't drive (Bible §8.6, P1/P2).

**Canonical asset:** [`splash.svg`](splash.svg) (640×480 source; raster targets
baked at 640×480, 800×600, 1024×768). **Animated reference:**
[`docs/boot-preview.html`](../../docs/boot-preview.html) — the choreography
below, playable in a browser with slow-motion for design review.

## Visual composition

| Element | Spec |
|---|---|
| Field | Radial deep-sea gradient `#16344E → #0D1D2E → #0A141F`, centre slightly above middle |
| Mark | The Castalia keep (3× the 48-grid mark), centred, warm lit window `#F5D9A0` |
| Wordmark | `CASTALIA` caps, letter-spaced; `CLASSIC` beneath in `#7FA6C4` |
| Progress | Slim 200×6 px bar, track `#132638`, fill azure gradient `#2C6699→#3E82B6` |
| Credit | "Tombatossals Softworks", `#4E6E88`, small |
| Hint | "Esc: arranque detallado" bottom-right (drops to verbose text boot) |

## Choreography (the animated sequence)

Timings at 1× speed. Every stage is skippable; the whole sequence renders as
the static frame when `reduce-animations` is set or the framebuffer is slow.

| t (s) | Stage | Motion |
|---|---|---|
| 0.00 | Power-on | Black; vignette only |
| 0.30 | Glow | Radial field breathes in (opacity 0→1, 1.2 s ease-out) |
| 0.50 | Keep rises | Tower translates up 30 px + fades in (0.9 s ease-out) |
| 1.20 | Merlons | Three merlons pop bottom-up, 0.15 s stagger |
| 1.70 | Door & window | Fade in; window begins a slow warm pulse (4 s loop) |
| 1.90 | Sea | Wave band slides in from below the keep (0.8 s) |
| 2.10 | Wordmark | `CASTALIA` letter-spacing settles 0.6em→0.18em with fade (0.8 s) |
| 2.50 | Caption | `CLASSIC` + credit fade in |
| 2.70 | Progress | Track fades in; fill advances 0→100 % over ~3.2 s in realistic steps (38 → 52 → 86 → 100), sheen sweep 1.4 s loop |
| 2.7–5.9 | Status line | Cycles real boot phases: *Iniciando servicios… · Detectando hardware… · Montando discos… · Preparando escritorio…* |
| 5.90 | Complete | Bar glows; status → **Bienvenido** |
| 6.30 | Handoff | 0.7 s soft white bloom, then crossfade to the greeter |

## Engineering constraints (non-negotiable)

1. **The splash never delays boot.** It reads progress; it does not gate it.
   Budget: ≤ 0.3 s added to total boot, ≤ 8 MB RSS, zero GPU requirements.
2. **Framebuffer-safe.** Must render on plain KMS/VESA fb at 640×480×16bpp.
   Colors in this spec survive 16-bit quantisation (Bible §8.2 discipline).
3. **Progress is honest.** The fill maps to real stages published by runit
   stage scripts to `/run/castalia/boot-progress` (0–100 + label). No fake
   smooth bars.
4. **Esc always works** → verbose text boot (the splash is a veneer over,
   never a replacement for, readable boot messages — Bible P5).
5. **Text fallback:** on no-fb consoles the same stages print as plain lines:
   `castalia: iniciando servicios [##########------] 62%`.
6. Implementation target: a tiny C fbsplash client (Phase 2), *not* Plymouth
   (§6 — too heavy for the floor tier).

All artwork original (provenance ledger row required for every raster baked
from this source).
