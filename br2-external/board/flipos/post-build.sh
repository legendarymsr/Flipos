#!/bin/sh
# Runs after the rootfs tree is built ($1 = TARGET_DIR), before image assembly.
set -eu
T="$1"
mkdir -p "$T/data"                     # mountpoint for the ext4 data image (vdb)
# BusyBox init: keep the Wayland session alive (respawn); the compositor launches
# the terminal. libseat runs its built-in (in-process) seat backend — Buildroot's
# seatd ships no daemon — so no seatd process is needed. The serial getty stays.
# Regenerate our block every build (TARGET_DIR persists across rebuilds).
sed -i '/# flipos:/d; /\/usr\/bin\/seatd/d; /\/usr\/bin\/flipos-session/d' "$T/etc/inittab"
cat >> "$T/etc/inittab" <<'TAB'
# flipos: Wayland session (flipos-wm, which starts foot)
::respawn:/usr/bin/flipos-session
TAB
