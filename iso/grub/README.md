# `iso/grub/` — the installed system's boot menu (Bible §6.2)

The live ISO boots through **isolinux** (`iso/isolinux/`). Everything here is
for the system *after* it is installed, where the bootloader is **GRUB2**.

| File | Where it ends up | Put there by |
|---|---|---|
| `theme/theme.txt` | `/boot/grub/themes/castalia/theme.txt` | the installer, from `/usr/share/castalia/grub/` |
| `11_castalia_safe` | `/etc/grub.d/11_castalia_safe` (mode 755) | the installer, same source |

The background next to `theme.txt` is `iso/boot-bg/splash.png`
(`tools/bootbg_gen.py`), and the `.pf2` fonts the theme names are baked with
`grub-mkfont` at install time. Both are copied in by `packages/mkdeb.sh` and
by the desktop ISO hook, so a Castalia system carries its own boot identity
whether it was installed or `apt install`-ed.

## Why there is no `grub.cfg` here

There used to be one: a static, hand-written menu with `@ROOT_UUID@`
placeholders that the installer was supposed to template. Nothing ever
templated it. It was read by exactly one thing — a unit test asserting that
the *repository copy* contained the Bible's entries — so CI stayed green on a
menu no installed machine ever saw. It was also wrong: it pointed at
`/boot/vmlinuz` under the **root** filesystem's UUID, and the installer puts
`/boot` on its own partition, where the kernel is `/vmlinuz-<version>` under
the **boot** partition. It would not have booted had it shipped.

The menu now comes from `grub-mkconfig`, which discovers the real kernel every
time it runs — so it survives a kernel update, which a hardcoded path does
not. Castalia's identity is applied through the two supported hooks:
`/etc/default/grub.d/50-castalia.cfg` (branding, theme, timeout, last-booted
memory, and `GRUB_DISABLE_OS_PROBER=false` so an existing OS gets its own
entry) written by `installer/castalia_installer/plan.py`, and the generator
above for Safe Mode.

A **drop-in**, not `/etc/default/grub` itself: grub-mkconfig sources the main
file and then every `/etc/default/grub.d/*.cfg`, so Castalia's settings win
without deleting the ones the image it was installed from had already made.
The first version wrote the main file, and the install-and-boot test caught it
taking the source image's serial console with it — a machine that came up and
could not be heard. `GRUB_CMDLINE_LINUX` is deliberately never set here: it is
where an image puts `console=`, and it belongs to whoever built that image.

Debian's own "recovery mode" entries are deliberately left enabled under
*Advanced options*: the Castalia recovery environment (§6.13) does not exist
yet, and removing the last root-shell repair path before its replacement is
built would take something away and give nothing back.
