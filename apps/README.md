# apps/ — Castalia first-party applications (Bible §9)

Each app links `shell/libcastalia-ui` and is built through the shell CMake
(`shell/CMakeLists.txt` adds this directory). See the full catalogue with
per-app scope in [`docs/PROJECT_BIBLE.md` §9](../docs/PROJECT_BIBLE.md#9-built-in-applications).

| App | Status | Notes |
|-----|--------|-------|
| `text-editor/` (Notas) | ✅ | Plain-text editor: open/save/find, word-wrap, Ln/Col + encoding status |
| `calculator/` (Calc) | ✅ | Standard arithmetic, themed keypad, keyboard entry, expression line |
| `image-viewer/` (Visor) | ✅ | View/zoom/rotate/fit images, step through a folder, dimension+zoom status |
| `wine-manager/` (castalia-wine) | ✅ | Wine prefix manager: per-app prefixes, honest compat ratings (Platino…No funciona), add/run/winetricks/remove, and the plain-language "what won't work" note (§11) |
| `control-center/` | ✅ PoC | The settings hub (§9.1, §10): category list + panels; the **Appearance** panel re-themes the whole running app live (§6.16); **About** shows the original identity + Microsoft non-affiliation notice |

Build (via the shell tree):

```sh
cmake -S shell -B build/out/shell-build -DCMAKE_BUILD_TYPE=Release
cmake --build build/out/shell-build -j4
QT_QPA_PLATFORM=offscreen \
  build/out/shell-build/apps/control-center/castalia-control-center \
  --theme classic --repo . --screenshot cc.png
```
