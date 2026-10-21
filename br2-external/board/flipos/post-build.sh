#!/bin/sh
# Runs after the rootfs tree is built ($1 = TARGET_DIR), before image assembly.
set -eu
T="$1"
mkdir -p "$T/data"                     # mountpoint for the ext4 data image (vdb)
# BusyBox init: keep seatd + the Wayland session alive (respawn). The compositor
# launches the terminal. The serial getty stays for a root shell.
if ! grep -q flipos-session "$T/etc/inittab"; then
  cat >> "$T/etc/inittab" <<'TAB'
# flipos: seat daemon + Wayland session (flipos-wm, which starts foot)
::respawn:/usr/bin/seatd
::respawn:/usr/bin/flipos-session
TAB
fi
