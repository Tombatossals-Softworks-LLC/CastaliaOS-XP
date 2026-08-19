#!/usr/bin/env python3
"""Castalia theme exporter — theme.conf → Qt QSS + Openbox themerc.

The §6.16 promise ("one switch changes everything") made mechanical: every
consumer of the theme system derives from the same ``themes/<id>/theme.conf``
bundle. This tool generates the two stylesheets the v1 desktop stack consumes:

* ``castalia.qss``       — Qt 5 stylesheet for the shell and all first-party
                           apps (libcastalia-ui loads it at startup).
* ``openbox-3/themerc``  — Openbox window decorations: titlebar gradients,
                           buttons (close = accent-tinted, matching §8.3),
                           menus and borders.

Generated files are build artifacts (default output ``build/out/themes/``,
gitignored) — they are never hand-edited; the tokens are the source of truth.

Usage:
    PYTHONPATH=tools python3 tools/theme_export.py                 # all themes
    PYTHONPATH=tools python3 tools/theme_export.py --theme classic
    PYTHONPATH=tools python3 tools/theme_export.py --out DIR
"""

from __future__ import annotations

import argparse
import sys
import tomllib
from pathlib import Path

from castalia_qa import color

REPO = Path(__file__).resolve().parents[1]


def mix(hex_a: str, hex_b: str, t: float) -> str:
    """Linear blend of two #RRGGBB colors, t=0 → a, t=1 → b."""
    a, b = color.parse_hex(hex_a), color.parse_hex(hex_b)
    return "#" + "".join(
        f"{round(ca + (cb - ca) * t):02X}" for ca, cb in zip(a, b))


def load_theme(conf: Path) -> dict:
    data = tomllib.loads(conf.read_text(encoding="utf-8"))
    data["_id"] = data["meta"]["id"]
    return data


# ------------------------------------------------------------------- QSS ---

