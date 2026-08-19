"""WCAG color math used by the Castalia theme linter.

Implements sRGB relative luminance and contrast ratio exactly as defined by
WCAG 2.x, plus the Bible's gradient rule (§8.2): a titlebar gradient's two
stops may differ by at most 0.12 in relative luminance so it renders cleanly
(ditherable, band-free) on 16-bit-era display modes.
"""

from __future__ import annotations

import re

_HEX_RE = re.compile(r"^#([0-9a-fA-F]{6})$")

#: Maximum relative-luminance delta allowed between two gradient stops (§8.2).
GRADIENT_MAX_LUMINANCE_DELTA = 0.12

#: WCAG AA contrast for normal text (§8.2 / §7.11 accessibility baseline).
CONTRAST_AA = 4.5

#: WCAG AAA contrast, required for the High Contrast theme (§8.2).
CONTRAST_AAA = 7.0

#: WCAG AA contrast for large text — used for text over gradient *stops*
#: (the strict 4.5 check is done against the gradient midpoint).
CONTRAST_LARGE = 3.0


def parse_hex(value: str) -> tuple[int, int, int]:
    """Parse ``#RRGGBB`` into an (r, g, b) tuple. Raises ValueError otherwise."""
    match = _HEX_RE.match(value.strip())
    if not match:
        raise ValueError(f"not a #RRGGBB color: {value!r}")
    raw = match.group(1)
    return tuple(int(raw[i : i + 2], 16) for i in (0, 2, 4))  # type: ignore[return-value]


def _linearize(channel: float) -> float:
    """sRGB channel (0..1) -> linear-light value, per WCAG."""
    if channel <= 0.04045:
        return channel / 12.92
    return ((channel + 0.055) / 1.055) ** 2.4


def relative_luminance(color: str) -> float:
    """WCAG relative luminance (0.0 = black, 1.0 = white) of a #RRGGBB color."""
    r, g, b = (c / 255.0 for c in parse_hex(color))
    return 0.2126 * _linearize(r) + 0.7152 * _linearize(g) + 0.0722 * _linearize(b)


def contrast_ratio(color_a: str, color_b: str) -> float:
    """WCAG contrast ratio between two #RRGGBB colors (1.0 .. 21.0)."""
    la = relative_luminance(color_a)
    lb = relative_luminance(color_b)
    lighter, darker = max(la, lb), min(la, lb)
    return (lighter + 0.05) / (darker + 0.05)


def gradient_luminance_delta(top: str, bottom: str) -> float:
    """Absolute relative-luminance difference between two gradient stops."""
    return abs(relative_luminance(top) - relative_luminance(bottom))


def gradient_midpoint_luminance(top: str, bottom: str) -> float:
    """Mean of the two stops' luminance — the reference for text-on-gradient."""
    return (relative_luminance(top) + relative_luminance(bottom)) / 2.0


def contrast_vs_luminance(color: str, luminance: float) -> float:
    """Contrast ratio between a color and an arbitrary luminance value."""
    lc = relative_luminance(color)
    lighter, darker = max(lc, luminance), min(lc, luminance)
    return (lighter + 0.05) / (darker + 0.05)
