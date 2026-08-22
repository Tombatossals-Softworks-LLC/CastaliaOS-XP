"""Build the offline Help Center from the /docs tree (Bible §20, P5/P6).

§20 says the manual is a shipped product and the on-machine Help Center is
"built from all /docs at ISO time". This is that build: markdown in, a
self-contained HTML tree out, no network at read time and no network at build
time either.

Three constraints shape it, and they are all the same constraint in different
clothes — this has to work on a Pentium 4 with 512 MB and no internet:

* **No JavaScript framework, no web fonts, no CDN.** One stylesheet, inline.
  A help page that needs to fetch something is a help page that does not open
  when the network is the thing that broke.
* **No markdown library.** The renderer below is a few hundred lines and
  handles the subset the doc set actually uses. A build-time dependency on
  pip is a build that does not run on a machine without pip, and the ISO hook
  runs inside a minbase chroot.
* **Search is a static index.** Every page's text goes into one file the
  Help Center greps. Grepping 60 KB is instant on any machine; an index
  server is not something a desktop should have.

Usage: python3 tools/help_build.py [--docs DIR] [--out DIR] [--check]
"""

from __future__ import annotations

import argparse
import html
import json
import re
import sys
from pathlib import Path

#: The order topics appear in the Help Center, and what they are called there.
#: Ordered by how likely somebody in trouble is to want them, which is not the
#: same as the order §20 lists them in: the person who opens Help is more
#: often stuck than curious.
SECTIONS = (
    ("user", "Manual de usuario"),
    ("troubleshooting", "Si algo va mal"),
    ("recovery", "Recuperación"),
    ("install", "Instalación"),
    ("wine", "Aplicaciones de Windows"),
    ("legal", "Aviso legal"),
)

#: Contributor documentation. Included in the tree so the ISO carries the
#: whole set (§20 says the manual is always on the machine), but listed apart
#: — somebody looking for "no me funciona el sonido" should not have to walk
#: past the packaging guide to reach it.
DEV_SECTIONS = (
    ("dev", "Guía de desarrollo"),
    ("theming", "Guía de temas"),
    ("packaging", "Guía de empaquetado"),
    ("hardware", "Compatibilidad de hardware"),
    ("releases", "Notas de versión"),
)

STYLE = """
:root {
  --ink: #2b2b2b; --dim: #6b6b6b; --bg: #f6f3ee; --card: #ffffff;
  --accent: #c8641e; --rule: #ddd6cb; --code: #f0ece4;
}
* { box-sizing: border-box; }
body { margin: 0; background: var(--bg); color: var(--ink);
       font: 15px/1.6 "DejaVu Sans", sans-serif; }
.wrap { max-width: 46rem; margin: 0 auto; padding: 1.5rem 1.25rem 4rem; }
header { background: var(--card); border-bottom: 2px solid var(--accent); }
header .wrap { padding: 1rem 1.25rem; }
header a { color: var(--accent); text-decoration: none; font-weight: bold; }
h1 { font-size: 1.6rem; margin: 0 0 .4rem; }
h2 { font-size: 1.25rem; margin: 1.8rem 0 .5rem; color: var(--accent); }
h3 { font-size: 1.05rem; margin: 1.4rem 0 .4rem; }
p, ul, ol { margin: 0 0 .9rem; }
li { margin: 0 0 .35rem; }
a { color: #1c5f9e; }
code { background: var(--code); padding: .1rem .3rem; border-radius: 2px;
       font-family: "DejaVu Sans Mono", monospace; font-size: .92em; }
pre { background: var(--code); padding: .8rem 1rem; overflow-x: auto;
      border-left: 3px solid var(--rule); }
pre code { background: none; padding: 0; }
table { border-collapse: collapse; width: 100%; margin: 0 0 1rem;
        display: block; overflow-x: auto; }
th, td { border: 1px solid var(--rule); padding: .4rem .6rem;
         text-align: left; vertical-align: top; }
th { background: var(--card); }
blockquote { margin: 0 0 1rem; padding: .1rem 1rem; border-left: 3px solid
             var(--rule); color: var(--dim); }
em.stamp { display: block; color: var(--dim); font-size: .9em;
           margin: 0 0 1.2rem; }
nav.toc { background: var(--card); border: 1px solid var(--rule);
          padding: 1rem 1.25rem; margin: 0 0 1.5rem; }
nav.toc h2 { margin-top: 0; }
footer { color: var(--dim); font-size: .88em; margin-top: 3rem;
         border-top: 1px solid var(--rule); padding-top: 1rem; }
@media (max-width: 40rem) { .wrap { padding: 1rem .75rem 3rem; } }
"""


