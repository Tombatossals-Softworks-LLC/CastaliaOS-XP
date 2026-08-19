#!/usr/bin/env python3
"""Castalia design-system preview generator.

Reads the REAL design tokens — ``themes/*/theme.conf`` bundles, the icon
family, the mark, and the "Azure Bay" wallpaper — and emits a self-contained
interactive HTML page: a clickable desktop mockup (Explorer window, Control
Center, taskbar, Castalia Menu) that re-themes live when you pick a theme,
plus widget gallery, token tables with WCAG contrast ratios computed by the
same math the linter enforces, an icon gallery, and the metric table.

This is the §6.16 promise ("one switch changes everything") demonstrated
before a single line of Qt exists, generated from the single source of truth.

Usage:
    PYTHONPATH=tools python3 tools/preview_gen.py                # -> docs/preview.html
    PYTHONPATH=tools python3 tools/preview_gen.py --out FILE
    PYTHONPATH=tools python3 tools/preview_gen.py --fragment FILE  # body-only
"""

from __future__ import annotations

import argparse
import base64
import re
import sys
import tomllib
import urllib.parse
from pathlib import Path

from castalia_qa import color

REPO = Path(__file__).resolve().parents[1]
THEME_ORDER = ["human", "classic", "azul", "oliva", "plata", "medianoche",
               "high-contrast"]
DEFAULT_THEME = "human"           # the flagship look leads the preview
DEFAULT_WALLPAPER = "branding/wallpapers/valle-de-castalia.jpg"

DESKTOP_ICONS = [
    ("computer", "Equipo"),
    ("documents", "Documentos"),
    ("network", "Lugares de red"),
    ("trash", "Papelera"),
]

ICON_LABELS = {
    "folder": "folder", "computer": "computer", "documents": "documents",
    "network": "network", "trash": "trash", "settings": "settings",
    "disk": "disk", "help": "help", "home": "home",
}


# --------------------------------------------------------------------------
# loading
# --------------------------------------------------------------------------

def load_themes(themes_dir: Path) -> list[dict]:
    themes = []
    for conf in sorted(themes_dir.glob("*/theme.conf")):
        data = tomllib.loads(conf.read_text(encoding="utf-8"))
        data["_id"] = data["meta"]["id"]
        themes.append(data)
    order = {tid: i for i, tid in enumerate(THEME_ORDER)}
    themes.sort(key=lambda t: order.get(t["_id"], 99))
    return themes


def load_svg(path: Path) -> str:
    """Load an SVG and strip fixed width/height so CSS can size it."""
    svg = path.read_text(encoding="utf-8")
    svg = re.sub(r"<!--.*?-->", "", svg, flags=re.S)
    svg = re.sub(r'(<svg[^>]*?)\s+width="\d+"\s+height="\d+"', r"\1", svg, count=1)
    return svg.strip()


def load_icons(icons_dir: Path) -> dict[str, str]:
    return {p.stem: load_svg(p) for p in sorted(icons_dir.glob("*.svg"))}


def wallpaper_data_uri(path: Path) -> str:
    """A data URI for a wallpaper — vector or photographic.

    The shipped default is a JPEG, so this can no longer assume the file is
    text: SVG is inlined (comments stripped, it is the bulk of the bytes),
    anything else is base64'd as-is.
    """
    if path.suffix.lower() == ".svg":
        raw = re.sub(r"<!--.*?-->", "", path.read_text(encoding="utf-8"),
                     flags=re.S)
        return "data:image/svg+xml," + urllib.parse.quote(raw, safe="")
    mime = {".jpg": "image/jpeg", ".jpeg": "image/jpeg",
            ".png": "image/png"}.get(path.suffix.lower(), "image/png")
    return (f"data:{mime};base64,"
            + base64.b64encode(path.read_bytes()).decode("ascii"))


# --------------------------------------------------------------------------
# per-theme CSS + token tables
# --------------------------------------------------------------------------

def theme_css_block(theme: dict) -> str:
    c, m = theme["colors"], theme["metrics"]
    tid = theme["_id"]
    return (
        f".th-{tid}{{"
        f"--accent:{c['accent']};--tb-top:{c['titlebar_top']};"
        f"--tb-bot:{c['titlebar_bottom']};--tb-text:{c['titlebar_text']};"
        f"--tbi-top:{c['titlebar_inactive_top']};"
        f"--tbi-bot:{c['titlebar_inactive_bottom']};"
        f"--tbi-text:{c['titlebar_inactive_text']};"
        f"--surface:{c['surface']};--surface-alt:{c['surface_alt']};"
        f"--text:{c['text']};--text2:{c['text_secondary']};"
        f"--sel-bg:{c['selection_bg']};--sel-text:{c['selection_text']};"
        f"--bord:{c['border']};"
        f"--tb-h:{m['titlebar_height']}px;--rad:{m['corner_radius']}px;"
        f"--panel-h:{m['panel_height']}px;}}"
    )


