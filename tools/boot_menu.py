#!/usr/bin/env python3
"""Render the live ISO's isolinux boot menu from the repo template.

`build/mkiso.sh` calls this once per edition (Bible §14.1). It is a separate,
tested tool rather than a shell heredoc for a reason: the menu is the only
part of the product a user meets before anything of ours is running, and the
last two bugs in it — a template nothing rendered, then a substitution done in
the wrong order — were both invisible until an ISO was built and taken apart
again.

Substitution order is the load-bearing detail: INSTALL is filled first because
the entries it splices in carry an APPEND placeholder of their own.

  usage: boot_menu.py --template FILE --out FILE --title STR --append STR
                      [--install-entries FILE]
"""
import argparse
import sys
from pathlib import Path

#: The placeholders the template may use, in the order they must be filled.
PLACEHOLDERS = ("INSTALL", "TITLE", "APPEND")


def render(template: str, title: str, append: str, install: str = "") -> str:
    """Fill the template. Returns the menu exactly as it will ship."""
    out = template
    for name, value in (("INSTALL", install), ("TITLE", title),
                        ("APPEND", append)):
        out = out.replace(f"@{name}@", value)
    return out


def unfilled(menu: str) -> list[str]:
    """Placeholders still in a rendered menu — always a bug, never cosmetic:
    isolinux would show `@APPEND@` to the user or boot without `boot=live`."""
    import re
    return sorted(set(re.findall(r"@([A-Z_]+)@", menu)))


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--template", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--title", required=True,
                    help="the edition's LABEL, shown as the menu title")
    ap.add_argument("--append", required=True,
                    help="kernel arguments shared by every entry")
    ap.add_argument("--install-entries", type=Path, default=None,
                    help="entries for an edition that ships an installer; "
                         "omitted editions simply get none")
    args = ap.parse_args(argv[1:])

    install = ""
    if args.install_entries is not None:
        install = args.install_entries.read_text(encoding="utf-8")

    menu = render(args.template.read_text(encoding="utf-8"),
                  title=args.title, append=args.append, install=install)

    left = unfilled(menu)
    if left:
        print(f"boot-menu: unsubstituted placeholder(s): {left}",
              file=sys.stderr)
        return 1

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(menu, encoding="utf-8")
    entries = sum(1 for line in menu.splitlines()
                  if line.startswith("LABEL "))
    print(f"boot-menu: wrote {args.out} ({entries} entries)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
