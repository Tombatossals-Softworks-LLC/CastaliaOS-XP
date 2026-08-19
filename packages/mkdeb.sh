#!/bin/sh
# mkdeb.sh — build the castalia-desktop .deb from a compiled shell tree
# (Bible §13, §17.2 step 1). This is the distributable form of the desktop:
# the same binaries, launchers, backends and asset tree the live-desktop ISO
# hook installs (build/hooks/desktop-amd64.sh), packaged so an existing
# Debian-family system can `apt install` Castalia and pick "Castalia Classic"
# from the greeter.
#
# Layout (mirrors the installed system, §17.2):
#   /opt/castalia/bin/*                     shell + all first-party apps
#   /usr/bin/castalia-*                     PATH symlinks + backend launchers
#   /usr/share/castalia/{themes,branding}   the runtime asset tree (= REPO)
#   /usr/share/castalia/build/out/themes    generated QSS + Openbox themercs
#   /usr/share/castalia/{installer,recovery} shared Python backends
#   /usr/share/themes/Castalia-*/openbox-3  WM decorations per theme
#   /usr/share/icons/Castalia/48x48/apps    icon family (PNG-free SVG)
#   /usr/share/xsessions/castalia.desktop   greeter session entry
#   /etc/castalia/theme.conf                default theme (conffile)
#
# Usage:
#   sh packages/mkdeb.sh [--bindir DIR] [--repo PATH] [--out DIR]
#                        [--version V] [--arch A] [--dry-run]
#
# The app list is tests/apps.manifest — the same manifest the QA suites gate
# on, so a binary cannot ship unrendered/untested. Version defaults to the
# repo-root VERSION file. Requires dpkg-deb and an exported theme tree
# (tools/theme_export.py); --dry-run prints the plan and needs neither.

set -eu

REPO=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BINDIR="$REPO/build/out/shell-build"
OUT="$REPO/build/out/deb"
VERSION=
ARCH=
DRY=0
while [ $# -gt 0 ]; do
    case "$1" in
        --bindir)  BINDIR=${2:?}; shift 2 ;;
        --repo)    REPO=$(cd "${2:?}" && pwd); shift 2 ;;
        --out)     OUT=${2:?}; shift 2 ;;
        --version) VERSION=${2:?}; shift 2 ;;
        --arch)    ARCH=${2:?}; shift 2 ;;
        --dry-run) DRY=1; shift ;;
        -h|--help) sed -n '2,30p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "mkdeb: error: unknown option: $1" >&2; exit 2 ;;
    esac
done

die() { printf 'mkdeb: error: %s\n' "$*" >&2; exit 1; }
log() { printf 'mkdeb: %s\n' "$*"; }

MANIFEST="$REPO/tests/apps.manifest"
[ -r "$MANIFEST" ] || die "no app manifest: $MANIFEST"
[ -n "$VERSION" ] || {
    [ -r "$REPO/VERSION" ] || die "no VERSION file and no --version"
    VERSION=$(tr -d ' \n' < "$REPO/VERSION")
}
case "$VERSION" in
    [0-9]*) : ;;
    *) die "version must start with a digit (Debian policy): '$VERSION'" ;;
esac
[ -n "$ARCH" ] || ARCH=$(dpkg --print-architecture 2>/dev/null || echo amd64)

PKG=castalia-desktop
DEB="$OUT/${PKG}_${VERSION}_${ARCH}.deb"
PREFIX=/opt/castalia
SHARE=/usr/share/castalia

# The full binary set: the two shell planes + every app in the manifest.
# (name:path pairs; explorer/instalador/recuperacion live in the manifest.)
bins="panel/castalia-panel desktop/castalia-desktop"
while IFS='|' read -r name bin _live _render; do
    case "$name" in ''|'#'*) continue ;; esac
    bins="$bins $bin"
done < "$MANIFEST"

if [ "$DRY" -eq 1 ]; then
    log "PLAN package: $PKG $VERSION ($ARCH) -> $DEB"
    for b in $bins; do
        log "PLAN binary: $BINDIR/$b -> $PREFIX/bin/$(basename "$b") (+ /usr/bin symlink)"
    done
    log "PLAN session: shell/session/castalia-session -> $PREFIX/bin (+ /usr/bin)"
    log "PLAN session: shell/session/castalia-open -> $PREFIX/bin (+ /usr/bin)"
    log "PLAN config: $SHARE/openbox/rc.xml (§7.7 keyboard map, via --config-file)"
    log "PLAN session: shell/session/castalia.desktop -> /usr/share/xsessions"
    log "PLAN assets: themes/ branding/ -> $SHARE"
    log "PLAN assets: generated QSS + Openbox themercs -> $SHARE/build/out/themes"
    log "PLAN assets: Openbox themes -> /usr/share/themes/Castalia-<id>"
    log "PLAN assets: icons -> /usr/share/icons/Castalia/48x48/apps"
    log "PLAN backends: installer + recovery Python -> $SHARE (+ /usr/bin launchers)"
    log "PLAN config: /etc/castalia/theme.conf (conffile, default human)"
    log "PLAN config: /etc/lightdm/lightdm-gtk-greeter.conf (conffile, Human dawn greeter)"
    log "PLAN control: Depends on Qt5 widgets/svg runtime + python3"
    log "plan complete — run without --dry-run after building the shell"
    exit 0
