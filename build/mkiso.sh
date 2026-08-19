#!/bin/sh
# mkiso.sh — Castalia live ISO builder (Bible §17.2, §14.1).
#
# Staged pipeline: deps -> bootstrap -> configure -> packages -> assets ->
# squashfs -> iso. Each stage runs through run(), which under --dry-run
# prints the exact plan (and validates the edition profile) without touching
# the system — that is what CI exercises. A real run needs root plus
# debootstrap/mksquashfs/xorriso on the build host and produces a
# BIOS-bootable hybrid ISO (USB + CD) that boots via isolinux + live-boot.
#
# Usage:
#   sh build/mkiso.sh --edition live-amd64 [--dry-run] [--out DIR]
#
# Editions live in build/profiles/<edition>.conf (sh-sourceable KEY="value").

set -eu

REPO=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
EDITION="classic64"
DRY=0
OUT="$REPO/build/out/iso"

usage() { sed -n '2,14p' "$0" | sed 's/^# \{0,1\}//'; exit "${1:-0}"; }
die()   { printf 'mkiso: error: %s\n' "$*" >&2; exit 1; }

while [ $# -gt 0 ]; do
    case "$1" in
        --edition) EDITION=${2:?}; shift 2 ;;
        --dry-run) DRY=1; shift ;;
        --out)     OUT=${2:?}; shift 2 ;;
        -h|--help) usage ;;
        *)         die "unknown option: $1" ;;
    esac
done

PROFILE="$REPO/build/profiles/$EDITION.conf"
[ -r "$PROFILE" ] || die "unknown edition '$EDITION' (no $PROFILE)"
# shellcheck disable=SC1090
. "$PROFILE"
for var in LABEL ARCH SUITE MIRROR PACKAGES COMPRESSION; do
    eval "val=\${$var:-}"
    [ -n "$val" ] || die "profile $EDITION.conf is missing $var"
done

WORK="$OUT/work-$EDITION"
ROOTFS="$WORK/rootfs"
STAGING="$WORK/staging"
ISO="$OUT/castalia-$EDITION.iso"
ISOLINUX_LIB="/usr/lib/ISOLINUX"
SYSLINUX_MOD="/usr/lib/syslinux/modules/bios"

log() { printf 'mkiso[%s]: %s\n' "$EDITION" "$*"; }

# run STAGE DESCRIPTION CMD... — prints the plan under --dry-run.
run() {
    stage=$1; shift
    desc=$1; shift
    if [ "$DRY" -eq 1 ]; then
        log "PLAN $stage: $desc"
        log "     $*"
    else
        log "RUN  $stage: $desc"
        "$@"
    fi
}

mkdirp() { [ "$DRY" -eq 0 ] && mkdir -p "$@"; return 0; }

# ---------------------------------------------------------------- stages --

stage_deps() {
    for tool in debootstrap mksquashfs xorriso; do
        if [ "$DRY" -eq 1 ]; then
            log "PLAN deps: require $tool on the build host"
        else
            command -v "$tool" >/dev/null || die "missing build tool: $tool"
        fi
    done
    if [ "$DRY" -eq 0 ]; then
        [ "$(id -u)" -eq 0 ] || die "a real build needs root; use --dry-run"
        rm -rf "$WORK"
        mkdir -p "$ROOTFS" "$STAGING/live" "$STAGING/isolinux"
    fi
}

stage_bootstrap() {
    run bootstrap "debootstrap $SUITE/$ARCH (minbase) from $MIRROR" \
        debootstrap --arch="$ARCH" --variant=minbase \
        "$SUITE" "$ROOTFS" "$MIRROR"
}

