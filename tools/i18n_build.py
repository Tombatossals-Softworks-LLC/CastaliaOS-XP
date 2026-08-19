#!/usr/bin/env python3
"""Castalia's translation toolchain (Bible §7.13).

Castalia's source language is **Spanish**: the literals in the C++ are what a
Spanish user reads, and `castalia_es.qm` does not exist because it would be an
identity map. Every other language is a Qt catalogue on top.

Three jobs, one script, so nobody has to remember lupdate's argument order:

    tools/i18n_build.py extract      # sources → i18n/castalia_<lang>.ts
    tools/i18n_build.py release      # i18n/*.ts → build/out/i18n/*.qm
    tools/i18n_build.py --check      # what is missing, without writing

`--check` is the one CI and `tools/tests/test_i18n.py` care about: it exits
non-zero when a shipped language has untranslated or obsolete strings, which
is the only way a half-translated interface gets noticed before a user sees
a menu with Spanish in one half and English in the other.
"""
from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
I18N = REPO / "i18n"
OUT = REPO / "build" / "out" / "i18n"

#: The languages that ship, mirroring kLanguages in
#: shell/libcastalia-ui/Locale.cpp. "es" is the source language and is
#: deliberately absent: it has no catalogue.
LANGUAGES = ("en",)

#: Where translatable source lives. Trees, not a file list, so a new app is
#: picked up the day it is added rather than the day somebody remembers.
SOURCES = ("shell", "apps")


def qt_tool(name: str) -> str:
    """lupdate/lrelease, wherever the distribution put them."""
    for candidate in (name, f"{name}-qt5", f"/usr/lib/qt5/bin/{name}"):
        found = shutil.which(candidate) or (
            candidate if Path(candidate).is_file() else None)
        if found:
            return found
    raise SystemExit(
        f"{name} not found — install qttools5-dev-tools to build translations")


def sources() -> list[str]:
    paths: list[str] = []
    for tree in SOURCES:
        root = REPO / tree
        for ext in ("*.cpp", "*.h"):
            paths += [str(p.relative_to(REPO)) for p in sorted(root.rglob(ext))]
    return paths


def ts_path(lang: str) -> Path:
    return I18N / f"castalia_{lang}.ts"


def extract(langs=LANGUAGES) -> int:
    I18N.mkdir(parents=True, exist_ok=True)
    tool = qt_tool("lupdate")
    for lang in langs:
        # -locations none: line numbers churn on every unrelated edit and turn
        # the diff of a translation file into noise.
        # -no-obsolete: a string nobody uses is not a translation debt.
        cmd = [tool, "-locations", "none", "-no-obsolete",
               *sources(), "-ts", str(ts_path(lang))]
        r = subprocess.run(cmd, cwd=REPO, capture_output=True, text=True)
        if r.returncode != 0:
            sys.stderr.write(r.stdout + r.stderr)
            return r.returncode
        print(f"extract: {ts_path(lang).relative_to(REPO)}")
    return 0


def release(langs=LANGUAGES) -> int:
    OUT.mkdir(parents=True, exist_ok=True)
    tool = qt_tool("lrelease")
    for lang in langs:
        src = ts_path(lang)
        if not src.is_file():
            sys.stderr.write(f"missing catalogue: {src}\n")
            return 1
        dst = OUT / f"castalia_{lang}.qm"
        r = subprocess.run([tool, str(src), "-qm", str(dst)],
                           cwd=REPO, capture_output=True, text=True)
        if r.returncode != 0:
            sys.stderr.write(r.stdout + r.stderr)
            return r.returncode
        print(f"release: {dst.relative_to(REPO)}")
    return 0


def untranslated(lang: str) -> list[tuple[str, str]]:
    """(context, source) for every message with no usable translation.

    Qt marks these `type="unfinished"`; an empty <translation> counts too,
    because lrelease silently falls back to the source string and the result
    is a half-Spanish English interface with no error anywhere.
    """
    path = ts_path(lang)
    if not path.is_file():
        return [("", f"no catalogue at {path.relative_to(REPO)}")]
    root = ET.parse(path).getroot()
    missing = []
    for context in root.findall("context"):
        name = (context.findtext("name") or "").strip()
        for message in context.findall("message"):
            tr = message.find("translation")
            source = (message.findtext("source") or "").strip()
            if tr is None or tr.get("type") == "unfinished" \
                    or not (tr.text or "").strip():
                missing.append((name, source))
    return missing


def check(langs=LANGUAGES) -> int:
    bad = 0
    for lang in langs:
        missing = untranslated(lang)
        total = 0
        path = ts_path(lang)
        if path.is_file():
            total = sum(1 for _ in ET.parse(path).getroot().iter("message"))
        if missing:
            bad = 1
            print(f"{lang}: {len(missing)}/{total} strings untranslated")
            for context, source in missing[:20]:
                print(f"    {context}: {source[:70]}")
            if len(missing) > 20:
                print(f"    … and {len(missing) - 20} more")
        else:
            print(f"{lang}: {total}/{total} strings translated")
    return bad


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("action", nargs="?", default="check",
                    choices=("extract", "release", "check", "all"))
    ap.add_argument("--check", action="store_true",
                    help="report untranslated strings and exit non-zero")
    ap.add_argument("--lang", action="append", default=None,
                    help="restrict to one language (repeatable)")
    args = ap.parse_args()
    langs = tuple(args.lang) if args.lang else LANGUAGES

    if args.check or args.action == "check":
        return check(langs)
    if args.action == "extract":
        return extract(langs)
    if args.action == "release":
        return release(langs)
    return extract(langs) or check(langs) or release(langs)


if __name__ == "__main__":
    raise SystemExit(main())
