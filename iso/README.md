# iso/ — ISO layout & boot menus

Templates and assets for the hybrid live+install ISO (USB *and* CD/DVD bootable
via isolinux El-Torito). See
[`docs/PROJECT_BIBLE.md` §14.1](../docs/PROJECT_BIBLE.md#14-installer-and-first-boot-experience)
and [§6.2](../docs/PROJECT_BIBLE.md#6-system-architecture).

## Contents

| Path | Status | Purpose |
|------|--------|---------|
| `isolinux/isolinux.cfg.in` | ✅ | The vesamenu boot menu **build/mkiso.sh renders** — live session (default), safe graphics; Castalia colors, azure selection bar. Placeholders: `@TITLE@`, `@APPEND@`, `@INSTALL@` |
| `isolinux/entries-install.cfg` | ✅ | The install entries, added for editions whose profile sets `INSTALLER="yes"`; they pass `castalia.installer=gui\|text` to the live session |
| `grub/grub.cfg` | ✅ | Installed-system menu: Castalia Classic, Safe Mode, Recovery, Memtest (§6.2); installer templates `@ROOT_UUID@`; update pipeline appends the N-1 kernel entry |
| `grub/theme/theme.txt` | ✅ | GRUB gfxmenu theme: wordmark, styled boot menu, azure timeout bar; `.pf2` fonts baked at ISO build |
| `boot-bg/splash.png` | ✅ | 640×480 menu background, baked deterministically by `tools/bootbg_gen.py` (pure stdlib PNG) |
| `menu/` | planned | Extra menu fragments per edition |

The ISO is built by `build/mkiso.sh` per edition profile. Boot menus always
offer a safe-graphics live path for old/broken GPUs.

**The template here is what ships.** It used to be a design document mkiso.sh
ignored while writing its own single-entry menu inline, which is how ISOs went
out with no installer entry at all. `tools/tests/test_iso_boot.py` now fails
the build if mkiso.sh stops rendering this file, if the live entry is not
first, if an entry boots something the build never stages, or if the menu
offers a `castalia.installer=` mode the live session cannot handle.
