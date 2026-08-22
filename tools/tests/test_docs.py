"""The §20 documentation set, and the Help Center built from it.

§20 makes documentation a shipped product with one rule that is easy to write
and easy to forget: each doc says when it was last verified. Two things are
tested here — that the rule is enforced rather than hoped for, and that the
markdown really does turn into an offline manual, because a Help Center that
silently loses half a page is worse than one that fails to build.
"""
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]
REPO = TOOLS.parent
sys.path.insert(0, str(TOOLS))

from castalia_qa import docs_lint  # noqa: E402

sys.path.insert(0, str(TOOLS))
import help_build  # noqa: E402


class TheSetExistsTest(unittest.TestCase):
    def test_every_document_the_bible_names_is_here(self):
        self.assertEqual(docs_lint.check_set(REPO), [])

    def test_the_required_list_matches_section_20s_table(self):
        # §20 is a table of thirteen documents. If a row is added there and
        # not here, the gate would pass on a doc set with a hole in it.
        bible = (REPO / "docs" / "PROJECT_BIBLE.md").read_text("utf-8")
        table = bible.split("## 20. Documentation Set")[1].split("## 21.")[0]
        rows = [ln for ln in table.splitlines()
                if ln.startswith("| **") and "|" in ln[3:]]
        self.assertEqual(len(rows), 13, "§20's table changed size")
        # The Bible lists thirteen; REQUIRED carries those plus the Bible
        # itself, which §20's own table also names.
        self.assertEqual(len(docs_lint.REQUIRED), 13)

    def test_the_repo_passes_its_own_gate(self):
        problems = [p for p in docs_lint.check(REPO, _version()) if p.fatal]
        self.assertEqual([str(p) for p in problems], [])


def _version():
    return (REPO / "VERSION").read_text(encoding="utf-8").strip()


class TheStampRuleTest(unittest.TestCase):
    """§20: "each doc lists its 'last verified on version'"."""

    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp, True)
        (self.tmp / "VERSION").write_text("1.2.3\n")
        self.docs = self.tmp / "docs" / "user"
        self.docs.mkdir(parents=True)

    def write(self, text):
        (self.docs / "README.md").write_text(text, encoding="utf-8")

    def test_a_page_with_no_stamp_fails(self):
        self.write("# Manual\n\nTexto.\n")
        problems = docs_lint.check_stamps(self.tmp, "1.2.3")
        self.assertTrue(problems[0].fatal)
        self.assertIn("no 'last verified on version' line",
                      problems[0].message)

    def test_both_languages_are_accepted(self):
        for stamp in ("*Verificado en la versión 1.2.3.*",
                      "*Last verified on version 1.2.3.*"):
            self.write(f"# T\n\n{stamp}\n\nTexto.\n")
            self.assertEqual(docs_lint.check_stamps(self.tmp, "1.2.3"), [],
                             stamp)

    def test_a_stale_stamp_warns_but_does_not_fail(self):
        # A version bump must not turn the whole doc set red. It should
        # produce a list of pages somebody has to re-read — which is a
        # different thing, and a useful one.
        self.write("# T\n\n*Last verified on version 1.0.0.*\n")
        problems = docs_lint.check_stamps(self.tmp, "1.2.3")
        self.assertEqual(len(problems), 1)
        self.assertFalse(problems[0].fatal)
        self.assertIn("re-check it", problems[0].message)

    def test_a_two_part_version_is_a_version(self):
        self.write("# T\n\n*Last verified on version 1.2.*\n")
        self.assertEqual(docs_lint.check_stamps(self.tmp, "1.2"), [])

    def test_exemptions_are_an_explicit_list_not_a_pattern(self):
        # The value of the rule is that it is hard to opt out of, so an
        # exemption has to be a line somebody wrote on purpose.
        self.assertIsInstance(docs_lint.EXEMPT, set)
        self.assertEqual(docs_lint.EXEMPT, {"docs/releases/TEMPLATE.md"})


