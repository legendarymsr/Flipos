#!/system/bin/sh
# Make sure a NATIVE Android qemu-system-aarch64 is present at
# /data/flipos/bin/ (no Debian chroot). Run as root (KernelSU).
# SAFETY: writes only under /data/flipos.
set -eu
FLIPOS=/data/flipos
BIN="$FLIPOS/bin"
mkdir -p "$BIN" "$FLIPOS/qemu/lib"

QEMU="$BIN/qemu-system-aarch64"
if [ -x "$QEMU" ]; then echo "flipos: qemu already present ($QEMU)"; exit 0; fi

# 1) Bundled in the module zip? customize.sh would have copied it. (handled above)

# 2) A qemu you point us at (static or a dir with qemu + its libs).
if [ -n "${QEMU_SRC:-}" ] && [ -x "$QEMU_SRC" ]; then
  cp -f "$QEMU_SRC" "$QEMU"; chmod 0755 "$QEMU"
  [ -x "$(dirname "$QEMU_SRC")/qemu-img" ] && cp -f "$(dirname "$QEMU_SRC")/qemu-img" "$BIN/" || true
  echo "flipos: installed qemu from $QEMU_SRC"; exit 0
fi

# 3) Termux's qemu, if Termux is installed (it's native Android, no chroot).
TERMUX=/data/data/com.termux/files/usr
if [ -x "$TERMUX/bin/qemu-system-aarch64" ]; then
  echo "flipos: linking Termux qemu (+libs via LD_LIBRARY_PATH)"
  cp -f "$TERMUX/bin/qemu-system-aarch64" "$QEMU"; chmod 0755 "$QEMU"
  [ -x "$TERMUX/bin/qemu-img" ] && cp -f "$TERMUX/bin/qemu-img" "$BIN/" || true
  # Termux qemu is dynamic against Termux libs; copy the ones it needs.
  cp -f "$TERMUX/lib/"*.so* "$FLIPOS/qemu/lib/" 2>/dev/null || true
  echo "flipos: qemu installed"; exit 0
fi

cat <<MSG
flipos: no native qemu found. Options to provide one (pick one):
  a) Build a static qemu for Android and drop it at $QEMU:
        scripts/build-qemu-android.sh    (see docs; runs on a Linux build box)
  b) QEMU_SRC=/path/to/qemu-system-aarch64 su -c sh $0
  c) Install Termux + 'pkg install qemu-system-aarch64-headless', then re-run $0
The binary runs DIRECTLY on Android's kernel — no Debian chroot.
MSG
exit 1
