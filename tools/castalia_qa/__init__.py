"""Castalia OS QA tooling.

Enforces the Project Bible's design and legal rules as code:

- ``color``       — WCAG relative-luminance / contrast math shared by linters.
- ``theme_lint``  — validates theme bundles against the schema and the visual
                    design rules of docs/PROJECT_BIBLE.md §8 (gradient
                    luminance delta, contrast minima, metric ranges).
- ``provenance``  — the legal gate of §3.9: every shipped asset under
                    branding/ or themes/ must have a row in
                    legal/ASSET_PROVENANCE.csv.

Python is tooling-only in Castalia (§12): nothing here ships on the target OS.
"""

__all__ = ["color", "theme_lint", "provenance"]