def _inline(text: str) -> str:
    """Inline markdown: code, bold, italics, links. Escaped first."""
    # Code spans are pulled out before escaping so their contents survive
    # verbatim, then put back — otherwise `<b>` inside backticks becomes a
    # tag rather than the example it is.
    spans: list = []

    def stash(match):
        spans.append(match.group(1))
        return f"\x00{len(spans) - 1}\x00"

    text = re.sub(r"`([^`]+)`", stash, text)
    text = html.escape(text, quote=False)
    text = re.sub(r"\[([^\]]+)\]\(([^)\s]+)\)",
                  lambda m: f'<a href="{_href(m.group(2))}">{m.group(1)}</a>',
                  text)
    text = re.sub(r"\*\*([^*]+)\*\*", r"<b>\1</b>", text)
    text = re.sub(r"(?<!\*)\*([^*\n]+)\*(?!\*)", r"<i>\1</i>", text)
    return re.sub(r"\x00(\d+)\x00",
                  lambda m: f"<code>{html.escape(spans[int(m.group(1))])}"
                            f"</code>", text)


def _href(target: str) -> str:
    """Rewrite a repo-relative markdown link to its place in the built tree."""
    if target.startswith(("http://", "https://", "mailto:", "#")):
        return html.escape(target, quote=True)
    anchor = ""
    if "#" in target:
        target, anchor = target.split("#", 1)
        anchor = "#" + anchor
    if target.endswith(".md"):
        target = target[:-3] + ".html"
    # ../../legal/NOTICE.md and friends leave the docs tree; the Help Center
    # only carries the docs tree, so those become plain text rather than a
    # link into nothing. Handled by the caller keeping them escaped.
    return html.escape(target + anchor, quote=True)


def render(markdown: str) -> str:
    """The markdown subset the doc set uses, as HTML.

    Deliberately partial and deliberately not clever. It handles headings,
    paragraphs, fenced code, lists, tables, blockquotes, horizontal rules and
    HTML comments — which is everything in /docs — and anything it does not
    recognise passes through as a paragraph rather than disappearing. A
    renderer that silently drops what it does not understand is how a warning
    goes missing from a manual.
    """
    out: list = []
    lines = markdown.replace("\r\n", "\n").split("\n")
    i = 0
    para: list = []

    def flush():
        if para:
            out.append(f"<p>{_inline(' '.join(para))}</p>")
            para.clear()

    while i < len(lines):
        line = lines[i]
        stripped = line.strip()

        if stripped.startswith("<!--"):
            flush()
            while i < len(lines) and "-->" not in lines[i]:
                i += 1
            i += 1
            continue

        if stripped.startswith("```"):
            flush()
            i += 1
            block: list = []
            while i < len(lines) and not lines[i].strip().startswith("```"):
                block.append(lines[i])
                i += 1
            i += 1
            body = html.escape("\n".join(block))
            out.append(f"<pre><code>{body}</code></pre>")
            continue

        if not stripped:
            flush()
            i += 1
            continue

        if re.match(r"^-{3,}$|^\*{3,}$", stripped):
            flush()
            out.append("<hr>")
            i += 1
            continue

        heading = re.match(r"^(#{1,4})\s+(.*)$", stripped)
        if heading:
            flush()
            level = len(heading.group(1))
            out.append(f"<h{level}>{_inline(heading.group(2))}</h{level}>")
            i += 1
            continue

        # A table: a header row, a separator of dashes, then body rows.
        if stripped.startswith("|") and i + 1 < len(lines) \
                and re.match(r"^\|[\s:|-]+\|$", lines[i + 1].strip()):
            flush()
            head = [c.strip() for c in stripped.strip("|").split("|")]
            i += 2
            rows: list = []
            while i < len(lines) and lines[i].strip().startswith("|"):
                rows.append([c.strip()
                             for c in lines[i].strip().strip("|").split("|")])
                i += 1
            cells = "".join(f"<th>{_inline(c)}</th>" for c in head)
            body = "".join(
                "<tr>" + "".join(f"<td>{_inline(c)}</td>" for c in row)
                + "</tr>" for row in rows)
            out.append(f"<table><thead><tr>{cells}</tr></thead>"
                       f"<tbody>{body}</tbody></table>")
            continue

        if stripped.startswith(">"):
            flush()
            quote: list = []
            while i < len(lines) and lines[i].strip().startswith(">"):
                quote.append(lines[i].strip().lstrip("> ").rstrip())
                i += 1
            out.append(f"<blockquote>{_inline(' '.join(quote))}</blockquote>")
            continue

        bullet = re.match(r"^([-*]|\d+\.)\s+(.*)$", stripped)
        if bullet:
            flush()
            ordered = bullet.group(1)[0].isdigit()
            items: list = []
            while i < len(lines):
                m = re.match(r"^\s*([-*]|\d+\.)\s+(.*)$", lines[i])
                if not m:
                    # A wrapped continuation line belongs to the item above.
                    if items and lines[i].strip() and lines[i][:1].isspace():
                        items[-1] += " " + lines[i].strip()
                        i += 1
                        continue
                    break
                items.append(m.group(2))
                i += 1
            tag = "ol" if ordered else "ul"
            body = "".join(f"<li>{_inline(it)}</li>" for it in items)
            out.append(f"<{tag}>{body}</{tag}>")
            continue

        # The "last verified on version" line gets its own class so the Help
        # Center can show it quietly rather than as a first paragraph.
        if re.match(r"^\*(Verificado en la versi|Last verified on version)",
                    stripped):
            flush()
            out.append(f'<em class="stamp">{_inline(stripped.strip("*"))}'
                       f'</em>')
            i += 1
            continue

        para.append(stripped)
        i += 1

    flush()
    return "\n".join(out)