def _relative_luminance(hexstr: str) -> float:
    """WCAG relative luminance of ``#RRGGBB`` (0 = black, 1 = white)."""
    h = hexstr.lstrip("#")
    chans = (int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4))

    def lin(v: float) -> float:
        return v / 12.92 if v <= 0.03928 else ((v + 0.055) / 1.055) ** 2.4

    r, g, b = (lin(v) for v in chans)
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def build_qss(theme: dict) -> str:
    c, m = theme["colors"], theme["metrics"]
    hc = bool(theme["meta"].get("high_contrast", False))
    # Input fields and item views read on a "paper" background. On light themes
    # that's white on a grey chrome; on dark themes it must follow the theme
    # (else views/line-edits stay glaringly white). Derive it from the surface
    # brightness so every theme — light, high-contrast or the dark ones — is
    # internally consistent without a hand-set flag.
    dark = _relative_luminance(c["surface"]) < 0.5
    field = c["surface_alt"] if dark else "#FFFFFF"
    field_ink = c["text"] if dark else "#1E1E1E"
    hover = mix(c["surface"], c["accent"], 0.12)
    pressed = mix(c["surface_alt"], c["accent"], 0.18)
    # The default-button tint: light enough that `text` stays legible on it
    # in every theme (high-contrast included), strong enough to read as
    # "this is the action".
    default_top = mix(c["surface"], c["accent"], 0.10)
    default_bottom = mix(c["surface_alt"], c["accent"], 0.16)
    rad = m["corner_radius"]

    return f"""\
/* Castalia Qt stylesheet — GENERATED from themes/{theme['_id']}/theme.conf
 * by tools/theme_export.py. Do not edit; edit the tokens. */

QWidget {{
    background-color: {c['surface']};
    color: {c['text']};
    selection-background-color: {c['selection_bg']};
    selection-color: {c['selection_text']};
}}
QLabel, QCheckBox, QRadioButton {{ background: transparent; }}
QLabel[secondary="true"] {{ color: {c['text_secondary']}; }}

/* ---- buttons ---- */
QPushButton {{
    background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1,
        stop:0 {c['surface']}, stop:1 {c['surface_alt']});
    border: 1px solid {c['border']};
    border-radius: {rad}px;
    padding: 4px 14px;
    min-height: 18px;
}}
QPushButton:hover {{ background-color: {hover}; }}
QPushButton:pressed {{ background-color: {pressed}; }}
QPushButton:default {{
    /* The default action is signalled with the accent border and a warm
       tint — deliberately NOT with a bolder font. A font-weight set from a
       stylesheet is not accounted for in the widget's size hint, so a bold
       default button lays itself out at normal-weight width and then paints
       wider: long labels ("Sí, cerrar sesión") clip. §7.11 is explicit that
       dialogs must fit 800x600 "with no clipped buttons", so weight is not
       ours to change here. */
    border-color: {c['accent']};
    border-width: 2px;
    padding: 3px 13px;
    background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1,
        stop:0 {default_top}, stop:1 {default_bottom});
}}
QPushButton:disabled {{ color: {c['text_secondary']}; }}
QPushButton:focus {{ outline: none; border-color: {c['accent']}; }}

/* ---- text fields ---- */
QLineEdit, QPlainTextEdit, QTextEdit, QSpinBox, QDoubleSpinBox {{
    background-color: {field};
    color: {field_ink};
    border: 1px solid {c['border']};
    border-radius: {rad}px;
    padding: 3px 6px;
}}
QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus,
QSpinBox:focus, QDoubleSpinBox:focus {{ border-color: {c['accent']}; }}
QLineEdit:disabled {{ color: {c['text_secondary']}; }}

/* ---- combo box ---- */
QComboBox {{
    background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1,
        stop:0 {c['surface']}, stop:1 {c['surface_alt']});
    border: 1px solid {c['border']};
    border-radius: {rad}px;
    padding: 3px 8px;
}}
QComboBox:focus {{ border-color: {c['accent']}; }}
QComboBox QAbstractItemView {{
    background-color: {c['surface']};
    border: 1px solid {c['border']};
    selection-background-color: {c['selection_bg']};
    selection-color: {c['selection_text']};
}}

/* ---- menus ---- */
QMenuBar {{
    background-color: {c['surface']};
    border-bottom: 1px solid {c['border']};
}}
QMenuBar::item {{ padding: 4px 10px; background: transparent; }}
QMenuBar::item:selected {{
    background-color: {c['selection_bg']};
    color: {c['selection_text']};
}}
QMenu {{
    background-color: {c['surface']};
    border: 1px solid {c['border']};
    padding: 3px;
}}
QMenu::item {{ padding: 4px 24px 4px 20px; border-radius: {rad}px; }}
QMenu::item:selected {{
    background-color: {c['selection_bg']};
    color: {c['selection_text']};
}}
QMenu::item:disabled {{ color: {c['text_secondary']}; }}
QMenu::separator {{
    height: 1px; background: {c['border']}; margin: 4px 8px;
}}

/* ---- toolbar / statusbar ---- */
QToolBar {{
    background-color: {c['surface_alt']};
    border-bottom: 1px solid {c['border']};
    spacing: 4px; padding: 3px;
}}
QToolButton {{
    background: transparent;
    border: 1px solid transparent;
    border-radius: {rad}px;
    padding: 3px 8px;
}}
QToolButton:hover {{ background-color: {hover}; border-color: {c['border']}; }}
QToolButton:pressed {{ background-color: {pressed}; }}
QStatusBar {{
    background-color: {c['surface_alt']};
    color: {c['text_secondary']};
    border-top: 1px solid {c['border']};
}}

/* ---- scrollbars ---- */
QScrollBar:vertical {{
    background: {c['surface_alt']}; width: 14px; margin: 0;
    border-left: 1px solid {c['border']};
}}
QScrollBar:horizontal {{
    background: {c['surface_alt']}; height: 14px; margin: 0;
    border-top: 1px solid {c['border']};
}}
QScrollBar::handle {{
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
        stop:0 {c['surface']}, stop:1 {c['surface_alt']});
    border: 1px solid {c['border']};
    border-radius: {rad}px;
    min-height: 24px; min-width: 24px; margin: 2px;
}}
QScrollBar::handle:hover {{ background: {hover}; }}
QScrollBar::add-line, QScrollBar::sub-line {{ height: 0; width: 0; }}
QScrollBar::add-page, QScrollBar::sub-page {{ background: transparent; }}

/* ---- tabs ---- */
QTabWidget::pane {{
    border: 1px solid {c['border']};
    border-radius: {rad}px;
    top: -1px;
}}
QTabBar::tab {{
    background: {c['surface']};
    color: {c['text_secondary']};
    border: 1px solid {c['border']};
    border-bottom: none;
    border-top-left-radius: {rad}px;
    border-top-right-radius: {rad}px;
    padding: 4px 14px; margin-right: 2px;
}}
/* No font-weight change on selection. Qt sizes a tab with the UNSELECTED
   font and then draws the selected one bold, so the wider text overflowed
   its own tab: the first tab read "Estad". The accent bar and the darker
   text say "selected" without touching the metrics — which is the XP-era
   tab language anyway. */
QTabBar::tab:selected {{
    color: {c['text']};
    background: {c['surface_alt']};
    border-top: 2px solid {c['accent']};
}}

/* ---- check / radio ---- */
QCheckBox::indicator, QRadioButton::indicator {{
    width: 14px; height: 14px;
    border: 1px solid {c['border']};
    background: {field};
}}
QCheckBox::indicator {{ border-radius: 2px; }}
QRadioButton::indicator {{ border-radius: 7px; }}
QCheckBox::indicator:checked, QRadioButton::indicator:checked {{
    background: {c['accent']};
    border-color: {c['accent']};
}}
QCheckBox::indicator:focus, QRadioButton::indicator:focus {{
    border-color: {c['accent']};
}}

/* ---- progress / slider ---- */
QProgressBar {{
    background: {field};
    border: 1px solid {c['border']};
    border-radius: {rad}px;
    text-align: center;
    color: {c['text']};
    height: 14px;
}}
QProgressBar::chunk {{
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
        stop:0 {c['accent']}, stop:1 {c['selection_bg']});
    border-radius: {rad}px;
}}
QSlider::groove:horizontal {{
    height: 4px; background: {c['surface_alt']};
    border: 1px solid {c['border']}; border-radius: 2px;
}}
QSlider::handle:horizontal {{
    width: 10px; margin: -6px 0;
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
        stop:0 {c['surface']}, stop:1 {c['surface_alt']});
    border: 1px solid {c['accent']};
    border-radius: {rad}px;
}}

/* ---- item views ---- */
QListView, QTreeView, QTableView {{
    background: {field};
    color: {field_ink};
    alternate-background-color: {mix(field, c['surface_alt'], 0.5)};
    border: 1px solid {c['border']};
}}
QListView::item, QTreeView::item {{ padding: 3px; }}
QListView::item:selected, QTreeView::item:selected,
QTableView::item:selected {{
    background: {c['selection_bg']};
    color: {c['selection_text']};
}}
QHeaderView::section {{
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
        stop:0 {c['surface']}, stop:1 {c['surface_alt']});
    color: {c['text']};
    border: none; border-right: 1px solid {c['border']};
    border-bottom: 1px solid {c['border']};
    padding: 4px 8px;
}}

/* ---- misc ---- */
QToolTip {{
    background-color: {mix(c['surface'], '#FFF8DC' if not hc else '#000000', 0.5)};
    color: {c['text']};
    border: 1px solid {c['border']};
    padding: 3px 6px;
}}
QGroupBox {{
    border: 1px solid {c['border']};
    border-radius: {rad}px;
    margin-top: 10px; padding-top: 6px;
}}
QGroupBox::title {{
    subcontrol-origin: margin;
    left: 8px; padding: 0 4px;
    color: {c['text_secondary']};
}}
QSplitter::handle {{ background: {c['border']}; }}
"""


