#!/system/bin/sh
# Boot flipos in QEMU running DIRECTLY on Android (no Debian chroot). Run as root
# (KernelSU). View it with a FOSS VNC client (e.g. AVNC) at 127.0.0.1:5901.
# SAFETY: QEMU is a normal userspace process; its "disks" are files under
# /data/flipos/images. Nothing here touches a partition or the host /system,
# and nothing is mounted over Android.
set -eu
FLIPOS=/data/flipos
IMG="$FLIPOS/images"
QEMU="$FLIPOS/bin/qemu-system-aarch64"
QIMG="$FLIPOS/bin/qemu-img"
# If qemu was built dynamic with bundled libs, they live here; harmless if static.
export LD_LIBRARY_PATH="$FLIPOS/qemu/lib:${LD_LIBRARY_PATH:-}"
# Writable scratch for QEMU (it must not assume a world-writable /tmp on Android).
export TMPDIR="$FLIPOS/tmp"; mkdir -p "$TMPDIR"

RES="${RES:-540x1320}"; XRES="${RES%x*}"; YRES="${RES#*x}"
MEM_MB="${MEM_MB:-2048}"; SMP="${SMP:-4}"

# Acceleration: KVM when /dev/kvm is usable (your custom kernel exposes it),
# else TCG. ACCEL=tcg forces software.
if [ "${ACCEL:-}" = "tcg" ] || [ ! -e /dev/kvm ] || [ ! -w /dev/kvm ]; then
  ACCEL_ARGS="-accel tcg,thread=multi,tb-size=512 -cpu max"; ACCEL_NAME="TCG (software)"
else
  ACCEL_ARGS="-accel kvm -cpu host"; ACCEL_NAME="KVM (hardware)"
fi

[ -x "$QEMU" ] || { echo "no qemu at $QEMU — run: su -c sh $FLIPOS/bin/bootstrap-qemu.sh"; exit 1; }
[ -f "$IMG/Image" ] || { echo "no $IMG/Image — install the module with guest images, or copy them there"; exit 1; }

# data.img (ext4) is a plain file under /data; the flipos guest formats it on
# first boot if it has no filesystem, so no host mkfs is needed.
if [ ! -f "$IMG/data.img" ]; then
  if [ -x "$QIMG" ]; then "$QIMG" create -f raw "$IMG/data.img" 2G >/dev/null
  else truncate -s 2G "$IMG/data.img"; fi
  echo "flipos: created empty data.img (guest formats it on first boot)"
fi

echo "flipos: booting with $ACCEL_NAME. Connect a VNC client to 127.0.0.1:5901"
exec "$QEMU" \
  -M virt,gic-version=3 $ACCEL_ARGS -smp "$SMP" -m "$MEM_MB" \
  -kernel "$IMG/Image" \
  -append "root=/dev/vda ro rootfstype=erofs console=ttyAMA0" \
  -drive file="$IMG/rootfs.erofs",if=none,format=raw,id=hd0,readonly=on -device virtio-blk-pci,drive=hd0 \
  -drive file="$IMG/data.img",if=none,format=raw,id=hd1,cache=writeback,aio=threads -device virtio-blk-pci,drive=hd1 \
  -device virtio-gpu-pci,xres="$XRES",yres="$YRES" \
  -device virtio-tablet-pci -device virtio-keyboard-pci -device virtio-rng-pci -device virtio-balloon-pci \
  -netdev user,id=n0,hostfwd=tcp::2222-:22 -device virtio-net-pci,netdev=n0 \
  -display vnc=127.0.0.1:1 -serial mon:stdio