def contrast_rows(theme: dict) -> str:
    c = theme["colors"]
    hc = bool(theme["meta"].get("high_contrast", False))
    text_min = color.CONTRAST_AAA if hc else color.CONTRAST_AA

    mid_a = color.gradient_midpoint_luminance(c["titlebar_top"], c["titlebar_bottom"])
    checks = [
        ("text / surface", c["text"], c["surface"],
         color.contrast_ratio(c["text"], c["surface"]), text_min),
        ("secondary / surface", c["text_secondary"], c["surface"],
         color.contrast_ratio(c["text_secondary"], c["surface"]), 3.0),
        ("selection text / bg", c["selection_text"], c["selection_bg"],
         color.contrast_ratio(c["selection_text"], c["selection_bg"]), text_min),
        ("titlebar text / gradient mid", c["titlebar_text"], c["titlebar_top"],
         color.contrast_vs_luminance(c["titlebar_text"], mid_a), 4.5),
    ]
    delta = color.gradient_luminance_delta(c["titlebar_top"], c["titlebar_bottom"])

    rows = []
    for label, fg, bg, ratio, minimum in checks:
        ok = ratio >= minimum
        rows.append(
            f'<tr><td><span class="pair" style="background:{bg};color:{fg}">Ag</span> {label}</td>'
            f'<td class="num">{ratio:.2f}:1</td><td class="num">&ge; {minimum:g}:1</td>'
            f'<td><span class="chip {"ok" if ok else "bad"}">{"pass" if ok else "FAIL"}</span></td></tr>'
        )
    ok = delta <= color.GRADIENT_MAX_LUMINANCE_DELTA
    rows.append(
        '<tr><td><span class="pair grad" '
        f'style="background:linear-gradient(180deg,{c["titlebar_top"]},{c["titlebar_bottom"]})"></span> '
        "titlebar gradient &Delta;luminance (16-bit safety)</td>"
        f'<td class="num">{delta:.3f}</td><td class="num">&le; 0.12</td>'
        f'<td><span class="chip {"ok" if ok else "bad"}">{"pass" if ok else "FAIL"}</span></td></tr>'
    )
    return "\n".join(rows)


def swatch_rows(theme: dict) -> str:
    rows = []
    for key, value in theme["colors"].items():
        rows.append(
            f'<tr><td><span class="sw" style="background:{value}"></span></td>'
            f"<td><code>{key}</code></td><td><code>{value}</code></td></tr>"
        )
    return "\n".join(rows)


def token_section(theme: dict) -> str:
    tid = theme["_id"]
    m = theme["metrics"]
    name = theme["meta"]["name"]
    hc = " &middot; AAA enforced" if theme["meta"].get("high_contrast") else ""
    return f"""
<div class="tokset" data-theme-id="{tid}" hidden>
  <div class="tokgrid">
    <div class="tokcard">
      <h4>Palette — {name}</h4>
      <table class="toktable"><tbody>{swatch_rows(theme)}</tbody></table>
    </div>
    <div class="tokcard">
      <h4>Enforced checks{hc}</h4>
      <table class="toktable checks">
        <thead><tr><th>rule</th><th>value</th><th>minimum</th><th></th></tr></thead>
        <tbody>{contrast_rows(theme)}</tbody>
      </table>
      <h4 class="mt">Metrics</h4>
      <p class="metrics"><code>base&nbsp;{m['base_unit']}px</code>
      <code>titlebar&nbsp;{m['titlebar_height']}px</code>
      <code>radius&nbsp;{m['corner_radius']}px</code>
      <code>panel&nbsp;{m['panel_height']}px</code>
      <code>panel@800&nbsp;{m['panel_height_800']}px</code></p>
    </div>
  </div>
</div>"""


# --------------------------------------------------------------------------
# mockup fragments
# --------------------------------------------------------------------------

_UID = 0


def uniq(svg: str) -> str:
    """Rewrite an inlined SVG's ids per instance.

    Gradients defined inside a hidden subtree (e.g. the closed Castalia Menu)
    are ignored by browsers, which breaks every other url(#id) reference to
    the same id document-wide. Unique ids per embed make each instance
    self-sufficient.
    """
    global _UID
    _UID += 1
    n = _UID
    svg = re.sub(r'id="([^"]+)"', lambda m: f'id="{m.group(1)}_{n}"', svg)
    return re.sub(r"url\(#([^)]+)\)", lambda m: f"url(#{m.group(1)}_{n})", svg)


def icon(icons: dict[str, str], name: str, cls: str = "") -> str:
    return f'<span class="cxi {cls}" aria-hidden="true">{uniq(icons[name])}</span>'