class LinksResolveTest(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp, True)
        self.docs = self.tmp / "docs" / "user"
        self.docs.mkdir(parents=True)

    def test_a_broken_relative_link_is_caught(self):
        (self.docs / "README.md").write_text(
            "# T\n\nVer [esto](no-existe.md).\n", encoding="utf-8")
        problems = docs_lint.check_links(self.tmp)
        self.assertEqual(len(problems), 1)
        self.assertIn("broken link: no-existe.md", problems[0].message)

    def test_an_anchor_only_link_is_not_a_file(self):
        (self.docs / "README.md").write_text(
            "# T\n\nVer [esto](#seccion).\n", encoding="utf-8")
        self.assertEqual(docs_lint.check_links(self.tmp), [])

    def test_an_external_link_is_not_checked(self):
        (self.docs / "README.md").write_text(
            "# T\n\n[web](https://example.invalid/x).\n", encoding="utf-8")
        self.assertEqual(docs_lint.check_links(self.tmp), [])

    def test_a_link_with_an_anchor_checks_only_the_file(self):
        (self.docs / "otra.md").write_text("# O\n", encoding="utf-8")
        (self.docs / "README.md").write_text(
            "# T\n\n[o](otra.md#parte).\n", encoding="utf-8")
        self.assertEqual(docs_lint.check_links(self.tmp), [])


class HelpRenderTest(unittest.TestCase):
    """The markdown subset the doc set actually uses."""

    def test_headings_paragraphs_and_emphasis(self):
        out = help_build.render("# T\n\nUn **texto** con *énfasis*.\n")
        self.assertIn("<h1>T</h1>", out)
        self.assertIn("<b>texto</b>", out)
        self.assertIn("<i>énfasis</i>", out)

    def test_a_table_becomes_a_table(self):
        out = help_build.render("| A | B |\n|---|---|\n| 1 | 2 |\n")
        self.assertIn("<th>A</th>", out)
        self.assertIn("<td>2</td>", out)

    def test_lists_ordered_and_not(self):
        self.assertIn("<ul>", help_build.render("- uno\n- dos\n"))
        self.assertIn("<ol>", help_build.render("1. uno\n2. dos\n"))

    def test_a_fenced_block_keeps_its_contents_verbatim(self):
        out = help_build.render("```sh\nsudo dd if=x of=/dev/sdX\n```\n")
        self.assertIn("<pre><code>sudo dd if=x of=/dev/sdX", out)

    def test_html_inside_a_code_span_stays_an_example(self):
        # Escaping after extracting the span, not before: otherwise the
        # example in the theming guide becomes a tag.
        out = help_build.render("Usa `<b>negrita</b>` aquí.\n")
        self.assertIn("<code>&lt;b&gt;negrita&lt;/b&gt;</code>", out)

    def test_markdown_links_are_rewritten_to_html(self):
        out = help_build.render("[a](../recovery/README.md#x)\n")
        self.assertIn('href="../recovery/README.html#x"', out)

    def test_an_html_comment_is_not_shown_to_the_user(self):
        out = help_build.render("<!-- nota interna -->\n\nTexto.\n")
        self.assertNotIn("nota interna", out)

    def test_the_verified_line_gets_its_own_class(self):
        out = help_build.render("# T\n\n*Verificado en la versión 1.0.*\n")
        self.assertIn('<em class="stamp">', out)

    def test_nothing_is_silently_dropped(self):
        # A renderer that discards what it does not recognise is how a
        # warning goes missing from a manual. Anything unrecognised has to
        # come out the other side as text.
        out = help_build.render("Una línea rara :: con símbolos ~~raros~~\n")
        self.assertIn("con símbolos", out)


