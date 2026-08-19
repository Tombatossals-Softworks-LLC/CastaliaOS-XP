# Castalia OS — Documentation

The **[Project Bible](PROJECT_BIBLE.md)** is the single source of truth for the
design and roadmap.

Documentation is authored here in Markdown and the on-machine **Help Center**
(a first-party app) is generated from this tree at ISO-build time, so the manual
is always available offline. See
[`PROJECT_BIBLE.md` §20](PROJECT_BIBLE.md#20-documentation-set).

## Planned documentation set (§20)

| Doc | Path (planned) | Audience |
|-----|----------------|----------|
| Project Bible | `PROJECT_BIBLE.md` | Engineers/contributors (this repo) |
| User Manual | `user/` | End users |
| Install Guide | `install/` | New installers |
| Recovery Guide | `recovery/` | Users in trouble |
| Hardware Compatibility Guide | generated from QA (§19) | Buyers/testers |
| Developer Guide | `dev/` | Contributors |
| Theming Guide | `theming/` | Artists/themers |
| Packaging Guide | `packaging/` | Packagers |
| Wine Compatibility Guide | `wine/` | Power users |
| Legal Notice | `../legal/NOTICE.md` | Everyone |
| Release Notes / Known Issues | `releases/` | Each release |
| Troubleshooting Guide | `troubleshooting/` | Users |

Directories beyond the bible are created as their phase arrives (§18); do not
stub empty docs ahead of content.