def desktop_stage(icons: dict[str, str], mark: str) -> str:
    desk_icons = "\n".join(
        f'<div class="dico"><span class="cxi">{uniq(icons[i])}</span><span class="dlabel">{label}</span></div>'
        for i, label in DESKTOP_ICONS
    )
    sidebar = "\n".join(
        f'<li>{icon(icons, i, "i16")}{label}</li>'
        for i, label in [("home", "Carpeta personal"), ("documents", "Documentos"),
                         ("computer", "Equipo"), ("network", "Lugares de red"),
                         ("trash", "Papelera")]
    )
    files = "\n".join(
        f'<div class="fitem{" sel" if sel else ""}"><span class="cxi">{uniq(icons[i])}</span><span>{label}</span></div>'
        for i, label, sel in [
            ("folder", "Facturas", False), ("folder", "Fotos verano", True),
            ("folder", "Música", False), ("documents", "notas.txt", False),
            ("documents", "presupuesto.ods", False),
        ]
    )
    cc_tiles = "\n".join(
        f'<div class="cctile"><span class="cxi">{uniq(icons[i])}</span><span>{label}</span></div>'
        for i, label in [("computer", "Pantalla"), ("settings", "Sonido"),
                         ("network", "Red"), ("home", "Usuarios")]
    )
    menu_left = "\n".join(
        f'<li>{icon(icons, i, "i24")}<span>{label}</span></li>'
        for i, label in [("folder", "Castalia Explorer"), ("help", "Centro de ayuda"),
                         ("disk", "Centro de software"), ("settings", "Centro de control")]
    )
    menu_right = "\n".join(
        f'<li>{icon(icons, i, "i16")}<span>{label}</span></li>'
        for i, label in [("documents", "Documentos"), ("home", "Carpeta personal"),
                         ("computer", "Equipo"), ("network", "Lugares de red")]
    )

    return f"""
<div class="stagewrap">
<div class="stage th-{DEFAULT_THEME}" id="stage">

  <div class="deskicons">{desk_icons}</div>

  <!-- Control Center — inactive window (shows the inactive titlebar tokens) -->
  <div class="win win-cc inactive">
    <div class="tbar"><span class="cxi i16 wicon">{uniq(icons['settings'])}</span>
      <span class="ttext">Centro de control</span>
      <span class="wbtns"><button class="wb" tabindex="-1"><i class="g-min"></i></button><button class="wb" tabindex="-1"><i class="g-max"></i></button><button class="wb close" tabindex="-1"><i class="g-x"></i></button></span>
    </div>
    <div class="wbody ccbody">{cc_tiles}</div>
  </div>

  <!-- Castalia Explorer — active window -->
  <div class="win win-ex">
    <div class="tbar"><span class="cxi i16 wicon">{uniq(icons['folder'])}</span>
      <span class="ttext">Documentos — Castalia Explorer</span>
      <span class="wbtns"><button class="wb" aria-label="Minimizar"><i class="g-min"></i></button><button class="wb" aria-label="Maximizar"><i class="g-max"></i></button><button class="wb close" aria-label="Cerrar"><i class="g-x"></i></button></span>
    </div>
    <div class="menubar"><span>Archivo</span><span>Edición</span><span>Ver</span><span>Ayuda</span></div>
    <div class="toolbar">
      <button class="tbtn">&larr; Atrás</button><button class="tbtn">&rarr;</button><button class="tbtn">&uarr;</button>
      <span class="addr"><code>/home/dave/Documentos</code></span>
    </div>
    <div class="wbody exbody">
      <ul class="places">{sidebar}</ul>
      <div class="fgrid">{files}</div>
    </div>
    <div class="statusbar">5 elementos &middot; 2,3&nbsp;MB libres de 18,6&nbsp;GB</div>
  </div>

  <!-- Castalia Menu -->
  <div class="cmenu" id="cmenu" hidden>
    <div class="cmhead"><span class="avatar" aria-hidden="true"></span><strong>Dave</strong></div>
    <div class="cmcols">
      <ul class="cmleft">{menu_left}
        <li class="allapps"><span>Todas las aplicaciones</span><span aria-hidden="true">&#9656;</span></li>
      </ul>
      <ul class="cmright">{menu_right}</ul>
    </div>
    <div class="cmfoot"><button class="pw">Bloquear</button><button class="pw">Cerrar sesión</button><button class="pw solid">Apagar</button></div>
  </div>

  <!-- Taskbar -->
  <div class="taskbar">
    <button class="launch" id="launch" aria-expanded="false" aria-controls="cmenu">
      <span class="cxi i20">{uniq(mark)}</span><span class="ltext">Castalia</span>
    </button>
    <span class="tsep"></span>
    <span class="ql">{icon(icons, 'folder', 'i16')}{icon(icons, 'help', 'i16')}</span>
    <span class="tsep"></span>
    <button class="task active">{icon(icons, 'folder', 'i16')}<span>Documentos — Castalia…</span></button>
    <button class="task">{icon(icons, 'settings', 'i16')}<span>Centro de control</span></button>
    <span class="tray">{icon(icons, 'network', 'i14')}{icon(icons, 'disk', 'i14')}<span class="clock">10:24</span></span>
  </div>
</div>
</div>
<p class="stagecap">Interactive: pick a theme above — the entire desktop re-skins from that theme's
<code>theme.conf</code> (the §6.16 &ldquo;one switch changes everything&rdquo; promise). Click
<strong>Castalia</strong> to open the menu. Mockup strings are es-ES; every gradient, radius and
height is read from the shipped tokens, not hand-styled.</p>"""


def widget_gallery(icons: dict[str, str]) -> str:
    return f"""
<div class="wgrid">
  <div class="wcard"><h4>Buttons</h4>
    <div class="row"><button class="cbtn primary">Aceptar</button>
    <button class="cbtn">Cancelar</button>
    <button class="cbtn" disabled>Aplicar</button></div>
  </div>
  <div class="wcard"><h4>Inputs</h4>
    <div class="row"><span class="cinput">Peñíscola<span class="csel">2026</span></span></div>
    <div class="row opts">
      <label class="copt"><span class="cbox checked" aria-hidden="true"></span>Reducir animaciones</label>
      <label class="copt"><span class="cbox" aria-hidden="true"></span>Compositor</label>
      <label class="copt"><span class="cradio checked" aria-hidden="true"></span>30&nbsp;px</label>
      <label class="copt"><span class="cradio" aria-hidden="true"></span>28&nbsp;px</label>
    </div>
  </div>
  <div class="wcard"><h4>Progress &amp; slider</h4>
    <div class="cprog"><span style="width:62%"></span></div>
    <div class="cslider"><span class="ctrack"></span><span class="cthumb"></span></div>
  </div>
  <div class="wcard"><h4>Tabs &amp; list</h4>
    <div class="ctabs"><span class="ctab active">Tema</span><span class="ctab">Iconos</span><span class="ctab">Sonidos</span></div>
    <ul class="clist"><li>{icon(icons, 'folder', 'i16')}Castalia Classic</li>
    <li class="sel">{icon(icons, 'folder', 'i16')}Castalia Azul</li>
    <li>{icon(icons, 'folder', 'i16')}Castalia Oliva</li></ul>
  </div>
</div>"""


