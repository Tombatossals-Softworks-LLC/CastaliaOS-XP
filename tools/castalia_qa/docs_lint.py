"""The §20 documentation set, enforced as code.

§20 makes the documentation a *shipped product* and gives it one rule that is
easy to write and easy to forget: "each doc lists its 'last verified on
version'". A manual that quietly describes a release ago is worse than one
that admits it, because a reader cannot tell the difference. So the rule is
checked rather than remembered.

Three things are checked, in order of how much damage they do:

1. **The set exists.** §20 names thirteen documents. A missing one is not a
   gap somebody will notice — it is a Help Center with a hole in it that
   nobody opens until they need exactly that page.
2. **Every page says when it was last verified, and against what.** A stale
   version string is a warning; a missing one is a failure, because a page
   with no claim at all is the one that rots invisibly.
3. **Internal links resolve.** A broken link in a manual is a dead end at the
   exact moment somebody was following instructions.

Pure functions of files on disk, so the whole thing is unit-testable and runs
in the `gates` tier with no toolchain.
"""

from __future__ import annotations

import re
import sys
from dataclasses import dataclass
from pathlib import Path

#: The §20 set, as (directory or file, human name). Each entry is a document
#: the Bible promises; the value is what a failure message calls it.
REQUIRED = (
    ("docs/PROJECT_BIBLE.md", "Project Bible"),
    ("docs/user/README.md", "User Manual"),
    ("docs/install/README.md", "Install Guide"),
    ("docs/recovery/README.md", "Recovery Guide"),
    ("docs/troubleshooting/README.md", "Troubleshooting Guide"),
    ("docs/wine/README.md", "Wine Compatibility Guide"),
    ("docs/hardware/README.md", "Hardware Compatibility Guide"),
    ("docs/dev/README.md", "Developer Guide"),
    ("docs/theming/README.md", "Theming Guide"),
    ("docs/packaging/README.md", "Packaging Guide"),
    ("docs/legal/README.md", "Legal Notice"),
    ("docs/releases/TEMPLATE.md", "Release Notes template"),
    ("docs/releases/KNOWN_ISSUES.md", "Known Issues"),
)

#: Directories whose every .md file must carry a verified-on line. The Bible
#: is excluded: it is the design document, it is versioned by the repository
#: rather than by a release, and stamping it would be a claim nobody checks.
STAMPED_DIRS = ("user", "install", "recovery", "troubleshooting", "wine",
                "hardware", "dev", "theming", "packaging", "legal",
                "releases")

#: Pages that are exempt from the stamp, and why. Kept as an explicit list
#: rather than a pattern so that adding to it is a decision somebody makes on
#: purpose — the whole value of the rule is that it is hard to opt out of.
EXEMPT = {
    # A blank form. Its own "last verified" line is the one the person
    # filling it in writes about the release, and a second stamp above it
    # would be read as that date rather than as this file's.
    "docs/releases/TEMPLATE.md",
}

#: Both spellings of §20's rule. Spanish for the pages that become the Help
#: Center, English for the contributor-facing ones.
STAMP = re.compile(
    r"^\*(?:Verificado en la versi[oó]n|Last verified on version)\s+"
    r"(?P<version>[0-9]+\.[0-9]+(?:\.[0-9]+)?)[.,]?\s*"
    r"(?:[^*]*)?\*", re.MULTILINE)

#: A markdown link that points at something in this repository. Anchors,
#: mailto: and absolute URLs are somebody else's problem.
LINK = re.compile(r"\[[^\]]*\]\((?P<target>[^)\s]+)\)")


@dataclass(frozen=True)
class Problem:
    path: str
    message: str
    fatal: bool = True

    def __str__(self) -> str:
        mark = "error" if self.fatal else "warning"
        return f"{self.path}: {mark}: {self.message}"


def stamped_files(repo: Path) -> list:
    """Every markdown page that must carry a verified-on line.

    *repo* is needed to compute the paths :data:`EXEMPT` is expressed in;
    exemptions are written repo-relative so they read the same as the list in
    §20 rather than as an absolute path from somebody's machine.
    """
    out = []
    for name in STAMPED_DIRS:
        directory = repo / "docs" / name
        if not directory.is_dir():
            continue
        out.extend(path for path in sorted(directory.rglob("*.md"))
                   if str(path.relative_to(repo)) not in EXEMPT)
    return out


def check_set(repo: Path) -> list:
    """§20's thirteen documents are all present and not empty."""
    problems = []
    for rel, name in REQUIRED:
        path = repo / rel
        if not path.is_file():
            problems.append(Problem(rel, f"missing: §20 requires {name}"))
        elif not path.read_text(encoding="utf-8").strip():
            problems.append(Problem(rel, f"empty: {name} is a stub"))
    return problems


def check_stamps(repo: Path, version: str) -> list:
    """Every stamped page says which version it was last checked against."""
    problems = []
    for path in stamped_files(repo):
        rel = str(path.relative_to(repo))
        text = path.read_text(encoding="utf-8")
        match = STAMP.search(text)
        if match is None:
            problems.append(Problem(
                rel, "no 'last verified on version' line (§20). Add "
                     "*Verificado en la versión X.Y.Z.* or *Last verified on "
                     "version X.Y.Z.* under the title"))
            continue
        found = match.group("version")
        if found != version:
            # A warning, not a failure. A version bump should not turn the
            # whole doc set red — it should produce a list of pages somebody
            # has to re-read, which is exactly what this prints.
            problems.append(Problem(
                rel, f"last verified against {found}, current version is "
                     f"{version} — re-check it and update the line",
                fatal=False))
    return problems


def check_links(repo: Path) -> list:
    """Every relative link in the doc set points at something real."""
    problems = []
    pages = sorted((repo / "docs").rglob("*.md"))
    for path in pages:
        rel = str(path.relative_to(repo))
        for match in LINK.finditer(path.read_text(encoding="utf-8")):
            target = match.group("target")
            if target.startswith(("http://", "https://", "mailto:", "#")):
                continue
            # Strip an anchor: the file has to exist, the heading is not
            # something this can check without parsing every document.
            target = target.split("#", 1)[0]
            if not target:
                continue
            resolved = (path.parent / target).resolve()
            if not resolved.exists():
                problems.append(Problem(
                    rel, f"broken link: {match.group('target')}"))
    return problems


def check(repo: Path, version: str) -> list:
    return check_set(repo) + check_stamps(repo, version) + check_links(repo)


def main(argv: list[str]) -> int:
    repo = Path(argv[0] if argv else ".").resolve()
    version_file = repo / "VERSION"
    if not version_file.is_file():
        print(f"docs-lint: no VERSION file at {version_file}", file=sys.stderr)
        return 2
    version = version_file.read_text(encoding="utf-8").strip()

    problems = check(repo, version)
    fatal = [p for p in problems if p.fatal]
    warnings = [p for p in problems if not p.fatal]

    for problem in problems:
        print(problem, file=sys.stderr if problem.fatal else sys.stdout)

    pages = len(stamped_files(repo))
    if fatal:
        print(f"\ndocs-lint: FAIL — {len(fatal)} problem(s) across "
              f"{pages} page(s)", file=sys.stderr)
        return 1
    if warnings:
        print(f"\ndocs-lint: OK with {len(warnings)} page(s) to re-check "
              f"against {version} ({pages} page(s), §20 set complete)")
        return 0
    print(f"docs-lint: OK — {pages} page(s), all verified against {version}, "
          f"§20 set complete, every link resolves")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