fi

command -v dpkg-deb >/dev/null || die "missing dpkg-deb (install dpkg-dev)"
THEMES_OUT="$REPO/build/out/themes"
[ -d "$THEMES_OUT" ] || die "no generated themes at $THEMES_OUT — run \
PYTHONPATH=tools python3 tools/theme_export.py first"

STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT INT TERM

log "staging $PKG $VERSION ($ARCH)"

# binaries + PATH symlinks
for b in $bins; do
    src="$BINDIR/$b"
    name=$(basename "$b")
    [ -x "$src" ] || die "missing binary: $src (build the shell first)"
    install -Dm755 "$src" "$STAGE$PREFIX/bin/$name"
    mkdir -p "$STAGE/usr/bin"
    ln -s "$PREFIX/bin/$name" "$STAGE/usr/bin/$name"
done

# the session manager + greeter entry
install -Dm755 "$REPO/shell/session/castalia-session" \
    "$STAGE$PREFIX/bin/castalia-session"
ln -s "$PREFIX/bin/castalia-session" "$STAGE/usr/bin/castalia-session"
install -Dm755 "$REPO/shell/session/castalia-open" \
    "$STAGE$PREFIX/bin/castalia-open"
ln -s "$PREFIX/bin/castalia-open" "$STAGE/usr/bin/castalia-open"
install -Dm644 "$REPO/shell/session/castalia.desktop" \
    "$STAGE/usr/share/xsessions/castalia.desktop"
# a greeter launches the session with no CASTALIA_REPO in the environment;
# the session then falls back to $PREFIX/share/castalia — point it at the
# packaged asset tree.
mkdir -p "$STAGE$PREFIX/share"
ln -s "$SHARE" "$STAGE$PREFIX/share/castalia"

# runtime asset tree — the installed system's CASTALIA_REPO
mkdir -p "$STAGE$SHARE"
cp -a "$REPO/themes" "$STAGE$SHARE/"
cp -a "$REPO/branding" "$STAGE$SHARE/"
mkdir -p "$STAGE$SHARE/build/out"
cp -a "$THEMES_OUT" "$STAGE$SHARE/build/out/themes"

# Translation catalogues (tools/i18n_build.py release). Spanish is the source
# language and has no .qm, so an empty directory here is the correct state for
# a Spanish-only build — but a language declared in Locale.cpp with no
# catalogue installed is an interface that silently stays Spanish, which is
# why the missing file is an error rather than a shrug.
I18N_OUT="$REPO/build/out/i18n"
for lang in $(python3 - <<'PY'
import re, pathlib
src = pathlib.Path("shell/libcastalia-ui/Locale.cpp").read_text("utf-8")
table = src.split("kLanguages[] = {", 1)[1].split("};", 1)[0]
print(" ".join(c for c in re.findall(r'\{"([a-z]{2})"', table) if c != "es"))
PY
); do
    qm="$I18N_OUT/castalia_$lang.qm"
    [ -f "$qm" ] || die "missing translation $qm — run \
python3 tools/i18n_build.py release"
    install -Dm644 "$qm" "$STAGE$SHARE/i18n/castalia_$lang.qm"
done