def icon_gallery(icons: dict[str, str], mark: str) -> str:
    cells = []
    for name in sorted(icons):
        cells.append(
            f'<div class="icell"><div class="irow">'
            f'<span class="cxi i48">{uniq(icons[name])}</span>'
            f'<span class="cxi i32">{uniq(icons[name])}</span>'
            f'<span class="cxi i16">{uniq(icons[name])}</span></div>'
            f"<code>{name}</code></div>"
        )
    cells.append(
        f'<div class="icell"><div class="irow"><span class="cxi i48">{uniq(mark)}</span>'
        f'<span class="cxi i32">{uniq(mark)}</span><span class="cxi i16">{uniq(mark)}</span></div>'
        f"<code>castalia-mark</code></div>"
    )
    return '<div class="igrid">' + "\n".join(cells) + "</div>"


# --------------------------------------------------------------------------
# page assembly
# --------------------------------------------------------------------------

def build_page(repo: Path) -> tuple[str, str]:
    """Return (fragment_html, full_html)."""
    themes = load_themes(repo / "themes")
    icons = load_icons(repo / "themes" / "icons" / "48")
    mark = load_svg(repo / "branding" / "logo" / "castalia-mark.svg")

    theme_css = "\n".join(theme_css_block(t) for t in themes)
    # Per-theme wallpaper (the optional [assets].wallpaper token): the stage
    # swaps art with the theme, exactly like the real desktop plane does.
    # Themes are grouped by the wallpaper they use, so each image is embedded
    # once however many themes share it. The default is a 470 KiB JPEG and
    # three themes fall back to it — one rule per theme would have put two
    # extra megabytes of base64 in a file that is meant to be openable.
    wall_users: dict[str, list[str]] = {}
    for t in themes:
        rel = t.get("assets", {}).get("wallpaper", DEFAULT_WALLPAPER)
        if not (repo / rel).exists():
            rel = DEFAULT_WALLPAPER
        wall_users.setdefault(rel, []).append(t["_id"])
    wall_css = []
    for rel, ids in wall_users.items():
        selector = ",".join(f".stage.th-{tid}" for tid in ids)
        wall_css.append(
            f"{selector}{{background-image:url('"
            f"{wallpaper_data_uri(repo / rel)}')}}")
    theme_css += "\n" + "\n".join(wall_css)
    chips = "\n".join(
        f'<button class="thchip{" on" if t["_id"] == DEFAULT_THEME else ""}" '
        f'data-theme-id="{t["_id"]}" aria-pressed="{"true" if t["_id"] == DEFAULT_THEME else "false"}">'
        f'<span class="dot" style="background:linear-gradient(180deg,'
        f'{t["colors"]["titlebar_top"]},{t["colors"]["titlebar_bottom"]})"></span>'
        f'{t["meta"]["name"].replace("Castalia ", "")}</button>'
        for t in themes
    )
    toksets = "\n".join(token_section(t) for t in themes)
    # the default theme's tokens are visible initially
    toksets = toksets.replace(f'data-theme-id="{DEFAULT_THEME}" hidden',
                              f'data-theme-id="{DEFAULT_THEME}"', 1)

    body = f"""<title>Castalia OS — Design System Preview</title>
<style>{PAGE_CSS}
/* --- theme token blocks generated from themes/*.conf --- */
{theme_css}
</style>
<div class="page">
<header class="mast">
  <span class="cxi i40">{uniq(mark)}</span>
  <div>
    <p class="eyebrow">Tombatossals Softworks &middot; design system preview v0.1</p>
    <h1>Castalia OS <span class="thin">— XP-class, entirely original</span></h1>
    <p class="lede">Every color, gradient, radius and height on this page is generated from the
    repository&rsquo;s shipped <code>theme.conf</code> tokens by <code>tools/preview_gen.py</code>.
    The linter&rsquo;s WCAG math runs at build time — the contrast tables below are computed, not copied.</p>
  </div>
</header>

<section>
  <div class="sechead"><h2>The desktop, live</h2><div class="chips" role="group" aria-label="Theme">{chips}</div></div>
  {desktop_stage(icons, mark)}
</section>

<section>
  <h2>Widget language</h2>
  <p class="note">Same tokens, applied to the control set the Qt <code>libcastalia-ui</code> library will implement (§8.3): 2&nbsp;px radius, 1&nbsp;px borders, era-honest depth without compositor effects.</p>
  <div class="stage-widgets th-{DEFAULT_THEME}" id="widgetstage">{widget_gallery(icons)}</div>
</section>

<section>
  <h2>Tokens &amp; enforced checks</h2>
  <p class="note">These are the rules CI fails on (<code>tools/castalia_qa/theme_lint.py</code>): WCAG contrast minima and the 16-bit-era gradient safety rule (&Delta;luminance &le; 0.12).</p>
  {toksets}
</section>

<section>
  <h2>Icon family — 48 / 32 / 16&nbsp;px</h2>
  <p class="note">One grid, top-left light, tangerine warmth: sun-orange + chocolate + aluminium gradients with tango outlines (§8.4). Drawn as pure geometry — no fonts, no traced artwork. Provenance-tracked in <code>legal/ASSET_PROVENANCE.csv</code>.</p>
  {icon_gallery(icons, mark)}
</section>

<footer class="foot">
  <p>Generated by <code>tools/preview_gen.py</code> from <code>themes/*/theme.conf</code>,
  <code>themes/icons/48/</code> and <code>branding/</code>. All artwork original,
  Tombatossals Softworks. Windows&reg; is a trademark of Microsoft Corporation;
  Castalia OS is unaffiliated — and shares none of its assets.</p>
</footer>
</div>
<script>{PAGE_JS}</script>
"""
    full = (
        "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
        "</head>\n<body>\n" + body + "\n</body>\n</html>\n"
    )
    return body, full


