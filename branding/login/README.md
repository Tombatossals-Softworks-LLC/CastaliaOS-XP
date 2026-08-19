# Castalia login screen (greeter) — specification

The greeter is the daily threshold of the product: it must feel **calm,
instant, and welcoming**, run in a few MB of RAM on the FLOOR tier, and be
fully operable by keyboard alone (Bible §6.6, §8.6, §7.11).

**Animated reference:** [`docs/login-preview.html`](../../docs/login-preview.html)
— interactive demo of every state below, including the error path and the
high-contrast mode.

**Identity plate:** [`banner.svg`](banner.svg) — the greeter's banner (dawn
roundel + wordmark), drawn to sit above the login card. Until the native
token-driven greeter lands, the shipped stock GTK greeter is themed by
[`services/lightdm/lightdm-gtk-greeter.conf`](../../services/lightdm/lightdm-gtk-greeter.conf)
(Human Dawn background, Castalia icons, es-ES clock).

## Composition

| Element | Spec |
|---|---|
| Background | The active theme's wallpaper, full-bleed — "Human Dawn" on the shipped default, "Azure Bay" otherwise; a very slow 12 s drift-zoom (1.0→1.04) that stops under `reduce-animations` |
| Banner | `banner.svg` centred above the card (max-width 480 px, hides below 700 px width); fades in with the card |
| Scrim | Bottom-weighted darkening (`rgba(6,16,26,.0→.45)`) so foreground text always clears WCAG on the photo |
| Top bar | Clock (HH:MM, tabular numerals) + full es-ES date, right-aligned; small mark top-left |
| Login card | Stone surface `rgba(236,233,228,.94)`, 1 px border `#B8B2A6`, radius 6, soft 24 px shadow; max-width 360 px; fits 800×600 |
| User tiles | Avatar circle (48 px) + name; keyboard-focusable; selected tile lifts 2 px with azure ring |
| Password row | Slides open under the selected user (220 ms ease-out); show/hide toggle; caps-lock warning |
| Buttons | "Entrar" primary (azure), guest logs in directly |
| Bottom bar | Left: accessibility (toggles High-Contrast greeter live). Centre: session chip ("Sesión: Castalia Classic"). Right: restart / shutdown with labels |

## States & choreography

| State | Behaviour |
|---|---|
| Load | Card rises 16 px + fades in (600 ms, cubic-bezier(.2,.7,.3,1)); wallpaper drift starts |
| Select user | Tile ring + lift; password row slides open; input autofocused |
| Wrong password | Card shakes ±8 px (350 ms, 3 oscillations); message "Contraseña incorrecta" in terracotta `#B3372E`; input clears and refocuses; **no lockout theatrics** |
| Success | Button → spinner (400 ms) → check; card content crossfades to "Bienvenido, {usuario}"; screen crossfades 700 ms to the desktop handoff ("Preparando tu escritorio…") |
| High contrast | One toggle re-skins the whole greeter from the `high-contrast` theme tokens: black card, white text, `#FFD800` accent, thick focus rings |
| Reduced motion | All entrances instant; shake replaced by the message alone |

## Engineering constraints

1. **LightDM greeter** (Bible §6.6) implemented against the same
   `theme.conf` tokens as the desktop — one theme system everywhere (§6.16).
2. **Budget:** ≤ 12 MB RSS, first paint < 1 s after X is up on FLOOR.
3. **Keyboard-first:** Tab cycles tiles → password → buttons → bottom bar;
   Enter activates; Esc returns to the user list. Every control has a visible
   focus state.
4. **No user enumeration surprises:** tiles list local users only; an
   "Otro usuario…" entry allows typed usernames; guest is optional and off by
   default (Bible §15).
5. **Fits 800×600** with no scroll and no clipped controls (§7.11).
6. Autologin, when enabled, bypasses the greeter entirely (with the Bible's
   security note at the moment of enabling).
