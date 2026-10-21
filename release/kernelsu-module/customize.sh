# flipos KernelSU module — install step. Runs in the manager's installer env.
# SAFETY: copies payload to /data only. No partition writes, no system overlay,
# no dd/mkfs/fastboot, no remount of /system or /vendor.
SKIPMOUNT=true          # we ship no system/ overlay; nothing is mounted over the host
ASH_STANDALONE=1

FLIPOS_DIR=/data/flipos

ui_print "- flipos: installing to $FLIPOS_DIR (systemless, /data only)"
mkdir -p "$FLIPOS_DIR/images" "$FLIPOS_DIR/bin"

# flipos guest images bundled in the zip (payload/), copied out to writable /data.
if [ -d "$MODPATH/payload/images" ]; then
  cp -f "$MODPATH/payload/images/"* "$FLIPOS_DIR/images/" 2>/dev/null || true
  ui_print "- flipos: guest images copied"
else
  ui_print "- flipos: NOTE no guest images bundled — put Image + rootfs.erofs in $FLIPOS_DIR/images"
fi

# launcher + chroot bootstrap scripts
cp -f "$MODPATH/payload/run-flipos.sh"      "$FLIPOS_DIR/bin/" 2>/dev/null || true
cp -f "$MODPATH/payload/bootstrap-chroot.sh" "$FLIPOS_DIR/bin/" 2>/dev/null || true
chmod 0755 "$FLIPOS_DIR/bin/"*.sh 2>/dev/null || true

set_perm_recursive "$FLIPOS_DIR" 0 0 0755 0644 2>/dev/null || true

ui_print " "
ui_print "  flipos installed. It does NOT start at boot."
ui_print "  First time, set up the Debian+QEMU chroot (needs network):"
ui_print "      su -c sh $FLIPOS_DIR/bin/bootstrap-chroot.sh"
ui_print "  Then boot the OS and view it over VNC at 127.0.0.1:5901:"
ui_print "      su -c sh $FLIPOS_DIR/bin/run-flipos.sh"
ui_print " "
