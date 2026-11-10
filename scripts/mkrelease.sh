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
# flipos patch: phone keyboards "smart-punctuate" - a typed - can arrive as
# U+2010/2011/2012/2013/2014/2212 and quotes as curly ones, which the shell
# doesn't understand (`flipos‐pkg: not found`). Map them to ASCII before sending.
# Keysyms: legacy XK_endash/emdash/quote marks, else 0x1000000 + Unicode.
sed -i 's|^\(    sendKey(keysym, code, down) {\)$|\1\
        // flipos: map look-alike punctuation from phone keyboards to ASCII\
        keysym = ({ 0x0aaa: 0x2d, 0x0aa9: 0x2d, 0x0abb: 0x2d, 0x00ad: 0x2d, 0x1002010: 0x2d, 0x1002011: 0x2d,\
                    0x1002012: 0x2d, 0x1002013: 0x2d, 0x1002014: 0x2d, 0x1002212: 0x2d,\
                    0x0ad0: 0x27, 0x0ad1: 0x27, 0x1002018: 0x27, 0x1002019: 0x27,\
                    0x0ad2: 0x22, 0x0ad3: 0x22, 0x100201c: 0x22, 0x100201d: 0x22 })[keysym] ?? keysym;|' \
  "$STAGE/payload/novnc/core/rfb.js"
grep -q 'flipos: map look-alike punctuation' "$STAGE/payload/novnc/core/rfb.js" \
  || { echo "noVNC punctuation patch did not apply"; exit 1; }
# flipos patch: characters typed on a phone (virtual) keyboard reach sendKey()
# as a bare keysym with no key code; QEMU maps the keysym to the key but NOT to
# Shift, so | > & ! : " ? ( ) _ + etc. arrived unshifted (`a|b` became `a\b`).
# For those, hold Shift around the character. Physical keyboards pass a key
# code and send their own Shift, so they are unaffected.
sed -i '/^    sendKey(keysym, code, down) {$/,/_viewOnly) { return; }$/ s|^\(        if (this._rfbConnectionState !== .connected. \|\| this._viewOnly) { return; }\)$|\1\
        // flipos: shifted characters from phone keyboards need an explicit Shift\
        if (code === undefined \&\& down === undefined \&\&\
            ((keysym >= 0x41 \&\& keysym <= 0x5a) \|\| "~!@#$%^\&*()_+{}\|:\\"<>?".includes(String.fromCharCode(keysym)))) {\
            this.sendKey(0xffe1, "ShiftLeft", true);\
            this.sendKey(keysym, code, true);\
            this.sendKey(keysym, code, false);\
            this.sendKey(0xffe1, "ShiftLeft", false);\
            return;\
        }|' "$STAGE/payload/novnc/core/rfb.js"
grep -q 'flipos: shifted characters from phone keyboards' "$STAGE/payload/novnc/core/rfb.js" \
  || { echo "noVNC shift patch did not apply"; exit 1; }

mkdir -p "$ROOT/out"
OUT="$ROOT/out/flipos-$VER.zip"; rm -f "$OUT"
# Reproducible zip: fixed order and mtimes.
find "$STAGE" -exec touch -h -d "@${SOURCE_DATE_EPOCH:-1704067200}" {} +
( cd "$STAGE" && find . -mindepth 1 | LC_ALL=C sort | sed 's|^\./||' | zip -X -q "$OUT" -@ )
echo "built $OUT ($(du -h "$OUT" | cut -f1))"; ( cd "$ROOT/out" && sha256sum "flipos-$VER.zip" )
