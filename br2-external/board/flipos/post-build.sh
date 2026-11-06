#!/bin/sh
# Runs after the rootfs tree is built ($1 = TARGET_DIR), before image assembly.
set -eu
T="$1"
mkdir -p "$T/data"                     # mountpoint for the ext4 data image (vdb)
# BusyBox init: keep the Wayland session alive (respawn); the compositor launches
# the terminal. libseat runs its built-in (in-process) seat backend — Buildroot's
# seatd ships no daemon — so no seatd process is needed. The serial getty stays.
# Regenerate our block every build (TARGET_DIR persists across rebuilds).
sed -i '/# flipos:/d; /\/usr\/bin\/seatd/d; /\/usr\/bin\/flipos-session/d; /\/usr\/bin\/flipos-hostd/d' "$T/etc/inittab"
cat >> "$T/etc/inittab" <<'TAB'
# flipos: Wayland session (flipos-wm, which starts foot)
::respawn:/usr/bin/flipos-session
# flipos: phone status (battery) from the Android host over virtio-serial
::respawn:/usr/bin/flipos-hostd
TAB

# Android host bundle: move QEMU (built here for aarch64/glibc) out of the guest
# into $BINARIES_DIR/android-host. The guest image never contains QEMU.
python3 -I "$(dirname "$0")/android-bundle.py" "$T" "$BINARIES_DIR/android-host" "$BUILD_DIR"