stage_configure() {
    run configure "hostname + apt sources (Bible §13.1)" \
        sh -c "echo castalia > '$ROOTFS/etc/hostname';
               printf 'deb %s %s main\n' '$MIRROR' '$SUITE' \
                   > '$ROOTFS/etc/apt/sources.list'"
    # Complete SysVinit control (§6.4 fallback init): debootstrap minbase
    # ships no usable /etc/inittab, so init would stall at "Enter runlevel:".
    # We write initdefault=2, a no-op sysinit, and autologin-root gettys on
    # serial + tty1 so the boot proof is capturable headlessly. runit-init in
    # the live path is a Phase-1 follow-up.
    run configure "write /etc/inittab (initdefault + autologin gettys)" \
        sh -c "cat > '$ROOTFS/etc/inittab' <<'INITTAB'
# Castalia live boot — minimal SysVinit control (Bible §6.4).
id:2:initdefault:
si::sysinit:/bin/mount -a
T0:23:respawn:/sbin/agetty --autologin root -L 115200 ttyS0 vt100
1:23:respawn:/sbin/agetty --autologin root tty1 linux
INITTAB
               printf 'castalia %s (%s) — arranque de prueba\n\n' \
                   '$SUITE' '$ARCH' > '$ROOTFS/etc/issue'"
    run configure "brand /etc/os-release" \
        sh -c "cat > '$ROOTFS/etc/os-release' <<'OSREL'
NAME=\"Castalia OS\"
PRETTY_NAME=\"Castalia Classic ($EDITION)\"
ID=castalia
ID_LIKE=debian
HOME_URL=\"https://tombatossals.example\"
OSREL"
}

stage_packages() {
    run packages "install edition set: $PACKAGES" \
        chroot "$ROOTFS" sh -c \
        "export DEBIAN_FRONTEND=noninteractive;
         apt-get update &&
         apt-get install -y --no-install-recommends $PACKAGES &&
         passwd -d root &&
         apt-get clean"
}

stage_hook() {
    # Optional per-profile chroot hook (e.g. build + install the Castalia
    # shell against the TARGET's Qt/glibc — the correct-ABI way, matching the
    # eventual .deb packaging). SRC_DIRS are copied into the chroot first.
    [ -n "${HOOK:-}" ] || return 0
    run hook "stage source (${SRC_DIRS:-}) into chroot" \
        sh -c "mkdir -p '$ROOTFS/usr/src/castalia';
               for d in ${SRC_DIRS:-}; do
                   cp -a '$REPO'/\"\$d\" '$ROOTFS/usr/src/castalia/'; done"
    # `&&`, not `;`. With semicolons the chroot's exit status was thrown
    # away and the build's status became `rm`'s — so a hook that died half
    # way through produced an ISO with no shell on it and a green build. That
    # is not hypothetical: it is how castalia-live-desktop-amd64 0.1.1 went
    # out with no /opt/castalia at all, booting to a bare root shell.
    run hook "run chroot hook: $HOOK (build + install shell, configure X)" \
        sh -c "set -e;
               cp '$REPO/$HOOK' '$ROOTFS/tmp/castalia-hook.sh';
               chroot '$ROOTFS' sh /tmp/castalia-hook.sh;
               rm -f '$ROOTFS/tmp/castalia-hook.sh'"
    # …and then check the artefact rather than the exit code. A hook can
    # "succeed" and still leave the image without the thing the edition is
    # named after; this is the last point at which that is cheap to catch.
    run hook "verify the hook left a bootable Castalia" true
    for required in bin/castalia-panel bin/castalia-desktop bin/castalia-session
    do
        if [ "$DRY" -eq 0 ] && [ ! -e "$ROOTFS/opt/castalia/$required" ]; then
            die "hook finished but /opt/castalia/$required is missing: this
     image would boot to a console, not to Castalia"
        fi
    done
    if [ "${INSTALLER:-no}" = "yes" ] && [ "$DRY" -eq 0 ]; then
        [ -x "$ROOTFS/usr/local/bin/castalia-live-session" ] \
            || die "no castalia-live-session: the boot menu offers a live
     desktop this image cannot start"
        [ -e "$ROOTFS/opt/castalia/bin/castalia-instalador" ] \
            || die "the boot menu offers to install but there is no installer"
    fi
}