# --------------------------------------------------------------------------
# static CSS / JS
# --------------------------------------------------------------------------

PAGE_CSS = """
:root{
  --pg-bg:#F2F0EB; --pg-ink:#24211C; --pg-ink2:#6B6559; --pg-card:#FFFFFF;
  --pg-line:#DCD6C9; --pg-accent:#2C6699; --pg-accent-soft:#E3EDF5;
  --ok-bg:#E4F2E9; --ok-ink:#22603C; --bad-bg:#F9E4E1; --bad-ink:#8F2A22;
}
@media (prefers-color-scheme: dark){:root{
  --pg-bg:#14181D; --pg-ink:#E8E4DC; --pg-ink2:#98A1AB; --pg-card:#1C2229;
  --pg-line:#2C343D; --pg-accent:#6FA7D1; --pg-accent-soft:#22303C;
  --ok-bg:#1D3A2A; --ok-ink:#7FCBA0; --bad-bg:#42211E; --bad-ink:#EE9C93;
}}
:root[data-theme="dark"]{
  --pg-bg:#14181D; --pg-ink:#E8E4DC; --pg-ink2:#98A1AB; --pg-card:#1C2229;
  --pg-line:#2C343D; --pg-accent:#6FA7D1; --pg-accent-soft:#22303C;
  --ok-bg:#1D3A2A; --ok-ink:#7FCBA0; --bad-bg:#42211E; --bad-ink:#EE9C93;
}
:root[data-theme="light"]{
  --pg-bg:#F2F0EB; --pg-ink:#24211C; --pg-ink2:#6B6559; --pg-card:#FFFFFF;
  --pg-line:#DCD6C9; --pg-accent:#2C6699; --pg-accent-soft:#E3EDF5;
  --ok-bg:#E4F2E9; --ok-ink:#22603C; --bad-bg:#F9E4E1; --bad-ink:#8F2A22;
}
*{box-sizing:border-box}
body{background:var(--pg-bg);color:var(--pg-ink);
  font-family:Verdana,'DejaVu Sans',Geneva,Tahoma,sans-serif;
  font-size:14px;line-height:1.55;margin:0}
.page{max-width:1060px;margin:0 auto;padding:28px 20px 48px;
  display:flex;flex-direction:column;gap:40px}
code{font-family:'Cascadia Mono','DejaVu Sans Mono',Consolas,monospace;
  font-size:.92em}
h1{font-size:26px;margin:2px 0 10px;letter-spacing:-.01em;text-wrap:balance}
h1 .thin{font-weight:400;color:var(--pg-ink2)}
h2{font-size:19px;margin:0 0 6px}
h4{font-size:13px;margin:0 0 8px;text-transform:uppercase;
  letter-spacing:.06em;color:var(--pg-ink2)}
.eyebrow{margin:0;font-size:11px;text-transform:uppercase;
  letter-spacing:.14em;color:var(--pg-accent)}
.lede{margin:0;max-width:64ch;color:var(--pg-ink2)}
.note{margin:0 0 14px;max-width:70ch;color:var(--pg-ink2);font-size:13px}
.mast{display:flex;gap:18px;align-items:flex-start;
  border-bottom:1px solid var(--pg-line);padding-bottom:22px}
.foot{border-top:1px solid var(--pg-line);padding-top:16px;
  font-size:12px;color:var(--pg-ink2)}
.foot p{max-width:80ch;margin:0}
.sechead{display:flex;justify-content:space-between;align-items:center;
  gap:16px;flex-wrap:wrap;margin-bottom:10px}
.sechead h2{margin:0}

/* icon embed helper */
.cxi{display:inline-flex;width:48px;height:48px;flex:none}
.cxi svg{width:100%;height:100%}
.i40{width:40px;height:40px}.i32{width:32px;height:32px}
.i24{width:24px;height:24px}.i20{width:20px;height:20px}
.i16{width:16px;height:16px}.i14{width:14px;height:14px}

/* theme chips */
.chips{display:flex;gap:8px;flex-wrap:wrap}
.thchip{display:inline-flex;align-items:center;gap:8px;
  font:inherit;font-size:12.5px;color:var(--pg-ink);
  background:var(--pg-card);border:1px solid var(--pg-line);
  border-radius:999px;padding:6px 14px 6px 8px;cursor:pointer}
.thchip .dot{width:16px;height:16px;border-radius:50%;
  border:1px solid rgba(0,0,0,.25)}
.thchip.on{border-color:var(--pg-accent);
  box-shadow:0 0 0 1px var(--pg-accent);background:var(--pg-accent-soft)}
.thchip:focus-visible,.launch:focus-visible,.wb:focus-visible,
.task:focus-visible,.pw:focus-visible{outline:2px solid var(--pg-accent);outline-offset:2px}

/* ============ the desktop stage — all colors from theme tokens ========= */
.stagewrap{overflow-x:auto;border-radius:10px}
.stage{position:relative;min-width:820px;aspect-ratio:4/3;max-height:640px;
  background-size:cover;background-position:center;
  border:1px solid var(--pg-line);border-radius:10px;overflow:hidden;
  font-size:12px;color:var(--text)}
.deskicons{position:absolute;top:14px;left:14px;display:flex;
  flex-direction:column;gap:16px}
.dico{display:flex;flex-direction:column;align-items:center;gap:4px;width:86px}
.dlabel{color:#FFFFFF;text-shadow:0 1px 2px rgba(0,0,0,.75);
  font-size:11px;text-align:center}

.win{position:absolute;display:flex;flex-direction:column;
  background:var(--surface);border:1px solid var(--bord);
  border-radius:calc(var(--rad) + 2px);
  box-shadow:0 6px 22px rgba(10,25,40,.35)}
.win-ex{left:26%;top:9%;width:60%;height:66%;z-index:3}
.win-cc{left:13%;top:30%;width:38%;height:48%;z-index:2}
.tbar{display:flex;align-items:center;gap:6px;height:var(--tb-h);
  padding:0 6px 0 8px;color:var(--tb-text);
  background:linear-gradient(180deg,var(--tb-top),var(--tb-bot));
  border-radius:var(--rad) var(--rad) 0 0}
.inactive .tbar{background:linear-gradient(180deg,var(--tbi-top),var(--tbi-bot));
  color:var(--tbi-text)}
.ttext{flex:1;font-size:12px;font-weight:700;white-space:nowrap;
  overflow:hidden;text-overflow:ellipsis}
.wbtns{display:flex;gap:3px}
.wb{width:20px;height:20px;border-radius:var(--rad);cursor:pointer;
  background:var(--surface-alt);border:1px solid var(--bord);
  display:inline-flex;align-items:center;justify-content:center;padding:0}
.wb.close{background:var(--accent);border-color:var(--tb-bot)}
.wb i{display:block}
.g-min{width:10px;height:2px;background:var(--text);margin-top:8px}
.g-max{width:8px;height:8px;border:2px solid var(--text)}
.g-x{position:relative;width:10px;height:10px}
.g-x::before,.g-x::after{content:"";position:absolute;left:4px;top:-1px;
  width:2px;height:12px;background:var(--sel-text)}
.g-x::before{transform:rotate(45deg)}.g-x::after{transform:rotate(-45deg)}

.menubar{display:flex;gap:14px;padding:4px 10px;font-size:11.5px;
  background:var(--surface);border-bottom:1px solid var(--bord)}
.toolbar{display:flex;gap:6px;align-items:center;padding:5px 8px;
  background:var(--surface-alt);border-bottom:1px solid var(--bord)}
.tbtn{font:inherit;font-size:11.5px;color:var(--text);cursor:pointer;
  background:var(--surface);border:1px solid var(--bord);
  border-radius:var(--rad);padding:3px 9px}
.addr{flex:1;background:var(--surface);border:1px solid var(--bord);
  border-radius:var(--rad);padding:3px 8px;font-size:11px;color:var(--text2)}
.wbody{flex:1;display:flex;min-height:0;background:var(--surface)}
.exbody .places{list-style:none;margin:0;padding:8px 0;width:158px;
  background:var(--surface-alt);border-right:1px solid var(--bord);
  display:flex;flex-direction:column;gap:2px}
.places li{display:flex;gap:8px;align-items:center;padding:4px 12px;
  font-size:11.5px;color:var(--text)}
.fgrid{flex:1;display:grid;grid-template-columns:repeat(auto-fill,88px);
  gap:10px;align-content:start;padding:14px}
.fitem{display:flex;flex-direction:column;align-items:center;gap:4px;
  padding:6px 2px;border-radius:var(--rad);font-size:11px;
  color:var(--text);text-align:center}
.fitem.sel{background:var(--sel-bg);color:var(--sel-text)}
.statusbar{padding:3px 10px;font-size:10.5px;color:var(--text2);
  background:var(--surface-alt);border-top:1px solid var(--bord);
  border-radius:0 0 var(--rad) var(--rad)}
.ccbody{display:grid;grid-template-columns:1fr 1fr;gap:10px;padding:14px;
  align-content:start}
.cctile{display:flex;align-items:center;gap:10px;padding:8px 10px;
  background:var(--surface-alt);border:1px solid var(--bord);
  border-radius:var(--rad);font-size:11.5px;color:var(--text)}
.cctile .cxi{width:28px;height:28px}

.taskbar{position:absolute;left:0;right:0;bottom:0;height:var(--panel-h);
  display:flex;align-items:stretch;gap:6px;padding:2px 6px;
  background:linear-gradient(180deg,var(--tb-top),var(--tb-bot));
  border-top:1px solid var(--bord);z-index:6}
.launch{display:inline-flex;align-items:center;gap:6px;cursor:pointer;
  font:inherit;font-weight:700;font-size:12.5px;color:var(--sel-text);
  background:var(--accent);
  border:1px solid var(--tb-bot);border-radius:var(--rad);padding:0 12px 0 8px}
.tsep{width:1px;margin:4px 0;background:var(--tb-text);opacity:.35}
.ql{display:inline-flex;align-items:center;gap:6px;padding:0 4px}
.task{display:inline-flex;align-items:center;gap:6px;max-width:190px;
  font:inherit;font-size:11.5px;color:var(--tb-text);cursor:pointer;
  background:rgba(255,255,255,.14);border:1px solid rgba(255,255,255,.25);
  border-radius:var(--rad);padding:0 10px;margin:2px 0}
.task span{white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.task.active{background:rgba(255,255,255,.30)}
.tray{margin-left:auto;display:inline-flex;align-items:center;gap:8px;
  padding:0 10px;background:rgba(0,0,0,.14);border-radius:var(--rad)}
.clock{color:var(--tb-text);font-size:11.5px;
  font-variant-numeric:tabular-nums}

.cmenu{position:absolute;left:6px;bottom:calc(var(--panel-h) + 4px);
  width:380px;z-index:7;background:var(--surface);
  border:1px solid var(--bord);border-radius:var(--rad);
  box-shadow:0 10px 30px rgba(10,25,40,.45);overflow:hidden}
.cmhead{display:flex;align-items:center;gap:10px;padding:10px 14px;
  color:var(--tb-text);
  background:linear-gradient(180deg,var(--tb-top),var(--tb-bot))}
.avatar{width:28px;height:28px;border-radius:50%;
  background:radial-gradient(circle at 35% 30%,#E6D7B4,#B8965E);
  border:2px solid rgba(255,255,255,.7)}
.cmcols{display:grid;grid-template-columns:1.35fr 1fr}
.cmleft,.cmright{list-style:none;margin:0;padding:8px 0}
.cmleft{background:var(--surface)}
.cmright{background:var(--surface-alt);border-left:1px solid var(--bord)}
.cmleft li,.cmright li{display:flex;align-items:center;gap:10px;
  padding:7px 14px;font-size:12px;color:var(--text)}
.cmleft li.allapps{justify-content:space-between;margin-top:6px;
  border-top:1px solid var(--bord);font-weight:700}
.cmfoot{display:flex;justify-content:flex-end;gap:8px;padding:8px 12px;
  background:var(--surface-alt);border-top:1px solid var(--bord)}
.pw{font:inherit;font-size:11.5px;color:var(--text);cursor:pointer;
  background:var(--surface);border:1px solid var(--bord);
  border-radius:var(--rad);padding:4px 12px}
.pw.solid{background:var(--accent);border-color:var(--tb-bot);
  color:var(--sel-text);font-weight:700}
.stagecap{font-size:12.5px;color:var(--pg-ink2);max-width:78ch;margin:10px 2px 0}

/* ============ widget gallery (also token-driven) ============ */
.stage-widgets{background:var(--surface);border:1px solid var(--pg-line);
  border-radius:10px;padding:18px;color:var(--text)}
.wgrid{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:16px}
.wcard{background:var(--surface-alt);border:1px solid var(--bord);
  border-radius:calc(var(--rad) + 2px);padding:12px 14px}
.wcard h4{color:var(--text2)}
.row{display:flex;gap:8px;align-items:center;flex-wrap:wrap}
.row.opts{flex-direction:column;align-items:flex-start;gap:6px;margin-top:10px}
.cbtn{font:inherit;font-size:12px;color:var(--text);cursor:pointer;
  background:linear-gradient(180deg,var(--surface),var(--surface-alt));
  border:1px solid var(--bord);border-radius:var(--rad);padding:5px 16px}
.cbtn.primary{border-color:var(--accent);
  box-shadow:0 0 0 1px var(--accent) inset;font-weight:700}
.cbtn[disabled]{color:var(--text2);opacity:.55;cursor:default}
.cinput{display:inline-flex;background:#FFFFFF;color:#1E1E1E;
  border:1px solid var(--bord);border-radius:var(--rad);
  padding:4px 8px;font-size:12px;min-width:190px}
.th-high-contrast .cinput{background:#101010;color:#FFFFFF}
.csel{background:var(--sel-bg);color:var(--sel-text);margin-left:6px;
  padding:0 3px}
.copt{display:inline-flex;gap:8px;align-items:center;font-size:12px}
.cbox{width:14px;height:14px;border:1px solid var(--bord);
  border-radius:2px;background:var(--surface);position:relative}
.cbox.checked{background:var(--accent);border-color:var(--accent)}
.cbox.checked::after{content:"";position:absolute;left:4px;top:1px;
  width:4px;height:8px;border:solid var(--sel-text);
  border-width:0 2px 2px 0;transform:rotate(45deg)}
.cradio{width:14px;height:14px;border:1px solid var(--bord);
  border-radius:50%;background:var(--surface);position:relative}
.cradio.checked::after{content:"";position:absolute;inset:3px;
  border-radius:50%;background:var(--accent)}
.cprog{height:14px;background:var(--surface);border:1px solid var(--bord);
  border-radius:var(--rad);overflow:hidden;margin-bottom:14px}
.cprog span{display:block;height:100%;
  background:linear-gradient(180deg,var(--accent),var(--sel-bg))}
.cslider{position:relative;height:20px}
.ctrack{position:absolute;left:0;right:0;top:8px;height:4px;
  background:var(--surface);border:1px solid var(--bord);border-radius:2px}
.cthumb{position:absolute;left:58%;top:2px;width:10px;height:16px;
  background:linear-gradient(180deg,var(--surface),var(--surface-alt));
  border:1px solid var(--accent);border-radius:var(--rad)}
.ctabs{display:flex;gap:2px;margin-bottom:0}
.ctab{font-size:11.5px;padding:4px 12px;color:var(--text2);
  background:var(--surface);border:1px solid var(--bord);
  border-bottom:none;border-radius:var(--rad) var(--rad) 0 0}
.ctab.active{color:var(--text);font-weight:700;position:relative;top:1px;
  border-top:2px solid var(--accent)}
.clist{list-style:none;margin:0;padding:4px 0;background:var(--surface);
  border:1px solid var(--bord);border-radius:0 var(--rad) var(--rad) var(--rad)}
.clist li{display:flex;gap:8px;align-items:center;padding:4px 10px;
  font-size:12px;color:var(--text)}
.clist li.sel{background:var(--sel-bg);color:var(--sel-text)}

/* ============ token tables ============ */
.tokgrid{display:grid;grid-template-columns:repeat(auto-fit,minmax(320px,1fr));gap:16px}
.tokcard{background:var(--pg-card);border:1px solid var(--pg-line);
  border-radius:10px;padding:14px 16px;overflow-x:auto}
.toktable{border-collapse:collapse;width:100%;font-size:12px}
.toktable td,.toktable th{padding:4px 8px;text-align:left;
  border-bottom:1px solid var(--pg-line)}
.toktable tr:last-child td{border-bottom:none}
.toktable th{font-size:10.5px;text-transform:uppercase;
  letter-spacing:.06em;color:var(--pg-ink2)}
.toktable .num{font-variant-numeric:tabular-nums;white-space:nowrap}
.sw{display:inline-block;width:26px;height:16px;border-radius:3px;
  border:1px solid rgba(0,0,0,.25);vertical-align:middle}
.pair{display:inline-flex;align-items:center;justify-content:center;
  width:30px;height:20px;border-radius:3px;font-size:11px;font-weight:700;
  border:1px solid rgba(0,0,0,.2);margin-right:6px;vertical-align:middle}
.pair.grad{width:30px}
.chip{font-size:10.5px;font-weight:700;text-transform:uppercase;
  letter-spacing:.04em;border-radius:999px;padding:2px 9px}
.chip.ok{background:var(--ok-bg);color:var(--ok-ink)}
.chip.bad{background:var(--bad-bg);color:var(--bad-ink)}
.metrics{display:flex;gap:8px;flex-wrap:wrap;margin:6px 0 0}
.metrics code{background:var(--pg-accent-soft);border-radius:4px;
  padding:2px 8px;font-size:11.5px}
.mt{margin-top:16px}

/* ============ icon gallery ============ */
.igrid{display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:14px}
.icell{display:flex;flex-direction:column;gap:10px;
  background:var(--pg-card);border:1px solid var(--pg-line);
  border-radius:10px;padding:14px}
.icell .irow{display:flex;align-items:flex-end;gap:12px}
.icell code{font-size:10.5px;color:var(--pg-ink2)}

@media (max-width:720px){
  .mast{flex-direction:column}
  .cmenu{width:320px}
}
@media (prefers-reduced-motion:no-preference){
  .stage,.stage-widgets{transition:background-color .18s ease}
  .tbar,.taskbar,.cmhead{transition:background .18s ease}
}
"""

