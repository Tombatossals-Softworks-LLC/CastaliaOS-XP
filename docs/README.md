# Castalia OS — Documentation

The **[Project Bible](PROJECT_BIBLE.md)** is the single source of truth for the
design and roadmap. Everything else here is a shipped product: the on-machine
**Help Center** is generated from this tree at ISO-build time
(`tools/help_build.py`), so the manual is on the machine whether or not the
machine has internet. That is P5/P6, and §20.

## The documentation set (§20)

| Doc | Path | Audience | Language |
|-----|------|----------|----------|
| **Project Bible** | [`PROJECT_BIBLE.md`](PROJECT_BIBLE.md) | engineers/contributors | English |
| **User Manual** | [`user/`](user/README.md) | end users | Spanish |
| **Install Guide** | [`install/`](install/README.md) | new installers | Spanish |
| **Recovery Guide** | [`recovery/`](recovery/README.md) | users in trouble | Spanish |
| **Troubleshooting Guide** | [`troubleshooting/`](troubleshooting/README.md) | users | Spanish |
| **Wine Compatibility Guide** | [`wine/`](wine/README.md) | power users | Spanish |
| **Legal Notice** | [`legal/`](legal/README.md) · [`../legal/NOTICE.md`](../legal/NOTICE.md) | everyone | Spanish / English |
| **Hardware Compatibility Guide** | [`hardware/`](hardware/README.md) | buyers/testers | English |
| **Developer Guide** | [`dev/`](dev/README.md) | contributors | English |
| **Theming Guide** | [`theming/`](theming/README.md) | artists/themers | English |
| **Packaging Guide** | [`packaging/`](packaging/README.md) | packagers | English |
| **Release Notes template** | [`releases/TEMPLATE.md`](releases/TEMPLATE.md) | each release | English |
| **Known Issues** | [`releases/KNOWN_ISSUES.md`](releases/KNOWN_ISSUES.md) | each release | English |
| Releasing | [`RELEASING.md`](RELEASING.md) | maintainers | English |
| Evidence | [`evidence/`](evidence/) | anyone checking a claim | mixed |

### Why two languages

The user-facing docs are in **Spanish** because they become the Help Center on
a Spanish desktop, and a manual a user cannot read is not a manual. The
contributor-facing docs are in **English**, matching the Bible and the code
comments, because that is the language the codebase is written in.

## Rules

**Every document carries the version it was last checked against.** The line
is `*Verificado en la versión X.Y.Z.*` or `*Last verified on version X.Y.Z.*`,
right under the title, and `castalia_qa.docs_lint` fails the build when it is
missing or stale. Documentation that quietly describes a release ago is worse
than documentation that admits it — §20's own "last verified on version" rule,
enforced rather than remembered.

**Describe what exists.** Not what is planned, not what is nearly done. A
manual entry for a feature that is not there costs a user an afternoon.
Anything absent belongs in [`releases/KNOWN_ISSUES.md`](releases/KNOWN_ISSUES.md),
where absence is the subject rather than a surprise.

**Screenshots are of Castalia** (§3). Never of the product Castalia resembles.

## Checking the docs

```sh
PYTHONPATH=tools python3 -m castalia_qa.docs_lint .     # the gate CI runs
PYTHONPATH=tools python3 tools/help_build.py --out /tmp/help   # the Help Center
```
