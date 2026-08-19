# Screenshots — captions & credits

All images © Tombatossals Softworks, cleared for editorial use when writing
about Castalia OS. Filenames are ordered for convenience. Real captures from
the software (offscreen renders and live QEMU/X sessions) — not mock-ups.

| File | Caption |
|---|---|
| `01-desktop-human.png` | The Castalia OS "Human" desktop: the hand-drawn "Human Dawn" wallpaper, the tangerine icon set, and the taskbar with one-click quick launchers. |
| `02-start-menu-human.png` | The categorised Start Menu — Accessories, Games, System, Compatibility — every entry with its own icon; places and power controls at the sides. |
| `03-control-center-themes.png` | The Control Center's Appearance page: all seven themes (Human, Classic, Azul, Oliva, Plata, Medianoche, High Contrast) from one token file — the preview is instant and the choice re-skins the whole desktop. |
| `04-software-center.png` | The Software Center — add and remove software over dpkg/apt, in the Human theme. |
| `05-installer.png` | The guided graphical installer. It shows the exact plan first and refuses every destructive step until you type the target disk to confirm; a text-mode installer is the guaranteed fallback. |
| `06-escritor-richtext.png` | "Escritor," the WordPad-class rich-text editor (exports HTML/ODT) — one of 42 first-party applications. |
| `07-diagnostics.png` | "Diagnóstico" — the hardware diagnostics tool with a real CPU/RAM/disk/graphics/network benchmark suite. |
| `08-buscaminas.png` | "Buscaminas," the Minesweeper-class game — clean-room, original art, with an automated rules self-test. |
| `09-solitario.png` | "Solitario" — original-art solitaire, likewise self-tested. |
| `10-terminal.png` | The terminal, with its own VT100 emulator. |
| `11-medianoche-dark-live.png` | **Medianoche**, the full-desktop dark mode — composed here by a real window manager (Openbox with the Castalia decorations) in a live session, not a mock-up. |
| `12-windows-compat-wine.png` | The Windows-application manager running a real Win32 program under Wine, in a per-app sandbox — captured live. |
| `13-calendario.png` | "Calendario" — a month view with a per-day notes pane (notes autosave and mark the day); it opens from the panel clock, the XP-era ergonomic. |
| `14-teclado-osk.png` | "Teclado en pantalla" — the on-screen keyboard (accessibility): a pointer-operable Spanish keyboard that types into your real window via XTEST and never steals focus. |
| `15-multimedia.png` | "Reproductor multimedia" — a playlist that hands playback to the best media player installed (mpv, VLC or MPlayer); native-first, honest about the backend it found. |
| `16-volumen.png` | "Control de volumen" — a small mixer over PulseAudio/PipeWire or ALSA; it opens from the panel tray speaker and is honest when no sound stack is present. |
| `17-buscar.png` | "Buscar archivos" — the file search: type part of a name, pick a folder, and it walks the tree in a background thread (streaming results, cancellable). |
| `18-wallpaper-picker.png` | The Control Center's "Fondo de escritorio" — pick any of the six original wallpapers (or "follow the theme") and the desktop applies it live. |
| `19-action-start-menu.png` | In use: the Start menu open over the Human desktop — every entry with its tangerine icon. |
| `20-action-paint.png` | In use: painting a brush stroke in Pintura, live. |
| `21-action-buscaminas.png` | In use: a game of Buscaminas (Minesweeper) mid-play, revealed numbers and all. |

## The demo screencast

`../video/castalia-os-demo.mp4` (web) and `../video/castalia-os-demo.gif`
(social) — a ~63-second recording of a **real** Castalia session: opening the
Start menu and **typing to search it**, painting, playing Buscaminas,
**Alt+Tab through the open windows**, opening the Calendario from the panel
clock and writing a note, a **notification toast from the real server**, the
**Centro de redes** listing Wi-Fi in plain language, and the wallpaper
switching live from the Control Center. Not a mock-up; the same real Openbox
+ shell session the CI captures, driven by xdotool. Reproduce it with
`sh tools/make-screencast.sh --gif`.

## Also useful for layouts

- `../logos/castalia-logo.png` — the Castalia mark, the artwork itself
  (256×280, transparent). Prefer this one.
- `../logos/castalia-mark-1024.png` — the vector edition, for large formats.
- `../logos/castalia-mark-1024-on-chocolate.png` — the mark on the Human
  chocolate background (for dark layouts).
- `../logos/castalia-wordmark-banner.png` — the wordmark banner (transparent).
- `../icons/icon-family-sheet.png` — the full 37-icon tangerine family.
- `../cursors/cursor-family-sheet.png` — the nine original mouse pointers,
  shipped as a real multi-size Xcursor theme (not a mock-up).
- `../wallpapers/wallpaper-human-dawn-2048.png` — the "Human Dawn" wallpaper
  at 2048 px (SVG source alongside).
