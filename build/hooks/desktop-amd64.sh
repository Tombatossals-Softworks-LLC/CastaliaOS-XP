#!/bin/sh
# Castalia desktop chroot hook (Bible §18 Phase 2).
#
# Runs INSIDE the debootstrapped rootfs (via chroot) after the base packages
# are installed. It builds the Castalia shell against the TARGET's Qt/glibc
# — the correct-ABI way, the same as the eventual .deb packaging would — then
# installs the binaries, stages the runtime asset tree, and wires the
# graphical live session (castalia-live-session -> castalia-session on tty1).
#
# Source was staged at /usr/src/castalia by mkiso.sh (SRC_DIRS).
set -eu
export DEBIAN_FRONTEND=noninteractive

SRC=/usr/src/castalia
PREFIX=/opt/castalia
SHARE=/usr/share/castalia

echo "castalia-hook: installing build toolchain"
apt-get install -y --no-install-recommends \
    build-essential cmake qtbase5-dev libqt5svg5-dev libxcb1-dev python3 \
    qttools5-dev-tools

echo "castalia-hook: exporting theme QSS from tokens (§6.16)"
cd "$SRC"
PYTHONPATH=tools python3 tools/theme_export.py --out "$SHARE/build/out/themes"

echo "castalia-hook: compiling translation catalogues (§7.13)"
# lrelease turns the reviewed i18n/*.ts into the .qm the shell loads. Spanish
# is the source language and has no catalogue, so this installs nothing at all
# on a Spanish-only build — and that is the correct outcome, not a failure.
python3 tools/i18n_build.py release
for qm in build/out/i18n/*.qm; do
    [ -f "$qm" ] || continue
    install -Dm644 "$qm" "$SHARE/i18n/$(basename "$qm")"
done

echo "castalia-hook: staging the GRUB boot identity (§6.2)"
# What the installer copies into /boot/grub and /etc/grub.d on the target.
# Same source and same destination as packages/mkdeb.sh, so an installed
# machine gets its boot menu whichever way Castalia arrived.
install -Dm644 iso/grub/theme/theme.txt "$SHARE/grub/theme/theme.txt"
install -Dm644 iso/boot-bg/splash.png   "$SHARE/grub/theme/splash.png"
install -Dm755 iso/grub/11_castalia_safe "$SHARE/grub/11_castalia_safe"
install -Dm755 iso/grub/12_castalia_recovery "$SHARE/grub/12_castalia_recovery"

echo "castalia-hook: staging the recovery boot environment (§18 P5)"
install -Dm755 recovery/boot/castalia-recovery-console \
    /usr/lib/castalia/recovery/castalia-recovery-console
install -Dm755 recovery/boot/initramfs-hook \
    /etc/initramfs-tools/hooks/castalia-recovery
install -Dm755 recovery/boot/init-premount \
    /etc/initramfs-tools/scripts/init-premount/castalia-recovery

echo "castalia-hook: staging the hardware probe (§6.15)"
install -Dm644 hwprobe/quirks.json /usr/share/castalia/hwprobe/quirks.json
cp -a hwprobe/castalia_hwprobe /usr/share/castalia/hwprobe/
for f in run log/run service.conf; do
    install -Dm644 "services/castalia-hwprobe/$f" \
        "/usr/share/castalia/services/castalia-hwprobe/$f"
done
chmod 755 /usr/share/castalia/services/castalia-hwprobe/run \
          /usr/share/castalia/services/castalia-hwprobe/log/run
cat > /usr/bin/castalia-hwprobe <<'HW'
#!/bin/sh
export PYTHONPATH=/usr/share/castalia/hwprobe
exec python3 -m castalia_hwprobe "$@"
HW
chmod 755 /usr/bin/castalia-hwprobe

echo "castalia-hook: building the shell (target Qt, correct ABI)"
cmake -S shell -B /tmp/shellbuild -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/shellbuild -j"$(nproc)"

echo "castalia-hook: installing binaries -> $PREFIX/bin"
install -Dm755 /tmp/shellbuild/panel/castalia-panel      "$PREFIX/bin/castalia-panel"
install -Dm755 /tmp/shellbuild/desktop/castalia-desktop  "$PREFIX/bin/castalia-desktop"
install -Dm755 /tmp/shellbuild/explorer/castalia-explorer "$PREFIX/bin/castalia-explorer"
install -Dm755 /tmp/shellbuild/apps/buscar/castalia-buscar "$PREFIX/bin/castalia-buscar"
install -Dm755 /tmp/shellbuild/apps/control-center/castalia-control-center "$PREFIX/bin/castalia-control-center"
install -Dm755 /tmp/shellbuild/apps/text-editor/castalia-notas "$PREFIX/bin/castalia-notas"
install -Dm755 /tmp/shellbuild/apps/calculator/castalia-calc "$PREFIX/bin/castalia-calc"
install -Dm755 /tmp/shellbuild/apps/image-viewer/castalia-visor "$PREFIX/bin/castalia-visor"
install -Dm755 /tmp/shellbuild/apps/multimedia/castalia-multimedia "$PREFIX/bin/castalia-multimedia"
install -Dm755 /tmp/shellbuild/apps/wine-manager/castalia-wine "$PREFIX/bin/castalia-wine"
install -Dm755 /tmp/shellbuild/apps/clasicos/castalia-clasicos "$PREFIX/bin/castalia-clasicos"
install -Dm755 /tmp/shellbuild/apps/paint/castalia-pintura "$PREFIX/bin/castalia-pintura"
install -Dm755 /tmp/shellbuild/apps/diagnostics/castalia-diagnostico "$PREFIX/bin/castalia-diagnostico"
install -Dm755 /tmp/shellbuild/apps/welcome/castalia-bienvenida "$PREFIX/bin/castalia-bienvenida"
install -Dm755 /tmp/shellbuild/apps/screensaver/castalia-salvapantallas "$PREFIX/bin/castalia-salvapantallas"
install -Dm755 /tmp/shellbuild/apps/archiver/castalia-archivador "$PREFIX/bin/castalia-archivador"
install -Dm755 /tmp/shellbuild/apps/terminal/castalia-terminal "$PREFIX/bin/castalia-terminal"
install -Dm755 /tmp/shellbuild/apps/monitor/castalia-monitor "$PREFIX/bin/castalia-monitor"
install -Dm755 /tmp/shellbuild/apps/volumen/castalia-volumen "$PREFIX/bin/castalia-volumen"
install -Dm755 /tmp/shellbuild/apps/screenshot/castalia-captura "$PREFIX/bin/castalia-captura"
install -Dm755 /tmp/shellbuild/apps/software/castalia-software "$PREFIX/bin/castalia-software"
install -Dm755 /tmp/shellbuild/apps/buscaminas/castalia-buscaminas "$PREFIX/bin/castalia-buscaminas"
install -Dm755 /tmp/shellbuild/apps/solitario/castalia-solitario "$PREFIX/bin/castalia-solitario"
install -Dm755 /tmp/shellbuild/apps/charmap/castalia-caracteres "$PREFIX/bin/castalia-caracteres"
install -Dm755 /tmp/shellbuild/apps/richtext/castalia-escritor "$PREFIX/bin/castalia-escritor"
install -Dm755 /tmp/shellbuild/apps/stickies/castalia-adhesivas "$PREFIX/bin/castalia-adhesivas"
install -Dm755 /tmp/shellbuild/apps/clock/castalia-reloj "$PREFIX/bin/castalia-reloj"
install -Dm755 /tmp/shellbuild/apps/calendario/castalia-calendario "$PREFIX/bin/castalia-calendario"
install -Dm755 /tmp/shellbuild/apps/fechahora/castalia-fechahora "$PREFIX/bin/castalia-fechahora"
install -Dm755 /tmp/shellbuild/apps/usuarios/castalia-usuarios "$PREFIX/bin/castalia-usuarios"
install -Dm755 /tmp/shellbuild/apps/papelera/castalia-papelera "$PREFIX/bin/castalia-papelera"
install -Dm755 /tmp/shellbuild/apps/impresoras/castalia-impresoras "$PREFIX/bin/castalia-impresoras"
install -Dm755 /tmp/shellbuild/apps/redes/castalia-redes "$PREFIX/bin/castalia-redes"
install -Dm755 /tmp/shellbuild/apps/acerca/castalia-acerca "$PREFIX/bin/castalia-acerca"
install -Dm755 /tmp/shellbuild/apps/predeterminados/castalia-predeterminados "$PREFIX/bin/castalia-predeterminados"
install -Dm755 /tmp/shellbuild/apps/magnifier/castalia-lupa "$PREFIX/bin/castalia-lupa"
install -Dm755 /tmp/shellbuild/apps/teclado/castalia-teclado "$PREFIX/bin/castalia-teclado"
install -Dm755 /tmp/shellbuild/apps/updates/castalia-actualizaciones "$PREFIX/bin/castalia-actualizaciones"
install -Dm755 /tmp/shellbuild/apps/salir/castalia-salir "$PREFIX/bin/castalia-salir"
install -Dm755 /tmp/shellbuild/apps/ejecutar/castalia-ejecutar "$PREFIX/bin/castalia-ejecutar"
install -Dm755 /tmp/shellbuild/apps/notificaciones/castalia-notificaciones "$PREFIX/bin/castalia-notificaciones"
install -Dm755 /tmp/shellbuild/apps/registros/castalia-registros "$PREFIX/bin/castalia-registros"
install -Dm755 /tmp/shellbuild/apps/servicios/castalia-servicios "$PREFIX/bin/castalia-servicios"
install -Dm755 /tmp/shellbuild/apps/hardware/castalia-hardware "$PREFIX/bin/castalia-hardware"
install -Dm755 /tmp/shellbuild/apps/discos/castalia-discos "$PREFIX/bin/castalia-discos"
install -Dm755 /tmp/shellbuild/apps/migrar/castalia-migrar "$PREFIX/bin/castalia-migrar"
install -Dm755 /tmp/shellbuild/installer-gui/castalia-instalador "$PREFIX/bin/castalia-instalador"
install -Dm755 /tmp/shellbuild/recovery-gui/castalia-recuperacion "$PREFIX/bin/castalia-recuperacion"
install -Dm755 shell/session/castalia-session            "$PREFIX/bin/castalia-session"
install -Dm755 shell/session/castalia-open               "$PREFIX/bin/castalia-open"
for b in castalia-panel castalia-desktop castalia-explorer castalia-buscar castalia-control-center castalia-notas castalia-calc castalia-visor castalia-multimedia castalia-wine castalia-clasicos castalia-calendario castalia-pintura castalia-diagnostico castalia-bienvenida castalia-salvapantallas castalia-archivador castalia-terminal castalia-monitor castalia-volumen castalia-captura castalia-software castalia-buscaminas castalia-solitario castalia-caracteres castalia-escritor castalia-adhesivas castalia-reloj castalia-fechahora castalia-usuarios castalia-papelera castalia-impresoras castalia-redes castalia-acerca castalia-predeterminados castalia-lupa castalia-teclado castalia-actualizaciones castalia-salir castalia-ejecutar castalia-notificaciones castalia-registros castalia-servicios castalia-hardware castalia-discos castalia-migrar castalia-instalador castalia-recuperacion; do
    ln -sf "$PREFIX/bin/$b" "/usr/local/bin/$b"
done

# Win32 compatibility demo (Bible §11): if Wine is installed (compat edition)
# and our original hello.exe was staged, install it so the live session can
# run a REAL Windows program under Wine.
CASTALIA_HAS_WINE=0
if command -v wine >/dev/null 2>&1 \
   && [ -f "$SRC/compat/win32-demo/hello.exe" ]; then
    install -Dm644 "$SRC/compat/win32-demo/hello.exe" \
        "$PREFIX/share/win32-demo/hello.exe"
    CASTALIA_HAS_WINE=1
    echo "castalia-hook: Wine present — Win32 demo installed"
fi

echo "castalia-hook: staging runtime asset tree -> $SHARE"
cp -a themes "$SHARE/"
cp -a branding "$SHARE/"
install -Dm644 themes/human/theme.conf /etc/castalia/theme.conf
# Greeter appearance (§6.6): staged when the profile ships services/ (the
# live images boot straight to X, so this only matters post-install).
if [ -f services/lightdm/lightdm-gtk-greeter.conf ]; then
    install -Dm644 services/lightdm/lightdm-gtk-greeter.conf \
        /etc/lightdm/lightdm-gtk-greeter.conf
fi

# Stage the installer's shared Python backend (Bible §14) so the graphical
# installer (castalia-instalador) can drive it: it runs
# `python3 -m castalia_installer` with PYTHONPATH=$SHARE/installer.
if [ -d installer/castalia_installer ]; then
    mkdir -p "$SHARE/installer"
    cp -a installer/castalia_installer "$SHARE/installer/"
    # Text installer entry point — the guaranteed fallback (§14.5 #5),
    # reachable from any console even if graphics fail.
    cat > /usr/local/bin/castalia-instalar-texto <<'TX'
#!/bin/sh
export PYTHONPATH=/usr/share/castalia/installer
exec python3 -m castalia_installer.tui "$@"
TX
    chmod +x /usr/local/bin/castalia-instalar-texto
    echo "castalia-hook: installer backend + text UI staged -> $SHARE/installer"
fi

# Stage the Restore Points backend (Bible §9, P8) + a launcher.
if [ -d recovery/castalia_recovery ]; then
    mkdir -p "$SHARE/recovery"
    cp -a recovery/castalia_recovery "$SHARE/recovery/"
    cat > /usr/local/bin/castalia-restore <<'RS'
#!/bin/sh
export PYTHONPATH=/usr/share/castalia/recovery
exec python3 -m castalia_recovery "$@"
RS
    chmod +x /usr/local/bin/castalia-restore
    echo "castalia-hook: Restore Points backend staged -> $SHARE/recovery"
fi

echo "castalia-hook: installing Openbox themes (generated themerc) + icons"
for tdir in "$SHARE"/build/out/themes/*/; do
    id=$(basename "$tdir")
    if [ -f "$tdir/openbox-3/themerc" ]; then
        install -Dm644 "$tdir/openbox-3/themerc" \
            "/usr/share/themes/Castalia-$id/openbox-3/themerc"
    fi
