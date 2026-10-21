#!/system/bin/sh
# Set up a MINIMAL Debian arm64 chroot under /data/flipos/debian and install
# QEMU inside it. Run once, as root (KernelSU). Needs network.
# SAFETY: writes only under /data/flipos. No partitions, no /system, no dd/mkfs.
set -eu
FLIPOS=/data/flipos
CHROOT="$FLIPOS/debian"
SUITE="${SUITE:-bookworm}"
MIRROR="${MIRROR:-http://deb.debian.org/debian}"

mkdir -p "$CHROOT"
echo "flipos: bootstrapping Debian ($SUITE) into $CHROOT"

if [ -x "$(command -v debootstrap || true)" ]; then
  # Cleanest, truly-minimal Debian — no prebuilt blob.
  debootstrap --arch=arm64 --variant=minbase "$SUITE" "$CHROOT" "$MIRROR"
elif [ -n "${DEBIAN_ROOTFS_URL:-}" ]; then
  # Fallback: a pinned minimal rootfs tarball you provide + verify.
  echo "flipos: fetching $DEBIAN_ROOTFS_URL"
  tmp="$FLIPOS/rootfs.tar.xz"
  wget -O "$tmp" "$DEBIAN_ROOTFS_URL"
  if [ -n "${DEBIAN_ROOTFS_SHA256:-}" ]; then
    echo "$DEBIAN_ROOTFS_SHA256  $tmp" | sha256sum -c - || { echo "SHA mismatch — aborting"; exit 1; }
  else
    echo "WARNING: DEBIAN_ROOTFS_SHA256 unset — cannot verify the rootfs. Set it and re-run."; exit 1
  fi
  tar -xJf "$tmp" -C "$CHROOT"; rm -f "$tmp"
else
  echo "ERROR: no debootstrap and no DEBIAN_ROOTFS_URL set."
  echo "Install debootstrap (e.g. in Termux: pkg install debootstrap), or set"
  echo "DEBIAN_ROOTFS_URL + DEBIAN_ROOTFS_SHA256 to a minimal Debian arm64 rootfs."
  exit 1
fi

# DNS + minimal mounts to run apt inside the chroot.
cp /etc/resolv.conf "$CHROOT/etc/resolv.conf" 2>/dev/null || echo "nameserver 1.1.1.1" > "$CHROOT/etc/resolv.conf"
for m in proc sys dev dev/pts; do mkdir -p "$CHROOT/$m"; done
mount -t proc proc "$CHROOT/proc"
mount -t sysfs sys "$CHROOT/sys"
mount -o bind /dev "$CHROOT/dev"
mount -o bind /dev/pts "$CHROOT/dev/pts"

echo "flipos: installing qemu inside the chroot"
chroot "$CHROOT" /bin/sh -c "apt-get update -qq && apt-get install -y --no-install-recommends qemu-system-arm qemu-utils e2fsprogs"

# Unmount (these binds are under /data, not host mounts, but clean up anyway).
umount "$CHROOT/dev/pts" "$CHROOT/dev" "$CHROOT/sys" "$CHROOT/proc" 2>/dev/null || true
echo "flipos: chroot ready. Boot with: su -c sh $FLIPOS/bin/run-flipos.sh"
