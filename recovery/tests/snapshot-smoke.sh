#!/bin/sh
# snapshot-smoke.sh — prove Restore Points on real files (Bible §9, P8).
#
# Uses only temp directories (no root): builds a fake "system", takes a restore
# point, breaks the system (edit + delete + add files), takes a second point
# (checking --link-dest dedup), then restores the first point and verifies the
# system is back exactly as it was. Exercises the REAL rsync engine.
set -eu
export PATH="/usr/local/sbin:/usr/sbin:/sbin:$PATH"
command -v rsync >/dev/null 2>&1 || { echo "need rsync" >&2; exit 2; }

HERE=$(cd "$(dirname "$0")/.." && pwd)     # recovery/
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT INT TERM
SYS="$WORK/sys"
STORE="$WORK/store"
mkdir -p "$SYS/etc" "$SYS/usr/bin"
echo "v1" > "$SYS/etc/config"
echo "importante" > "$SYS/etc/keep.conf"
printf '#!/bin/sh\necho hi\n' > "$SYS/usr/bin/tool"
chmod +x "$SYS/usr/bin/tool"

run() {
    PYTHONPATH="$HERE" python3 -m castalia_recovery \
        --source "$SYS" --store "$STORE" "$@"
}

echo "snapshot-smoke: taking the first restore point"
run create --label "estado inicial" --reason manual
P1=$(run list | head -1 | awk '{print $1}')
echo "snapshot-smoke:   point = $P1"

echo "snapshot-smoke: breaking the system (edit + delete + add)"
echo "v2-ROTO" > "$SYS/etc/config"
rm "$SYS/etc/keep.conf"
echo "basura" > "$SYS/etc/anadido-por-mala-actualizacion.conf"
sleep 1   # ensure a distinct timestamp id for the second point
run create --label "tras actualización" --reason pre-update >/dev/null
P2=$(run list | head -1 | awk '{print $1}')

echo "snapshot-smoke: restoring $P1"
run restore "$P1" --confirm >/dev/null

fail=0
check() { if [ "$1" = "ok" ]; then echo "  OK  $2"; else echo "  FAIL $2" >&2; fail=1; fi; }
[ "$(cat "$SYS/etc/config")" = "v1" ] && check ok "config revertido a v1" \
    || check no "config"
[ -f "$SYS/etc/keep.conf" ] && check ok "archivo borrado restaurado" \
    || check no "keep.conf ausente"
[ ! -f "$SYS/etc/anadido-por-mala-actualizacion.conf" ] \
    && check ok "archivo de la mala actualización eliminado" \
    || check no "archivo malo sigue presente"
[ -x "$SYS/usr/bin/tool" ] && check ok "bit de ejecución preservado" \
    || check no "exec bit"
# dedup: the unchanged 'tool' should be hardlinked between P1 and P2
if [ "$(stat -c '%i' "$STORE/$P1/usr/bin/tool")" = \
     "$(stat -c '%i' "$STORE/$P2/usr/bin/tool")" ]; then
    check ok "archivo sin cambios compartido por enlace duro (dedup)"
else
    check no "sin dedup por enlace duro"
fi
# the pre-restore point was taken automatically (restore is reversible)
[ "$(run list | wc -l)" -ge 3 ] \
    && check ok "punto pre-restauración creado (restauración reversible)" \
    || check no "sin punto pre-restauración"

[ $fail -eq 0 ] && echo "snapshot-smoke: PASS — Restore Points really work" \
    || { echo "snapshot-smoke: FAIL" >&2; exit 1; }