done
# Openbox uses the active theme's decorations, the Castalia icon family and
# the shipped keyboard map (§7.7) — one source of truth in the repo, so the
# live image and an installed system get the identical bindings. It lands in
# the Castalia asset tree, NOT /etc/xdg/openbox/rc.xml: that path belongs to
# the openbox package. castalia-session passes it with --config-file.
install -Dm644 shell/session/openbox-rc.xml "$SHARE/openbox/rc.xml"
# a friendly icon theme index so Qt finds the Castalia icons by name
install -d /usr/share/icons/Castalia/48x48/apps
cp "$SHARE"/themes/icons/48/*.svg /usr/share/icons/Castalia/48x48/apps/ \
    2>/dev/null || true

# The Xcursor pointer theme. Built from the staged sources when the image has
# ImageMagick; the desktop falls back to the stock pointers when it does not,
# so this never blocks an ISO build.
if command -v rsvg-convert >/dev/null 2>&1 || command -v convert >/dev/null 2>&1; then
    echo "castalia-hook: generating the Castalia-Human cursor theme"
    PYTHONPATH=tools python3 tools/cursor_gen.py --out /usr/share/icons.tmp \
        && cp -a /usr/share/icons.tmp/Castalia-Human /usr/share/icons/ \
        && install -d /usr/share/icons/default \
        && printf '[Icon Theme]\nName=Default\nInherits=Castalia-Human\n' \
            > /usr/share/icons/default/index.theme
    rm -rf /usr/share/icons.tmp
else
    echo "castalia-hook: no SVG rasteriser — keeping the stock pointers"
fi

echo "castalia-hook: wiring the graphical live session"
# allow the root X server to start from the console (live proof)
printf 'allowed_users=anybody\nneeds_root_rights=yes\n' > /etc/X11/Xwrapper.config
cat > /usr/local/bin/castalia-live-session <<'SX'
#!/bin/sh
# The live session's entry point, respawned from /etc/inittab on tty1.
#
# It does three things the bare `startx` it replaced did not:
#   1. reads castalia.installer=<gui|text> off the kernel command line, which
#      is what the boot menu's "Instalar Castalia OS" entries pass;
#   2. marks the session as live (CASTALIA_LIVE=1), which is what puts the
#      "Instalar Castalia OS" icon on the desktop;
#   3. never leaves a black screen. If X cannot start on this hardware, it
#      says so in Spanish and hands over a root shell with the two commands
#      that still work — a live image whose graphics fail must still be
#      installable, not dead.
export CASTALIA_PREFIX=/opt/castalia
export CASTALIA_REPO=/usr/share/castalia
export HOME=/root
export CASTALIA_LIVE=1
__WINE_DEMO__

installer_mode=$(sed -n 's/.*castalia\.installer=\([a-z]*\).*/\1/p' \
                 /proc/cmdline 2>/dev/null)