stage_assets() {
    # kernel + live-boot initrd out of the rootfs onto the ISO
    run assets "copy kernel + initrd to /live" \
        sh -c "cp \"\$(ls -1 '$ROOTFS'/boot/vmlinuz-* | tail -1)\" \
                   '$STAGING/live/vmlinuz';
               cp \"\$(ls -1 '$ROOTFS'/boot/initrd.img-* | tail -1)\" \
                   '$STAGING/live/initrd.img'"
    # baked boot background + isolinux payload
    run assets "bake boot-menu background (§6.2)" \
        env PYTHONPATH="$REPO/tools" python3 "$REPO/tools/bootbg_gen.py" \
        --out "$STAGING/isolinux/splash.png"
    run assets "stage isolinux binaries + c32 modules" \
        sh -c "cp '$ISOLINUX_LIB/isolinux.bin' '$STAGING/isolinux/';
               cp '$SYSLINUX_MOD/ldlinux.c32' '$SYSLINUX_MOD/libcom32.c32' \
                  '$SYSLINUX_MOD/libutil.c32' '$SYSLINUX_MOD/vesamenu.c32' \
                  '$STAGING/isolinux/'"
    # The boot menu comes from iso/isolinux/isolinux.cfg.in — one menu, in
    # the repo, rendered per edition. It used to be written inline here while
    # iso/isolinux/isolinux.cfg sat unread as a "design", which is how an ISO
    # shipped with a single entry and no way to reach the installer. The live
    # session is the default entry: a live image you have to configure before
    # you can look at it is not a live image (§14.1).
    # The install entries only go in for an edition that ships an installer.
    install_flag=""
    if [ "${INSTALLER:-no}" = "yes" ]; then
        install_flag="--install-entries $REPO/iso/isolinux/entries-install.cfg"
    fi
    # console=ttyS0 rides along on every entry: harmless on real hardware,
    # and it is the only way the QEMU boot gate can see the guest at all.
    # shellcheck disable=SC2086  # install_flag is a deliberate two-word pair
    run assets "render boot menu from iso/isolinux/isolinux.cfg.in" \
        python3 "$REPO/tools/boot_menu.py" \
        --template "$REPO/iso/isolinux/isolinux.cfg.in" \
        --out "$STAGING/isolinux/isolinux.cfg" \
        --title "$LABEL" \
        --append "boot=live console=tty0 console=ttyS0,115200" \
        $install_flag
}

stage_squashfs() {
    # Keep /boot INSIDE the squashfs: it is the installer's source root
    # (/run/live/rootfs/filesystem.squashfs), so the kernel + initrd must
    # travel with it or an installed system has no vmlinuz/initrd to boot.
    # live-boot ignores the in-squashfs /boot (it uses the ISO's /live/vmlinuz),
    # so keeping it costs only a little space and buys bootable installs.
    run squashfs "compress rootfs ($COMPRESSION)" \
        mksquashfs "$ROOTFS" "$STAGING/live/filesystem.squashfs" \
        -comp "$COMPRESSION" -noappend
}

stage_iso() {
    run iso "hybrid El-Torito ISO (USB + CD, §14.1)" \
        xorriso -as mkisofs -o "$ISO" \
        -V "CASTALIA_$(echo "$EDITION" | tr 'a-z-' 'A-Z_')" \
        -isohybrid-mbr "$ISOLINUX_LIB/isohdpfx.bin" \
        -c isolinux/boot.cat -b isolinux/isolinux.bin \
        -no-emul-boot -boot-load-size 4 -boot-info-table \
        "$STAGING"
}

# ------------------------------------------------------------------ main --

log "edition: $LABEL ($ARCH, $SUITE) -> $ISO"
[ "$DRY" -eq 1 ] && log "dry run: printing the plan only"

mkdirp "$OUT"
stage_deps
stage_bootstrap
stage_configure
stage_packages
stage_hook
stage_assets
stage_squashfs
stage_iso

if [ "$DRY" -eq 1 ]; then
    log "plan complete (7 stages) — run without --dry-run on a build host"
else
    log "done: $ISO ($(du -h "$ISO" | cut -f1))"
fi
