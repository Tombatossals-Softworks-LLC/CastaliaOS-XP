"""Localisation is only real when it ships (Bible §7.13).

Four ways a translated desktop silently goes back to being a Spanish one, and
none of them raises an error at runtime — Qt falls back to the source string
and paints a half-Spanish interface with a straight face:

  * a language declared in Locale.cpp with no `i18n/castalia_<code>.ts`;
  * a catalogue with untranslated messages in it;
  * a translated string whose `%1` placeholders do not match the source, which
    turns into a message with a hole in it;
  * a catalogue nobody installs, because the packaging never learned about it.

Each of those is a test below. The one thing this file does NOT check is
whether the translations are any *good* — that needs a human who speaks the
language, and pretending otherwise would be worse than not checking at all.
"""
import re
import subprocess
import sys
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
LOCALE_CPP = REPO / "shell" / "libcastalia-ui" / "Locale.cpp"
I18N = REPO / "i18n"
BUILDER = REPO / "tools" / "i18n_build.py"
MKDEB = REPO / "packages" / "mkdeb.sh"
HOOK = REPO / "build" / "hooks" / "desktop-amd64.sh"

#: The language Castalia is *written* in. It has no catalogue by design.
SOURCE_LANG = "es"


def read(path):
    return path.read_text(encoding="utf-8")


def declared_languages():
    """The codes in kLanguages[] in Locale.cpp — the shipping list."""
    table = read(LOCALE_CPP).split("kLanguages[] = {", 1)[1].split("};", 1)[0]
    return re.findall(r'\{"([a-z]{2})"', table)


def translated_languages():
    return [c for c in declared_languages() if c != SOURCE_LANG]


def messages(lang):
    """(context, source, translation, is_finished) for a catalogue."""
    root = ET.parse(I18N / f"castalia_{lang}.ts").getroot()
    out = []
    for context in root.findall("context"):
        name = (context.findtext("name") or "").strip()
        for message in context.findall("message"):
            tr = message.find("translation")
            text = (tr.text or "") if tr is not None else ""
            finished = tr is not None and tr.get("type") != "unfinished" \
                and text.strip() != ""
            out.append((name, message.findtext("source") or "", text,
                        finished))
    return out


class CatalogueTest(unittest.TestCase):
    def test_source_language_has_no_catalogue(self):
        """castalia_es.qm would be an identity map — a file that can only
        ever drift away from the source it copies."""
        self.assertIn(SOURCE_LANG, declared_languages(),
                      "Spanish is the source language and must be offered")
        self.assertFalse(
            (I18N / f"castalia_{SOURCE_LANG}.ts").exists(),
            "the source language must not have a catalogue")

    def test_every_declared_language_has_a_catalogue(self):
        for lang in translated_languages():
            with self.subTest(lang=lang):
                self.assertTrue(
                    (I18N / f"castalia_{lang}.ts").is_file(),
                    f"Locale.cpp offers {lang} but i18n/castalia_{lang}.ts "
                    f"does not exist — the picker would offer a language "
                    f"that changes nothing")

    def test_no_untranslated_strings(self):
        for lang in translated_languages():
            with self.subTest(lang=lang):
                missing = [f"{c}: {s}" for c, s, _, ok in messages(lang)
                           if not ok]
                self.assertFalse(
                    missing,
                    f"{lang}: {len(missing)} untranslated "
                    f"(run tools/i18n_build.py --check): {missing[:5]}")

    def test_placeholders_survive_translation(self):
        """`%1` is not decoration: a translation that drops one renders a
        sentence with a hole where the network name should be."""
        for lang in translated_languages():
            for context, source, text, ok in messages(lang):
                if not ok:
                    continue
                want = set(re.findall(r"%\d", source))
                got = set(re.findall(r"%\d", text))
                with self.subTest(lang=lang, context=context, source=source):
                    self.assertEqual(
                        want, got,
                        f"placeholder mismatch in {context}: "
                        f"{source!r} -> {text!r}")

    def test_accelerators_survive_translation(self):
        """A menu that loses its '&' loses its keyboard access."""
        for lang in translated_languages():
            for context, source, text, ok in messages(lang):
                if not ok or "&" not in source:
                    continue
                with self.subTest(lang=lang, source=source):
                    self.assertIn(
                        "&", text,
                        f"{context}: {source!r} lost its accelerator")

    def test_roster_is_fully_translated(self):
        """The app names are the first thing anyone reads. If a language
        ships at all, the launch menu must be complete in it."""
        for lang in translated_languages():
            roster = [(s, ok) for c, s, _, ok in messages(lang)
                      if c == "AppRoster"]
            with self.subTest(lang=lang):
                self.assertGreaterEqual(
                    len(roster), 40,
                    "the roster lost most of its strings from the catalogue")
                self.assertFalse([s for s, ok in roster if not ok],
                                 f"{lang}: untranslated app names")


