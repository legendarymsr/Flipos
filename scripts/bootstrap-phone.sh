#!/usr/bin/env bash
# Run INSIDE the Z Flip 4 Debian arm64 chroot to build + run flipos natively.
# Native aarch64 build (no cross); the phone has the storage (128 GB) the cloud
# build host lacked. Safe: touches only files under this repo and the chroot.
set -euo pipefail
cd "$(dirname "$0")/.."

echo "== flipos phone bootstrap =="
[ "$(uname -m)" = "aarch64" ] || { echo "WARN: expected aarch64 (the phone chroot); got $(uname -m)"; }

# 1. Host build deps (Debian). qemu-system-arm provides qemu-system-aarch64.
if command -v apt-get >/dev/null 2>&1; then
  echo "installing build deps (needs root in the chroot)…"
  apt-get update -qq
  apt-get install -y --no-install-recommends \
    build-essential git python3 file wget cpio unzip rsync bc \
    libncurses-dev bzip2 ccache meson ninja-build pkg-config \
    qemu-system-arm qemu-utils e2fsprogs
else
  echo "no apt-get — install Buildroot's host deps + qemu manually."; exit 1
fi

# 2. Buildroot submodule (pinned to the recorded commit).
git submodule update --init --recursive buildroot

# 3. Guard against Android killing the build; log; build resumably.
command -v termux-wake-lock >/dev/null 2>&1 && termux-wake-lock || true
mkdir -p logs
echo "building — keep this in tmux; logging to logs/build.log"
# -j modest so 8 GB RAM doesn't OOM (each gcc ~0.5-1 GB).
JOBS="${JOBS:-$(( $(nproc) > 6 ? 6 : $(nproc) ))}"
nice -n 10 make image BR2_JLEVEL="$JOBS" 2>&1 | tee logs/build.log

echo "== done — images in out/images/. Run: make run =="