case "$installer_mode" in
    text)
        # §14.5 #5: the guaranteed fallback. No X at all. If it is somehow
        # not on the image, fall through to the message below rather than
        # exec'ing into nothing and being respawned forever.
        if [ -x /usr/local/bin/castalia-instalar-texto ]; then
            echo "Castalia OS — instalacion en modo texto"
            exec /usr/local/bin/castalia-instalar-texto
        fi
        echo "castalia: el instalador de texto no esta en esta imagen"
        ;;
    gui)
        # Install on top of a desktop the user can see and try first.
        export CASTALIA_AUTOSTART_INSTALLER=1
        ;;
    *)
        # An ordinary live boot opens a window or two so the first
        # impression shows the OS in use.
        export CASTALIA_DEMO=1
        ;;
esac

startx /opt/castalia/bin/castalia-session -- :0 vt1 -nolisten tcp && exit 0

cat <<'MSG'

  El servidor grafico no ha podido arrancar en este equipo.
  Castalia sigue siendo utilizable desde aqui:

    castalia-live-session      reintentar el escritorio
    castalia-instalar-texto    instalar sin graficos
    reboot                     reiniciar

  El registro esta en /var/log/castalia-x.log

MSG
exec /sbin/agetty --autologin root tty1 linux
SX
# turn on the Wine demo only when Wine + the exe are present
if [ "$CASTALIA_HAS_WINE" = "1" ]; then
    sed -i 's#__WINE_DEMO__#export CASTALIA_DEMO_WINE=1#' \
        /usr/local/bin/castalia-live-session
else
    sed -i '/__WINE_DEMO__/d' /usr/local/bin/castalia-live-session
fi
chmod +x /usr/local/bin/castalia-live-session
# swap the tty1 autologin getty for the X autostart; keep ttyS0 for debug
sed -i '\#agetty --autologin root tty1#d' /etc/inittab
echo 'x1:2:respawn:/usr/local/bin/castalia-live-session </dev/tty1 >/dev/tty1 2>/var/log/castalia-x.log' \
    >> /etc/inittab
# Whatever a user ends up looking at on a console, tell them what to run.
cat > /etc/issue <<'ISSUE'
Castalia OS — sesion en vivo

  castalia-live-session    abrir el escritorio
  castalia-instalar-texto  instalar sin graficos

ISSUE

echo "castalia-hook: shrinking image (purge build toolchain)"
apt-get purge -y build-essential cmake qtbase5-dev libqt5svg5-dev libxcb1-dev \
    >/dev/null 2>&1 || true
apt-get autoremove -y >/dev/null 2>&1 || true
apt-get clean
rm -rf /tmp/shellbuild "$SRC"
echo "castalia-hook: done"
