#!/system/bin/sh
# Boot flipos in QEMU inside the Debian chroot. Run as root (KernelSU).
# View it with a FOSS VNC client (e.g. AVNC) at 127.0.0.1:5901.
# SAFETY: bind mounts live under /data/flipos/debian; QEMU disks are files under
# /data/flipos/images. Nothing here touches a partition or the host /system.
set -eu
FLIPOS=/data/flipos
CHROOT="$FLIPOS/debian"
IMG="$FLIPOS/images"
RES="${RES:-540x1320}"; XRES="${RES%x*}"; YRES="${RES#*x}"
MEM_MB="${MEM_MB:-2048}"; SMP="${SMP:-4}"

# Acceleration: use KVM when /dev/kvm is usable (a custom kernel can expose it on
# the Z Flip 4), else fall back to TCG. Override with ACCEL=tcg to force.
if [ "${ACCEL:-}" = "tcg" ] || [ ! -e /dev/kvm ] || [ ! -w /dev/kvm ]; then
  ACCEL_ARGS="-accel tcg,thread=multi,tb-size=512 -cpu max"; ACCEL_NAME="TCG (software)"
else
  ACCEL_ARGS="-accel kvm -cpu host"; ACCEL_NAME="KVM (hardware)"
fi

[ -f "$IMG/Image" ] || { echo "no $IMG/Image — install the module with guest images, or copy them there"; exit 1; }
[ -x "$CHROOT/usr/bin/qemu-system-aarch64" ] || { echo "qemu missing — run bootstrap-chroot.sh first"; exit 1; }

# data.img (ext4) — created once, inside /data (a file, not a block device).
if [ ! -f "$IMG/data.img" ]; then
  chroot "$CHROOT" qemu-img create -f raw /images/data.img 2G
  chroot "$CHROOT" sh -c "mkfs.ext4 -qF -L flipos-data /images/data.img" || true
fi

mkdir -p "$CHROOT/images"
mount -o bind "$IMG" "$CHROOT/images"
for m in proc sys dev dev/pts; do mkdir -p "$CHROOT/$m"; done
mount -t proc proc "$CHROOT/proc"; mount -t sysfs sys "$CHROOT/sys"
mount -o bind /dev "$CHROOT/dev"; mount -o bind /dev/pts "$CHROOT/dev/pts"
cleanup() { umount "$CHROOT/images" "$CHROOT/dev/pts" "$CHROOT/dev" "$CHROOT/sys" "$CHROOT/proc" 2>/dev/null || true; }
trap cleanup EXIT INT TERM

echo "flipos: booting with $ACCEL_NAME. Connect a VNC client to 127.0.0.1:5901"
chroot "$CHROOT" qemu-system-aarch64 \
  -M virt,gic-version=3 $ACCEL_ARGS -smp "$SMP" -m "$MEM_MB" \
  -kernel /images/Image \
  -append "root=/dev/vda ro rootfstype=erofs console=ttyAMA0" \
  -drive file=/images/rootfs.erofs,if=none,format=raw,id=hd0,readonly=on -device virtio-blk-pci,drive=hd0 \
  -drive file=/images/data.img,if=none,format=raw,id=hd1,cache=writeback,aio=threads -device virtio-blk-pci,drive=hd1 \
  -device virtio-gpu-pci,xres="$XRES",yres="$YRES" \
  -device virtio-tablet-pci -device virtio-keyboard-pci -device virtio-rng-pci -device virtio-balloon-pci \
  -netdev user,id=n0,hostfwd=tcp::2222-:22 -device virtio-net-pci,netdev=n0 \
  -display vnc=127.0.0.1:1 -serial mon:stdio
