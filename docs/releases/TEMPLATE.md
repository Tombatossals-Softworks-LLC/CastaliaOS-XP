# Castalia OS X.Y.Z — release notes

*Released YYYY-MM-DD. Built from commit `<sha>`.*

<!--
Copy this file to docs/releases/X.Y.Z.md and fill it in. Rules that make
these notes worth reading:

  * Say what CHANGED for a person using the system, not what was committed.
    "The installer can now make room on a full disk" beats "add shrink mode".
  * Every regression goes in "Known regressions". A release note that only
    lists improvements is an advertisement, not a release note.
  * Anything a user must DO (back up first, disable Fast Startup, re-run
    something) goes at the top, under "Before you upgrade", not buried.
  * Link the guide, not the commit. Users do not read commits.
-->

## Before you upgrade

<!-- Delete if there is nothing. Do not delete if there is. -->

## New

## Changed

## Fixed

## Known regressions

<!-- Things that worked in the previous release and do not work in this one.
     If this section is empty, say "None known" — an empty heading reads as
     an oversight rather than as good news. -->

## Hardware

<!-- What was certified for this release (§19.2), and on what. "QEMU only"
     is an honest answer for a PATCH release; say it rather than omitting
     the section. -->

## Assets

| File | What |
|---|---|
| `castalia-live-desktop-amd64-X.Y.Z.iso` | the graphical live desktop, 64-bit |
| `castalia-live-amd64-X.Y.Z.iso` | the lean base image, 64-bit |
| `castalia-live-i386-X.Y.Z.iso` | the lean base image, 32-bit (FLOOR tier) |
| `castalia-desktop_X.Y.Z_amd64.deb` | the desktop as a package |
| `castalia-repo-X.Y.Z.tar.gz` | the apt overlay repository |
| `SHA256SUMS` (`.asc`) | checksums, signed when the release key was configured |

Verify before use:

```sh
sha256sum -c SHA256SUMS --ignore-missing
```

---

Castalia OS is an independent project of Tombatossals Softworks and is not
affiliated with, endorsed by, or sponsored by Microsoft.
