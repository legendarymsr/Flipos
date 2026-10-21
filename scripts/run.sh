#!/usr/bin/env bash
# flipos launcher — boots out/images under QEMU aarch64.
# Auto-detects KVM vs TCG and a display backend, sizes RAM/CPU from the host,
# and creates data.img on first run. Honest about what works where (see
# docs/LIMITATIONS.md): on an x86_64 host this is TCG-only emulation.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMG="$ROOT/out/images"
KERNEL="$IMG/Image"
ROOTFS="$IMG/rootfs.erofs"
DATA="$ROOT/out/data.img"
QEMU="${QEMU:-qemu-system-aarch64}"

RES="540x1320"                        # default portrait (half Flip-4 inner)
HEADLESS=0; FORCE_ACCEL=""; DEBUG=0
while [ $# -gt 0 ]; do case "$1" in
  --headless) HEADLESS=1 ;;
  --res) RES="$2"; shift ;;
  --kvm) FORCE_ACCEL=kvm ;;
  --tcg) FORCE_ACCEL=tcg ;;
  --debug) DEBUG=1 ;;
  *) echo "unknown arg: $1" >&2; exit 2 ;;
esac; shift; done
XRES="${RES%x*}"; YRES="${RES#*x}"

command -v "$QEMU" >/dev/null 2>&1 || { echo "ERROR: $QEMU not installed (apt install qemu-system-arm)"; exit 1; }
[ -f "$KERNEL" ] || { echo "ERROR: no kernel at $KERNEL — run 'make image' first"; exit 1; }

# --- acceleration ---
ACCEL="tcg,thread=multi,tb-size=512"; CPU="max"
if [ "$FORCE_ACCEL" = kvm ] || { [ -z "$FORCE_ACCEL" ] && [ -w /dev/kvm ]; }; then
  if [ "$(uname -m)" = "aarch64" ] && [ -e /dev/kvm ]; then ACCEL="kvm"; CPU="host"; fi
fi
[ "$FORCE_ACCEL" = tcg ] && { ACCEL="tcg,thread=multi,tb-size=512"; CPU="max"; }

# --- sizing: leave >=2 cores + half RAM for the host (Android needs headroom) ---
HOST_CORES="$(nproc)"; SMP="$(( HOST_CORES>3 ? HOST_CORES-2 : 2 ))"
HOST_MB="$(awk '/MemAvailable/{print int($2/1024)}' /proc/meminfo)"
MEM_MB="$(( HOST_MB/2 ))"; [ "$MEM_MB" -gt 2048 ] && MEM_MB=2048; [ "$MEM_MB" -lt 1024 ] && MEM_MB=1024

# --- data image (ext4), created once ---
if [ ! -f "$DATA" ]; then
  echo "creating data.img (2G ext4)…"
  qemu-img create -f raw "$DATA" 2G >/dev/null
  mkfs.ext4 -q -L flipos-data "$DATA" || echo "WARN: mkfs.ext4 missing; data fs not formatted"
fi

# --- display backend: Termux:X11 → Wayland → VNC → none ---
DISPLAY_ARGS="-display vnc=127.0.0.1:1"   # safe default (view with a FOSS VNC client)
if [ "$HEADLESS" = 1 ]; then
  DISPLAY_ARGS="-display none"
elif [ -n "${WAYLAND_DISPLAY:-}" ]; then
  DISPLAY_ARGS="-display gtk,gl=off"
elif [ -S "${TMPDIR:-/tmp}/.X11-unix/X0" ] || [ -n "${DISPLAY:-}" ]; then
  DISPLAY_ARGS="-display sdl,gl=off"
fi

# --- keep Android from killing us (we have root on the phone) ---
echo "tip: run inside tmux + termux-wake-lock; this script lowers its own oom_score_adj."
echo -1000 > /proc/self/oom_score_adj 2>/dev/null || true

DBG=""; [ "$DEBUG" = 1 ] && DBG="-monitor unix:$ROOT/out/qemu-mon.sock,server,nowait -s"

set -x
exec "$QEMU" \
  -M virt,gic-version=3 -accel "$ACCEL" -cpu "$CPU" -smp "$SMP" -m "$MEM_MB" \
  -kernel "$KERNEL" \
  -append "root=/dev/vda ro rootfstype=erofs console=ttyAMA0 quiet" \
  -drive file="$ROOTFS",if=none,format=raw,id=hd0,readonly=on \
    -device virtio-blk-pci,drive=hd0 \
  -drive file="$DATA",if=none,format=raw,id=hd1,cache=writeback,aio=threads \
    -device virtio-blk-pci,drive=hd1 \
  -device virtio-gpu-pci,xres="$XRES",yres="$YRES" \
  -device virtio-tablet-pci -device virtio-keyboard-pci \
  -device virtio-rng-pci -device virtio-balloon-pci \
  -netdev user,id=n0,hostfwd=tcp::2222-:22 -device virtio-net-pci,netdev=n0 \
  -serial mon:stdio $DISPLAY_ARGS $DBG