PAGE_JS = """
(function(){
  var stage=document.getElementById('stage');
  var widgets=document.getElementById('widgetstage');
  var chips=document.querySelectorAll('.thchip');
  var toksets=document.querySelectorAll('.tokset');
  chips.forEach(function(chip){
    chip.addEventListener('click',function(){
      var id=chip.getAttribute('data-theme-id');
      [stage,widgets].forEach(function(el){
        el.className=el.className.replace(/th-[a-z-]+/,'th-'+id);
      });
      chips.forEach(function(c){
        var on=c===chip;
        c.classList.toggle('on',on);
        c.setAttribute('aria-pressed',on?'true':'false');
      });
      toksets.forEach(function(t){
        t.hidden=t.getAttribute('data-theme-id')!==id;
      });
    });
  });
  var launch=document.getElementById('launch');
  var menu=document.getElementById('cmenu');
  launch.addEventListener('click',function(){
    var open=menu.hidden;
    menu.hidden=!open;
    launch.setAttribute('aria-expanded',open?'true':'false');
  });
})();
"""


# --------------------------------------------------------------------------

def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=REPO / "docs" / "preview.html",
                        help="full standalone HTML output path")
    parser.add_argument("--fragment", type=Path, default=None,
                        help="also write a body-only fragment here")
    args = parser.parse_args(argv[1:])

    fragment, full = build_page(REPO)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(full, encoding="utf-8")
    print(f"preview-gen: wrote {args.out} ({len(full)//1024} KiB)")
    if args.fragment:
        args.fragment.parent.mkdir(parents=True, exist_ok=True)
        args.fragment.write_text(fragment, encoding="utf-8")
        print(f"preview-gen: wrote fragment {args.fragment}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
