#!/usr/bin/env bash
# Assemble the flashable KernelSU module zip (out/flipos-<ver>.zip) from:
#   release/kernelsu-module/          module skeleton + controller + Action/WebUI
#   out/images/{Image,rootfs.erofs}   the flipos guest            (make image)
#   out/images/android-host/          QEMU + glibc for Android    (make image)
#   noVNC, pinned by git commit       the in-browser VNC viewer
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MOD="$ROOT/release/kernelsu-module"
IMAGES="$ROOT/out/images"
VER="$(sed -n 's/^version=//p' "$MOD/module.prop")"
QEMU_VERSION="$(sed -n 's/^QEMU_VERSION = //p' "$ROOT/buildroot/package/qemu/qemu.mk")"

# noVNC v1.7.0 (MPL-2.0), pinned to the tag's commit — git verifies content by hash.
NOVNC_URL=https://github.com/novnc/noVNC.git
NOVNC_TAG=v1.7.0
NOVNC_COMMIT=63107bd06d9e1f6136ff21aeda8cd62cbf0d433e

command -v zip >/dev/null || { echo "need 'zip'"; exit 1; }
for f in Image rootfs.erofs android-host/bin/qemu-system-aarch64 android-host/lib/ld-linux-aarch64.so.1; do
  [ -e "$IMAGES/$f" ] || { echo "missing out/images/$f — run 'make image' first"; exit 1; }
done

STAGE="$(mktemp -d)"; trap 'rm -rf "$STAGE"' EXIT
cp -a "$MOD/." "$STAGE/"
rm -f "$STAGE/payload/images/.gitkeep"
mkdir -p "$STAGE/payload/images"
cp "$IMAGES/Image" "$IMAGES/rootfs.erofs" "$STAGE/payload/images/"
cp -a "$IMAGES/android-host" "$STAGE/payload/host"
echo "$QEMU_VERSION" > "$STAGE/payload/host/QEMU_VERSION"

NOVNC="$ROOT/dl/novnc-$NOVNC_COMMIT"
if [ ! -d "$NOVNC" ]; then
  git clone -q --depth 1 --branch "$NOVNC_TAG" "$NOVNC_URL" "$NOVNC.tmp"
  mv "$NOVNC.tmp" "$NOVNC"
fi
got="$(git -C "$NOVNC" rev-parse HEAD)"
[ "$got" = "$NOVNC_COMMIT" ] || { echo "noVNC commit mismatch: $got != $NOVNC_COMMIT"; exit 1; }
mkdir -p "$STAGE/payload/novnc"
( cd "$NOVNC" && cp -a app core vendor vnc.html vnc_lite.html defaults.json mandatory.json package.json \
    LICENSE.txt AUTHORS "$STAGE/payload/novnc/" )
cp "$STAGE/payload/novnc/vnc.html" "$STAGE/payload/novnc/index.html"   # "/" works too
# flipos patch: don't request QEMU's LED-state events. With them, noVNC "syncs"
# Caps Lock to the local keyboard - and Android soft keyboards report Caps Lock
# wrongly, so noVNC switched it on in the guest and everything typed in CAPS.
# Without them QEMU's own lock-key-sync handles case correctly.
sed -i 's|^\( *\)encs.push(encodings.pseudoEncodingQEMULedEvent);|\1// flipos: LED-state sync disabled (see scripts/mkrelease.sh)|' \
  "$STAGE/payload/novnc/core/rfb.js"
grep -q 'flipos: LED-state sync disabled' "$STAGE/payload/novnc/core/rfb.js" \
  || { echo "noVNC caps-lock patch did not apply"; exit 1; }

mkdir -p "$ROOT/out"
OUT="$ROOT/out/flipos-$VER.zip"; rm -f "$OUT"
# Reproducible zip: fixed order and mtimes.
find "$STAGE" -exec touch -h -d "@${SOURCE_DATE_EPOCH:-1704067200}" {} +
( cd "$STAGE" && find . -mindepth 1 | LC_ALL=C sort | sed 's|^\./||' | zip -X -q "$OUT" -@ )
echo "built $OUT ($(du -h "$OUT" | cut -f1))"; ( cd "$ROOT/out" && sha256sum "flipos-$VER.zip" )
