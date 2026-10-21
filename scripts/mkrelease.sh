#!/usr/bin/env bash
# Assemble the flashable KernelSU module zip from the module skeleton + the built
# flipos guest images (out/images). Runs on any host; produces out/flipos-<ver>.zip.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MOD="$ROOT/release/kernelsu-module"
VER="$(sed -n 's/^version=//p' "$MOD/module.prop")"
command -v zip >/dev/null || { echo "need 'zip'"; exit 1; }

STAGE="$(mktemp -d)"; trap 'rm -rf "$STAGE"' EXIT
cp -a "$MOD/." "$STAGE/"
rm -f "$STAGE/payload/images/.gitkeep"

if [ -f "$ROOT/out/images/Image" ] && [ -f "$ROOT/out/images/rootfs.erofs" ]; then
  mkdir -p "$STAGE/payload/images"
  cp "$ROOT/out/images/Image" "$ROOT/out/images/rootfs.erofs" "$STAGE/payload/images/"
  echo "bundled guest images from out/images"
else
  echo "WARN: out/images not found — zip ships WITHOUT guest images (drop them into /data/flipos/images on device)"
fi

# Optional: bundle a native Android qemu from out/android-qemu/
# (qemu-system-aarch64 [+ qemu-img] [+ lib/*.so for a dynamic build]). If absent,
# the device obtains one via bootstrap-qemu.sh (Termux, or a build you supply).
if [ -x "$ROOT/out/android-qemu/qemu-system-aarch64" ]; then
  mkdir -p "$STAGE/payload/qemu/lib"
  cp "$ROOT/out/android-qemu/qemu-system-aarch64" "$STAGE/payload/qemu/"
  [ -x "$ROOT/out/android-qemu/qemu-img" ] && cp "$ROOT/out/android-qemu/qemu-img" "$STAGE/payload/qemu/" || true
  cp "$ROOT/out/android-qemu/lib/"*.so* "$STAGE/payload/qemu/lib/" 2>/dev/null || true
  echo "bundled native qemu from out/android-qemu"
else
  echo "note: no out/android-qemu — zip ships WITHOUT qemu (device runs bootstrap-qemu.sh)"
fi

mkdir -p "$ROOT/out"
OUT="$ROOT/out/flipos-$VER.zip"; rm -f "$OUT"
( cd "$STAGE" && zip -r -X "$OUT" . >/dev/null )
echo "built $OUT"; ( cd "$ROOT/out" && sha256sum "flipos-$VER.zip" )