# --------------------------------------------------------------- Openbox ---

def build_openbox(theme: dict) -> str:
    c, m = theme["colors"], theme["metrics"]
    pad_h = max(2, (m["titlebar_height"] - 15) // 2)
    inactive_btn = mix(c["titlebar_inactive_top"],
                       c["titlebar_inactive_bottom"], 0.5)

    return f"""\
# Castalia Openbox 3 theme — GENERATED from themes/{theme['_id']}/theme.conf
# by tools/theme_export.py. Do not edit; edit the tokens.

# ---- geometry (4px design grid, titlebar {m['titlebar_height']}px) ----
border.width: 1
padding.width: 4
padding.height: {pad_h}
window.handle.width: 3
window.client.padding.width: 0
menu.overlap: 0

# ---- window borders ----
window.active.border.color: {c['border']}
window.inactive.border.color: {c['border']}

# ---- active titlebar: the token gradient ----
window.active.title.bg: gradient vertical
window.active.title.bg.color: {c['titlebar_top']}
window.active.title.bg.colorTo: {c['titlebar_bottom']}
window.active.label.bg: parentrelative
window.active.label.text.color: {c['titlebar_text']}
window.label.text.justify: left

# ---- inactive titlebar ----
window.inactive.title.bg: gradient vertical
window.inactive.title.bg.color: {c['titlebar_inactive_top']}
window.inactive.title.bg.colorTo: {c['titlebar_inactive_bottom']}
window.inactive.label.bg: parentrelative
window.inactive.label.text.color: {c['titlebar_inactive_text']}

# ---- window buttons (close = accent-tinted, Bible §8.3) ----
window.active.button.unpressed.bg: flat solid
window.active.button.unpressed.bg.color: {c['surface_alt']}
window.active.button.unpressed.image.color: {c['text']}
window.active.button.hover.bg: flat solid
window.active.button.hover.bg.color: {mix(c['surface_alt'], c['accent'], 0.25)}
window.active.button.hover.image.color: {c['text']}
window.active.button.pressed.bg: flat solid
window.active.button.pressed.bg.color: {c['selection_bg']}
window.active.button.pressed.image.color: {c['selection_text']}
window.active.button.disabled.bg: flat solid
window.active.button.disabled.bg.color: {c['surface_alt']}
window.active.button.disabled.image.color: {c['text_secondary']}
window.active.button.close.unpressed.bg: flat solid
window.active.button.close.unpressed.bg.color: {c['accent']}
window.active.button.close.unpressed.image.color: {c['selection_text']}
window.active.button.close.hover.bg: flat solid
window.active.button.close.hover.bg.color: {mix(c['accent'], '#FFFFFF', 0.18)}
window.active.button.close.hover.image.color: {c['selection_text']}

window.inactive.button.unpressed.bg: flat solid
window.inactive.button.unpressed.bg.color: {inactive_btn}
window.inactive.button.unpressed.image.color: {c['titlebar_inactive_text']}

# ---- menus ----
menu.title.bg: gradient vertical
menu.title.bg.color: {c['titlebar_top']}
menu.title.bg.colorTo: {c['titlebar_bottom']}
menu.title.text.color: {c['titlebar_text']}
menu.title.text.justify: left
menu.items.bg: flat solid
menu.items.bg.color: {c['surface']}
menu.items.text.color: {c['text']}
menu.items.disabled.text.color: {c['text_secondary']}
menu.items.active.bg: flat solid
menu.items.active.bg.color: {c['selection_bg']}
menu.items.active.text.color: {c['selection_text']}
menu.border.width: 1
menu.border.color: {c['border']}
menu.separator.color: {c['border']}
menu.separator.width: 1
menu.separator.padding.width: 6
menu.separator.padding.height: 3

# ---- on-screen display (Alt+Tab etc.) ----
osd.bg: flat solid
osd.bg.color: {c['surface']}
osd.border.width: 1
osd.border.color: {c['border']}
osd.label.bg: parentrelative
osd.label.text.color: {c['text']}
osd.hilight.bg: flat solid
osd.hilight.bg.color: {c['selection_bg']}
osd.unhilight.bg: flat solid
osd.unhilight.bg.color: {c['surface_alt']}
"""


# ------------------------------------------------------------------ main ---

def export_theme(theme: dict, out_root: Path) -> list[Path]:
    tid = theme["_id"]
    qss = out_root / tid / "castalia.qss"
    obt = out_root / tid / "openbox-3" / "themerc"
    qss.parent.mkdir(parents=True, exist_ok=True)
    obt.parent.mkdir(parents=True, exist_ok=True)
    qss.write_text(build_qss(theme), encoding="utf-8")
    obt.write_text(build_openbox(theme), encoding="utf-8")
    return [qss, obt]


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path,
                        default=REPO / "build" / "out" / "themes")
    parser.add_argument("--theme", help="export a single theme id")
    args = parser.parse_args(argv[1:])

    confs = sorted((REPO / "themes").glob("*/theme.conf"))
    if args.theme:
        confs = [p for p in confs if p.parent.name == args.theme]
        if not confs:
            raise SystemExit(f"theme-export: no theme {args.theme!r}")

    for conf in confs:
        theme = load_theme(conf)
        for path in export_theme(theme, args.out):
            rel = path.relative_to(REPO) if path.is_relative_to(REPO) else path
            print(f"theme-export: wrote {rel}")
    print(f"theme-export: OK ({len(confs)} theme(s))")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