# Openbox decorations per theme + the icon family
for tdir in "$THEMES_OUT"/*/; do
    id=$(basename "$tdir")
    [ -f "$tdir/openbox-3/themerc" ] || continue
    install -Dm644 "$tdir/openbox-3/themerc" \
        "$STAGE/usr/share/themes/Castalia-$id/openbox-3/themerc"
done
mkdir -p "$STAGE/usr/share/icons/Castalia/48x48/apps"
cp "$REPO/themes/icons/48/"*.svg \
    "$STAGE/usr/share/icons/Castalia/48x48/apps/" 2>/dev/null || :

# The Xcursor pointer theme (tools/cursor_gen.py). Generated art, so it is
# packaged when it has been built; the cursor generator needs ImageMagick,
# which not every build host has, and the desktop is perfectly usable on the
# stock pointers without it.
CURSORS_OUT="$REPO/build/out/cursors/Castalia-Human"
if [ -d "$CURSORS_OUT/cursors" ]; then
    mkdir -p "$STAGE/usr/share/icons/Castalia-Human"
    cp -a "$CURSORS_OUT/cursors" "$STAGE/usr/share/icons/Castalia-Human/"
    install -Dm644 "$CURSORS_OUT/index.theme" \
        "$STAGE/usr/share/icons/Castalia-Human/index.theme"
    # X resolves the "default" cursor theme through this index.
    mkdir -p "$STAGE/usr/share/icons/default"
    printf '[Icon Theme]\nName=Default\nComment=Default cursor theme\nInherits=Castalia-Human\n' \
        > "$STAGE/usr/share/icons/default/index.theme"
    log "cursors: Castalia-Human -> /usr/share/icons (default theme)"
else
    log "cursors: skipped (no $CURSORS_OUT — run tools/cursor_gen.py)"
fi

# shared Python backends + their console launchers (§14.5, §9)
mkdir -p "$STAGE$SHARE/installer" "$STAGE$SHARE/recovery"
cp -a "$REPO/installer/castalia_installer" "$STAGE$SHARE/installer/"
cp -a "$REPO/recovery/castalia_recovery" "$STAGE$SHARE/recovery/"
find "$STAGE$SHARE" -name __pycache__ -type d -exec rm -rf {} + 2>/dev/null || :
cat > "$STAGE/usr/bin/castalia-instalar-texto" <<'TX'
#!/bin/sh
export PYTHONPATH=/usr/share/castalia/installer
exec python3 -m castalia_installer.tui "$@"
TX
cat > "$STAGE/usr/bin/castalia-restore" <<'RS'
#!/bin/sh
export PYTHONPATH=/usr/share/castalia/recovery
exec python3 -m castalia_recovery "$@"
RS
chmod 755 "$STAGE/usr/bin/castalia-instalar-texto" \
          "$STAGE/usr/bin/castalia-restore"

# default system theme (admin-editable → conffile) — Castalia Human, the
# warm flagship look (§8.2 addendum)
install -Dm644 "$REPO/themes/human/theme.conf" "$STAGE/etc/castalia/theme.conf"
# The Openbox configuration (decorations + the §7.7 global keyboard map).
# It goes in OUR asset tree, never /etc/xdg/openbox/rc.xml — that path is
# owned by the openbox package, and shipping it here makes dpkg refuse to
# unpack openbox ("trying to overwrite ... which is also in package
# castalia-desktop"). castalia-session points openbox at this copy with
# --config-file instead.
install -Dm644 "$REPO/shell/session/openbox-rc.xml" \
    "$STAGE$SHARE/openbox/rc.xml"

# greeter appearance: the Human dawn threshold (§6.6, §8.6)
install -Dm644 "$REPO/services/lightdm/lightdm-gtk-greeter.conf" \
    "$STAGE/etc/lightdm/lightdm-gtk-greeter.conf"

# control metadata. Qt runtime names differ across the time_t64 transition
# (Debian bookworm: libqt5widgets5; Ubuntu noble: libqt5widgets5t64) — the
# alternations accept either.
mkdir -p "$STAGE/DEBIAN"
size_kb=$(du -sk "$STAGE" | cut -f1)
cat > "$STAGE/DEBIAN/control" <<EOF
Package: $PKG
Version: $VERSION
Architecture: $ARCH
Maintainer: Tombatossals Softworks <hola@tombatossals.example>
Installed-Size: $size_kb
Depends: libqt5widgets5t64 | libqt5widgets5, libqt5gui5t64 | libqt5gui5, libqt5core5t64 | libqt5core5a, libqt5svg5t64 | libqt5svg5, python3, fonts-dejavu-core
Recommends: openbox, xinit, xserver-xorg
Section: x11
Priority: optional
Homepage: https://tombatossals.example
Description: Castalia OS Classic desktop — shell, apps and backends
 The complete Castalia Classic desktop: the Castalia Explorer shell
 (panel + desktop + file manager), the Control Center, the full suite of
 first-party Qt applications, the seven-theme design system, the graphical
 and text installers, and the Restore Points recovery backend.
 .
 Castalia OS is an original, legally-clean, XP-class desktop for early-
 and late-2000s PCs. It is not affiliated with Microsoft.
EOF
cat > "$STAGE/DEBIAN/conffiles" <<EOF
/etc/castalia/theme.conf
/etc/lightdm/lightdm-gtk-greeter.conf
EOF

mkdir -p "$OUT"
log "building $DEB"
dpkg-deb --build --root-owner-group "$STAGE" "$DEB" > /dev/null
log "done: $DEB ($(du -h "$DEB" | cut -f1))"
