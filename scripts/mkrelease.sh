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

mkdir -p "$ROOT/out"
OUT="$ROOT/out/flipos-$VER.zip"; rm -f "$OUT"
( cd "$STAGE" && zip -r -X "$OUT" . >/dev/null )
echo "built $OUT"; ( cd "$ROOT/out" && sha256sum "flipos-$VER.zip" )