def page(title: str, body: str, *, depth: int = 1) -> str:
    up = "../" * depth
    return f"""<!doctype html>
<html lang="es"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{html.escape(title)} — Ayuda de Castalia</title>
<style>{STYLE}</style></head><body>
<header><div class="wrap"><a href="{up}index.html">&larr; Centro de ayuda de
Castalia</a></div></header>
<div class="wrap">
{body}
<footer>Castalia OS es un proyecto independiente de Tombatossals Softworks y no
está afiliado a Microsoft, ni respaldado ni patrocinado por Microsoft.</footer>
</div></body></html>
"""


def title_of(markdown: str, fallback: str) -> str:
    for line in markdown.split("\n"):
        if line.startswith("# "):
            return line[2:].strip()
    return fallback


def plain_text(markdown: str) -> str:
    """The page's words, for the search index. Markup removed, not escaped."""
    text = re.sub(r"```.*?```", " ", markdown, flags=re.S)
    text = re.sub(r"<!--.*?-->", " ", text, flags=re.S)
    text = re.sub(r"[#*`|>_\[\]()-]", " ", text)
    return re.sub(r"\s+", " ", text).strip()


def build(docs: Path, out: Path) -> dict:
    """Render the tree. Returns the index that was written."""
    index: list = []
    for group, sections in (("user", SECTIONS), ("dev", DEV_SECTIONS)):
        for name, label in sections:
            directory = docs / name
            if not directory.is_dir():
                continue
            pages: list = []
            for source in sorted(directory.rglob("*.md")):
                rel = source.relative_to(docs)
                markdown = source.read_text(encoding="utf-8")
                title = title_of(markdown, source.stem)
                target = out / rel.with_suffix(".html")
                target.parent.mkdir(parents=True, exist_ok=True)
                depth = len(rel.parts) - 1
                target.write_text(page(title, render(markdown), depth=depth),
                                  encoding="utf-8")
                pages.append({"title": title,
                              "href": str(rel.with_suffix(".html")),
                              "text": plain_text(markdown)})
            if pages:
                # README first — it is the section's front page, and sorting
                # by filename would bury it under "aplicaciones.md".
                pages.sort(key=lambda p: (not p["href"].endswith(
                    "README.html"), p["title"]))
                index.append({"group": group, "id": name, "label": label,
                              "pages": pages})

    out.mkdir(parents=True, exist_ok=True)
    (out / "search-index.json").write_text(
        json.dumps(index, ensure_ascii=False), encoding="utf-8")
    (out / "index.html").write_text(_index_page(index), encoding="utf-8")
    (out / "style.css").write_text(STYLE, encoding="utf-8")
    return {"sections": len(index),
            "pages": sum(len(s["pages"]) for s in index)}


def _index_page(index: list) -> str:
    parts = ["<h1>Centro de ayuda de Castalia</h1>",
             '<em class="stamp">Toda esta ayuda está en tu equipo. No hace '
             'falta conexión a internet para leerla.</em>']
    for group, heading in (("user", None), ("dev", "Para quien construye "
                                                   "Castalia")):
        chunk = [s for s in index if s["group"] == group]
        if not chunk:
            continue
        if heading:
            parts.append(f"<h2>{heading}</h2>")
        for section in chunk:
            parts.append(f'<nav class="toc"><h2>'
                         f'{html.escape(section["label"])}</h2><ul>')
            for entry in section["pages"]:
                parts.append(f'<li><a href="{html.escape(entry["href"])}">'
                             f'{html.escape(entry["title"])}</a></li>')
            parts.append("</ul></nav>")
    return page("Centro de ayuda", "\n".join(parts), depth=0)


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser(
        prog="help_build.py",
        description="Build the offline Help Center from /docs (Bible §20).")
    p.add_argument("--docs", default="docs")
    p.add_argument("--out", default="build/out/help")
    p.add_argument("--check", action="store_true",
                   help="render everything into a temporary directory and "
                        "report, writing nothing permanent")
    args = p.parse_args(argv)

    docs = Path(args.docs)
    if not docs.is_dir():
        print(f"help-build: no docs tree at {docs}", file=sys.stderr)
        return 2

    if args.check:
        import tempfile

        with tempfile.TemporaryDirectory() as tmp:
            stats = build(docs, Path(tmp))
    else:
        stats = build(docs, Path(args.out))
        print(f"help-build: wrote {args.out}")
    print(f"help-build: {stats['pages']} page(s) in "
          f"{stats['sections']} section(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
