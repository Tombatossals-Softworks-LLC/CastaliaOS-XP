"""Castalia theme linter — the visual design system, enforced as code.

Validates every ``theme.conf`` (TOML) bundle under ``themes/<id>/`` against:

Schema (themes/SCHEMA.md)
    Required sections and keys, correct types, ``meta.id`` matching the
    directory name.

Design rules (docs/PROJECT_BIBLE.md §8)
    * Titlebar gradients: relative-luminance delta between stops <= 0.12
      so gradients stay band-free on 16-bit-era display modes.
    * Text contrast: body text vs surface >= 4.5:1 (>= 7:1 when the theme
      declares ``meta.high_contrast = true``); titlebar text >= 4.5:1 against
      the gradient midpoint and >= 3:1 against each stop; selection text
      >= 4.5:1 against the selection background; inactive titlebar text
      >= 3:1 against the inactive gradient midpoint.
    * Metrics: titlebar/panel heights and corner radius within the ranges the
      design grid allows; the base unit is fixed at 4 px.

Exit code 0 = all themes pass; 1 = violations (printed one per line).

Usage:  PYTHONPATH=tools python3 -m castalia_qa.theme_lint themes/
"""

from __future__ import annotations

import sys
import tomllib
from pathlib import Path

from castalia_qa import color

REQUIRED_META = ("name", "id", "version", "author")
REQUIRED_COLORS = (
    "accent",
    "titlebar_top",
    "titlebar_bottom",
    "titlebar_text",
    "titlebar_inactive_top",
    "titlebar_inactive_bottom",
    "titlebar_inactive_text",
    "surface",
    "surface_alt",
    "text",
    "text_secondary",
    "selection_bg",
    "selection_text",
    "border",
)
REQUIRED_METRICS = (
    "base_unit",
    "titlebar_height",
    "corner_radius",
    "panel_height",
    "panel_height_800",
)
REQUIRED_FONTS = ("ui", "mono")

METRIC_RANGES = {
    "titlebar_height": (20, 32),
    "corner_radius": (0, 4),
    "panel_height": (24, 36),
    "panel_height_800": (24, 32),
}
BASE_UNIT = 4


def lint_theme(conf_path: Path) -> list[str]:
    """Lint one theme.conf. Returns a list of violation strings (empty = pass)."""
    errors: list[str] = []
    rel = conf_path.as_posix()

    try:
        data = tomllib.loads(conf_path.read_text(encoding="utf-8"))
    except (tomllib.TOMLDecodeError, UnicodeDecodeError) as exc:
        return [f"{rel}: unparseable TOML: {exc}"]

    meta = data.get("meta", {})
    colors = data.get("colors", {})
    metrics = data.get("metrics", {})
    fonts = data.get("fonts", {})

    # --- schema ---------------------------------------------------------
    for key in REQUIRED_META:
        if not isinstance(meta.get(key), str) or not meta.get(key):
            errors.append(f"{rel}: [meta].{key} missing or not a string")
    for key in REQUIRED_COLORS:
        value = colors.get(key)
        if not isinstance(value, str):
            errors.append(f"{rel}: [colors].{key} missing")
            continue
        try:
            color.parse_hex(value)
        except ValueError:
            errors.append(f"{rel}: [colors].{key} is not #RRGGBB: {value!r}")
    for key in REQUIRED_METRICS:
        if not isinstance(metrics.get(key), int):
            errors.append(f"{rel}: [metrics].{key} missing or not an integer")
    for key in REQUIRED_FONTS:
        if not isinstance(fonts.get(key), str) or not fonts.get(key):
            errors.append(f"{rel}: [fonts].{key} missing or not a string")

    expected_id = conf_path.parent.name
    if isinstance(meta.get("id"), str) and meta["id"] != expected_id:
        errors.append(
            f"{rel}: [meta].id {meta['id']!r} does not match directory "
            f"{expected_id!r}"
        )

    if errors:
        return errors  # color/metric rules below assume a valid schema

    high_contrast = bool(meta.get("high_contrast", False))
    text_min = color.CONTRAST_AAA if high_contrast else color.CONTRAST_AA

    # --- gradient rule (16-bit safety, §8.2) ------------------------------
    for top_key, bottom_key, label in (
        ("titlebar_top", "titlebar_bottom", "titlebar"),
        ("titlebar_inactive_top", "titlebar_inactive_bottom", "inactive titlebar"),
    ):
        delta = color.gradient_luminance_delta(colors[top_key], colors[bottom_key])
        if delta > color.GRADIENT_MAX_LUMINANCE_DELTA:
            errors.append(
                f"{rel}: {label} gradient luminance delta {delta:.3f} exceeds "
                f"{color.GRADIENT_MAX_LUMINANCE_DELTA} (bands on 16-bit modes)"
            )

    # --- contrast rules ---------------------------------------------------
    def need(actual: float, minimum: float, what: str) -> None:
        if actual < minimum:
            errors.append(f"{rel}: {what} contrast {actual:.2f} < {minimum}")

    need(color.contrast_ratio(colors["text"], colors["surface"]), text_min,
         "text vs surface")
    need(color.contrast_ratio(colors["text_secondary"], colors["surface"]),
         color.CONTRAST_LARGE, "text_secondary vs surface")
    need(color.contrast_ratio(colors["selection_text"], colors["selection_bg"]),
         text_min, "selection_text vs selection_bg")

    active_mid = color.gradient_midpoint_luminance(
        colors["titlebar_top"], colors["titlebar_bottom"]
    )
    need(color.contrast_vs_luminance(colors["titlebar_text"], active_mid),
         color.CONTRAST_AA, "titlebar_text vs gradient midpoint")
    for stop in ("titlebar_top", "titlebar_bottom"):
        need(color.contrast_ratio(colors["titlebar_text"], colors[stop]),
             color.CONTRAST_LARGE, f"titlebar_text vs {stop}")

    inactive_mid = color.gradient_midpoint_luminance(
        colors["titlebar_inactive_top"], colors["titlebar_inactive_bottom"]
    )
    need(
        color.contrast_vs_luminance(colors["titlebar_inactive_text"], inactive_mid),
        color.CONTRAST_LARGE,
        "titlebar_inactive_text vs inactive gradient midpoint",
    )

    # --- metric ranges ------------------------------------------------------
    if metrics["base_unit"] != BASE_UNIT:
        errors.append(
            f"{rel}: [metrics].base_unit must be {BASE_UNIT} (the design grid)"
        )
    for key, (low, high) in METRIC_RANGES.items():
        if not low <= metrics[key] <= high:
            errors.append(
                f"{rel}: [metrics].{key}={metrics[key]} outside {low}..{high}"
            )

    return errors


def lint_tree(themes_dir: Path) -> list[str]:
    """Lint every themes/<id>/theme.conf under *themes_dir*."""
    confs = sorted(themes_dir.glob("*/theme.conf"))
    if not confs:
        return [f"{themes_dir.as_posix()}: no */theme.conf bundles found"]
    errors: list[str] = []
    for conf in confs:
        errors.extend(lint_theme(conf))
    return errors


def main(argv: list[str]) -> int:
    themes_dir = Path(argv[1]) if len(argv) > 1 else Path("themes")
    errors = lint_tree(themes_dir)
    if errors:
        for line in errors:
            print(f"theme-lint: {line}", file=sys.stderr)
        print(f"theme-lint: FAILED ({len(errors)} violation(s))", file=sys.stderr)
        return 1
    count = len(sorted(themes_dir.glob("*/theme.conf")))
    print(f"theme-lint: OK ({count} theme(s) pass the design system)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