class ToolchainTest(unittest.TestCase):
    def test_builder_check_passes(self):
        """The same command CI and a developer run, run here."""
        r = subprocess.run([sys.executable, str(BUILDER), "--check"],
                           cwd=REPO, capture_output=True, text=True)
        self.assertEqual(r.returncode, 0,
                         f"tools/i18n_build.py --check failed:\n"
                         f"{r.stdout}{r.stderr}")

    def test_builder_and_locale_agree_on_the_language_list(self):
        listed = re.search(r"^LANGUAGES = \((.*?)\)", read(BUILDER),
                           re.M | re.S).group(1)
        self.assertEqual(sorted(re.findall(r'"([a-z]{2})"', listed)),
                         sorted(translated_languages()),
                         "tools/i18n_build.py and Locale.cpp disagree about "
                         "which languages ship")


class PackagingTest(unittest.TestCase):
    """A catalogue that is not installed is a catalogue that does nothing."""

    def test_deb_installs_the_catalogues(self):
        text = read(MKDEB)
        self.assertIn("$SHARE/i18n/", text,
                      "packages/mkdeb.sh does not install any .qm")
        self.assertIn("i18n_build.py", text,
                      "mkdeb.sh should name the command that builds them")

    def test_iso_hook_builds_and_installs_the_catalogues(self):
        text = read(HOOK)
        self.assertIn("i18n_build.py release", text,
                      "the ISO hook never compiles the catalogues")
        self.assertIn("$SHARE/i18n/", text,
                      "the ISO hook never installs the catalogues")
        self.assertIn("qttools5-dev-tools", text,
                      "lrelease is not installed in the chroot, so the "
                      "release step above would fail the ISO build")

    def test_iso_stages_the_translation_sources(self):
        """The hook compiles .ts inside the chroot, so the .ts have to be
        staged there in the first place — the exact class of omission that
        shipped an ISO with no /opt/castalia once already."""
        checked = 0
        for profile in sorted((REPO / "build" / "profiles").glob("*.conf")):
            src = re.search(r'^SRC_DIRS="(.*?)"', read(profile), re.M)
            if src is None:
                continue     # a profile that does not build the shell at all
            checked += 1
            with self.subTest(profile=profile.name):
                self.assertIn("i18n", src.group(1).split(),
                              f"{profile.name} does not stage i18n/ into the "
                              f"chroot")
        self.assertGreater(checked, 0, "no profile stages source any more — "
                                       "this test stopped testing anything")


class WiringTest(unittest.TestCase):
    """Qt cannot retranslate a widget that already exists, so the loader has
    to run before any of them are built — in every binary, not most."""

    def test_every_gui_entry_point_installs_the_translator(self):
        offenders = []
        for main in sorted(REPO.glob("apps/*/src/main.cpp")) + \
                sorted(REPO.glob("shell/*/src/main.cpp")):
            text = read(main)
            if "QApplication" not in text:
                continue            # not a GUI entry point
            # Either directly, or through applyTheme(), which calls it.
            if "applyConfigured" in text or "applyTheme" in text:
                continue
            offenders.append(str(main.relative_to(REPO)))
        self.assertFalse(
            offenders,
            f"these binaries never install a translator, so they stay "
            f"Spanish whatever the user chose: {offenders}")

    def test_apply_theme_loads_the_language(self):
        theme = read(REPO / "shell" / "libcastalia-ui" / "Theme.cpp")
        self.assertIn("locale::applyConfigured", theme,
                      "applyTheme() no longer applies the language — every "
                      "app that relies on it silently reverts to Spanish")

    def test_control_center_can_change_the_language(self):
        cc = read(REPO / "apps" / "control-center" / "src" /
                  "ControlCenter.cpp")
        self.assertIn("persistLanguage", cc,
                      "the Control Center cannot change the language")
        self.assertIn("locale.conf", cc,
                      "the Control Center writes the choice somewhere other "
                      "than the file castalia::locale reads")


if __name__ == "__main__":
    unittest.main()