class HelpBuildTest(unittest.TestCase):
    def setUp(self):
        self.out = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.out, True)

    def test_the_real_doc_tree_builds(self):
        stats = help_build.build(REPO / "docs", self.out)
        self.assertGreaterEqual(stats["pages"], 12)
        self.assertTrue((self.out / "index.html").is_file())
        self.assertTrue((self.out / "search-index.json").is_file())

    def test_every_user_facing_section_reaches_the_index(self):
        help_build.build(REPO / "docs", self.out)
        index = (self.out / "index.html").read_text(encoding="utf-8")
        for _name, label in help_build.SECTIONS:
            self.assertIn(label, index, label)

    def test_the_front_page_of_a_section_is_listed_first(self):
        # Sorting by filename would bury "Manual de usuario" under
        # "Las aplicaciones".
        import json

        help_build.build(REPO / "docs", self.out)
        index = json.loads(
            (self.out / "search-index.json").read_text(encoding="utf-8"))
        user = next(s for s in index if s["id"] == "user")
        self.assertTrue(user["pages"][0]["href"].endswith("README.html"))

    def test_it_carries_no_network_dependency(self):
        # A help page that fetches something is a help page that does not
        # open when the network is what broke.
        help_build.build(REPO / "docs", self.out)
        for page in self.out.rglob("*.html"):
            text = page.read_text(encoding="utf-8")
            for forbidden in ("<script", "http://fonts", "https://fonts",
                              "cdn.", "<link rel=\"stylesheet\" href=\"http"):
                self.assertNotIn(forbidden, text, f"{page.name}: {forbidden}")

    def test_the_non_affiliation_notice_is_on_every_page(self):
        # §3: it goes where a reader is, not only in the legal document.
        help_build.build(REPO / "docs", self.out)
        for page in self.out.rglob("*.html"):
            self.assertIn("no\nestá afiliado a Microsoft",
                          page.read_text(encoding="utf-8"), page.name)

    def test_check_mode_writes_nothing(self):
        before = set(self.out.rglob("*"))
        rc = help_build.main(["--docs", str(REPO / "docs"), "--check"])
        self.assertEqual(rc, 0)
        self.assertEqual(set(self.out.rglob("*")), before)


class ManualLauncherTest(unittest.TestCase):
    LAUNCHER = REPO / "shell" / "session" / "castalia-manual"

    def empty_path(self):
        """A PATH with no browser on it — the FLOOR-image case.

        An empty directory rather than a nonexistent one, so `sh` itself is
        still reachable by absolute path and what is being tested is the
        launcher's fallback rather than the test harness failing to start.
        """
        empty = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, empty, True)
        return empty

    def test_it_is_posix_and_executable(self):
        self.assertTrue(self.LAUNCHER.stat().st_mode & 0o111)
        proc = subprocess.run(["sh", "-n", str(self.LAUNCHER)],
                              capture_output=True, text=True)
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertNotIn("[[ ", self.LAUNCHER.read_text(encoding="utf-8"))

    def test_it_says_so_when_no_manual_is_installed(self):
        proc = subprocess.run(
            ["sh", str(self.LAUNCHER)],
            env={"CASTALIA_HELP": "/nonexistent", "PATH": "/usr/bin:/bin"},
            capture_output=True, text=True)
        self.assertEqual(proc.returncode, 1)
        self.assertIn("no hay manual instalado", proc.stderr)

    def test_an_unknown_topic_falls_back_to_the_index_and_says_so(self):
        out = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, out, True)
        help_build.build(REPO / "docs", out)
        proc = subprocess.run(
            ["/bin/sh", str(self.LAUNCHER), "no-existe"],
            env={"CASTALIA_HELP": str(out), "PATH": str(self.empty_path())},
            capture_output=True, text=True)
        self.assertIn("no encuentro", proc.stderr)
        # No browser on PATH: it must print the path rather than fail mutely.
        self.assertIn("el manual está en", proc.stderr)

    def test_with_no_browser_it_prints_the_path(self):
        out = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, out, True)
        help_build.build(REPO / "docs", out)
        proc = subprocess.run(
            ["/bin/sh", str(self.LAUNCHER), "recovery"],
            env={"CASTALIA_HELP": str(out), "PATH": str(self.empty_path())},
            capture_output=True, text=True)
        self.assertIn("recovery/README.html", proc.stderr)


if __name__ == "__main__":
    unittest.main()
