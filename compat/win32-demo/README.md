# compat/win32-demo — original Win32 compatibility proof

`hello.c` is an **original** Win32 GUI program (Windows API only) that we
compile with MinGW to a genuine PE `.exe`. Running it under **Wine** on
Castalia OS proves the compatibility layer works end to end — with software we
wrote ourselves, so it is entirely legally clean (no Microsoft code or assets).
See [`docs/PROJECT_BIBLE.md` §11](../../docs/PROJECT_BIBLE.md#11-compatibility-strategy).

```sh
make            # -> hello.exe (needs gcc-mingw-w64-i686)
wine hello.exe  # runs the real Win32 app under Wine
```

The compiled `hello.exe` is a build artifact (gitignored); the ISO's compat
hook builds it in-image and the live session launches it under Wine for the
Phase 6 proof.
