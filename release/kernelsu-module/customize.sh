# flipos KernelSU module — install step (runs in the manager's installer).
# Configures everything: guest images, bundled QEMU (KVM + TCG, VNC + websocket),
# the noVNC web viewer, the `flipos` controller, the Action button and WebUI.
# SAFETY: copies files to /data/flipos only. No partition writes, no system
# overlay, no dd/mkfs/fastboot, no remount, and nothing starts at boot.
SKIPMOUNT=true          # no system/ overlay: nothing is mounted over Android
ASH_STANDALONE=1
# NOTE: managers (KernelSU-Next, Magisk) do not keep the zip's exec bits when
# extracting, so test files with -f and set modes explicitly below.

F=/data/flipos
P="$MODPATH/payload"

# Upgrading while flipos runs? stop it first (its disk files are about to be replaced).
[ -f "$F/bin/flipos" ] && sh "$F/bin/flipos" stop >/dev/null 2>&1

ui_print "- flipos: installing to $F (systemless, /data only)"
mkdir -p "$F/images" "$F/bin" "$F/run" "$F/tmp"

for f in Image rootfs.erofs; do
  [ -f "$P/images/$f" ] || abort "! payload/images/$f missing from the zip"
done
cp -f "$P/images/Image" "$P/images/rootfs.erofs" "$F/images/"
ui_print "- guest OS images installed (your data.img is kept)"

[ -f "$P/host/lib/ld-linux-aarch64.so.1" ] && [ -f "$P/host/bin/qemu-system-aarch64" ] || abort "! bundled QEMU missing from the zip"
rm -rf "$F/host" "$F/novnc"
cp -a "$P/host" "$F/host"
cp -a "$P/novnc" "$F/novnc"
cp -f "$P/flipos" "$F/bin/flipos"
rm -rf "$F/qemu" "$F/bin/run-flipos.sh" "$F/bin/bootstrap-qemu.sh" \
       "$F/bin/qemu-system-aarch64" "$F/bin/qemu-img"            # pre-0.3 layout
ui_print "- QEMU $(cat "$P/host/QEMU_VERSION" 2>/dev/null) + noVNC installed"

set_perm_recursive "$F" 0 0 0755 0644
set_perm_recursive "$F/host/bin" 0 0 0755 0755
set_perm "$F/host/lib/ld-linux-aarch64.so.1" 0 0 0755
set_perm "$F/bin/flipos" 0 0 0755
chmod 0700 "$F/images" "$F/run"            # the VM's disks are root-only
[ -f "$F/images/data.img" ] && chmod 0600 "$F/images/data.img"

# Self-test the bundled QEMU on this phone (catches a loader/libc problem now).
if "$F/host/lib/ld-linux-aarch64.so.1" --library-path "$F/host/lib" \
     "$F/host/bin/qemu-system-aarch64" --version >/dev/null 2>&1; then
  ui_print "- QEMU runs on this device"
else
  ui_print "! WARNING: bundled QEMU failed its self-test on this device"
fi
if [ -c /dev/kvm ]; then
  ui_print "- /dev/kvm found: flipos will use KVM (falls back to TCG if it fails)"
else
  ui_print "- no /dev/kvm: flipos will use TCG (software, slow)"
fi

ui_print " "
ui_print "  Done. flipos does NOT start at boot."
ui_print "  Start/stop: press this module's Action button,"
ui_print "  or open its WebUI, or: su -c sh $F/bin/flipos start"
ui_print "  Viewer: your browser opens noVNC automatically;"
ui_print "  or a VNC app at 127.0.0.1:5901."
ui_print " "
