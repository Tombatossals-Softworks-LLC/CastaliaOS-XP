"""Provenance gate — the legal discipline of docs/PROJECT_BIBLE.md §3.9.

Every shipped asset under ``branding/`` or ``themes/`` (art, sound, font,
cursor — including SVG sources) MUST have a row in
``legal/ASSET_PROVENANCE.csv`` recording its source, author, and license.
This tool fails the build when:

* an asset file exists with no ledger row (untracked asset — the dangerous
  case: nothing lands "just for now");
* a ledger row points at a file that does not exist (stale ledger);
* a ledger row is missing a required field (asset_path, type, source,
  author, license).

Exit code 0 = clean; 1 = violations (printed one per line).

Usage:  PYTHONPATH=tools python3 -m castalia_qa.provenance [repo_root]
"""

from __future__ import annotations

import csv
import sys
from pathlib import Path

#: Directories whose assets must be provenance-tracked.
ASSET_ROOTS = ("branding", "themes", "iso")

#: File extensions treated as shipped assets. SVG is text but is still art —
#: it is tracked like any other asset.
ASSET_EXTENSIONS = {
    ".png", ".jpg", ".jpeg", ".gif", ".bmp", ".ico", ".icns", ".svg",
    ".ogg", ".wav", ".flac", ".mp3",
    ".ttf", ".otf", ".woff", ".woff2",
    ".cur", ".ani", ".xcf", ".kra", ".blend",
}

REQUIRED_FIELDS = ("asset_path", "type", "source", "author", "license")

LEDGER = Path("legal/ASSET_PROVENANCE.csv")


def find_assets(root: Path) -> set[str]:
    """All asset files (repo-relative POSIX paths) under the tracked roots."""
    found: set[str] = set()
    for asset_root in ASSET_ROOTS:
        base = root / asset_root
        if not base.is_dir():
            continue
        for path in base.rglob("*"):
            if path.is_file() and path.suffix.lower() in ASSET_EXTENSIONS:
                found.add(path.relative_to(root).as_posix())
    return found


def read_ledger(root: Path) -> tuple[list[dict[str, str]], list[str]]:
    """Parse the ledger, skipping comment lines. Returns (rows, errors)."""
    ledger_path = root / LEDGER
    errors: list[str] = []
    if not ledger_path.is_file():
        return [], [f"{LEDGER.as_posix()}: ledger file is missing"]

    with ledger_path.open(newline="", encoding="utf-8") as handle:
        lines = [line for line in handle if not line.lstrip().startswith("#")]
    reader = csv.DictReader(lines)

    rows: list[dict[str, str]] = []
    for index, row in enumerate(reader, start=2):
        cleaned = {(k or "").strip(): (v or "").strip() for k, v in row.items()}
        if not any(cleaned.values()):
            continue  # blank line
        missing = [f for f in REQUIRED_FIELDS if not cleaned.get(f)]
        if missing:
            errors.append(
                f"{LEDGER.as_posix()}: row {index} "
                f"({cleaned.get('asset_path') or '?'}) missing required "
                f"field(s): {', '.join(missing)}"
            )
        rows.append(cleaned)
    return rows, errors


def check(root: Path) -> list[str]:
    """Run the full gate. Returns a list of violation strings (empty = clean)."""
    assets = find_assets(root)
    rows, errors = read_ledger(root)
    ledger_paths = {row["asset_path"] for row in rows if row.get("asset_path")}

    for asset in sorted(assets - ledger_paths):
        errors.append(
            f"{asset}: shipped asset has NO row in {LEDGER.as_posix()} "
            "(add source/author/license before it can land — Bible §3.9)"
        )
    for entry in sorted(ledger_paths):
        if entry not in assets and not (root / entry).is_file():
            errors.append(
                f"{LEDGER.as_posix()}: entry {entry!r} points at a file that "
                "does not exist (stale ledger row)"
            )
    return errors


def main(argv: list[str]) -> int:
    root = Path(argv[1]) if len(argv) > 1 else Path(".")
    errors = check(root)
    if errors:
        for line in errors:
            print(f"provenance-check: {line}", file=sys.stderr)
        print(
            f"provenance-check: FAILED ({len(errors)} violation(s))",
            file=sys.stderr,
        )
        return 1
    tracked = len(find_assets(root))
    print(f"provenance-check: OK ({tracked} asset(s), all provenance-tracked)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
