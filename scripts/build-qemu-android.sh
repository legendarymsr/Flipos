#!/usr/bin/env bash
# Build a NATIVE Android (bionic) qemu-system-aarch64 with the Android NDK — so
# flipos runs via QEMU DIRECTLY on Android (KVM in your kernel), no Debian chroot.
#
# Runs on a Linux x86_64 build box (this cloud VM, a PC, or the phone's own build
# env). Output → out/android-qemu/{qemu-system-aarch64, qemu-img, lib/*.so}, which
# `make release` bundles into the flashable zip (or copy to /data/flipos).
#
# HONESTY: all versions below are pinned to real, download-verified releases, but
# this full recipe is NOT verified end-to-end from the authoring environment. A
# from-scratch NDK qemu build can need small patches (qemu assumes a few glibc-isms
# bionic lacks). If a step fails, the battle-tested fallback is Termux's prebuilt
# `qemu-system-aarch64-headless` (bootstrap-qemu.sh pulls it) — this script is the
# "no Termux, build it yourself" path. Iterate per the error; don't assume success.
set -euo pipefail

# ---- pinned versions (verified to resolve upstream; bump freely) ----
NDK_VER="${NDK_VER:-r27d}"               # Android NDK (LTS line)
API="${API:-28}"                          # min Android API level
QEMU_VER="${QEMU_VER:-9.2.0}"
GLIB_SERIES="2.82"; GLIB_VER="${GLIB_VER:-2.82.4}"
PIXMAN_VER="${PIXMAN_VER:-0.44.2}"
SLIRP_VER="${SLIRP_VER:-4.8.0}"
FFI_VER="${FFI_VER:-3.4.6}"
PCRE2_VER="${PCRE2_VER:-10.44}"

TARGET=aarch64-linux-android
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="${WORK:-$ROOT/out/qemu-build}"
PREFIX="$WORK/prefix"                      # staging for the cross-built deps
OUT="$ROOT/out/android-qemu"
JOBS="${JOBS:-$(nproc)}"
mkdir -p "$WORK/src" "$PREFIX" "$OUT/lib"

need() { command -v "$1" >/dev/null || { echo "need '$1' (apt install $2)"; exit 1; }; }
need meson meson; need ninja ninja-build; need pkg-config pkg-config
need curl curl; need unzip unzip; need python3 python3

fetch() { # url -> extracted dir name echoed
  local url="$1" f="$WORK/src/$(basename "$1")"
  [ -f "$f" ] || curl -fL --retry 3 -o "$f" "$url"
  case "$f" in
    *.zip) unzip -q -o "$f" -d "$WORK/src" ;;
    *) tar -C "$WORK/src" -xf "$f" ;;
  esac
}

# ---- 1. Android NDK + toolchain env ----
if [ -z "${NDK_HOME:-}" ]; then
  echo "== fetching Android NDK $NDK_VER"
  fetch "https://dl.google.com/android/repository/android-ndk-$NDK_VER-linux.zip"
  NDK_HOME="$WORK/src/android-ndk-$NDK_VER"
fi
TC="$NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64"
export CC="$TC/bin/${TARGET}${API}-clang"
export CXX="$TC/bin/${TARGET}${API}-clang++"
export AR="$TC/bin/llvm-ar" RANLIB="$TC/bin/llvm-ranlib" STRIP="$TC/bin/llvm-strip"
export LD="$TC/bin/ld" NM="$TC/bin/llvm-nm"
export PKG_CONFIG_LIBDIR="$PREFIX/lib/pkgconfig"
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig"
export CPPFLAGS="-I$PREFIX/include" LDFLAGS="-L$PREFIX/lib"
[ -x "$CC" ] || { echo "NDK clang not found at $CC — set NDK_HOME"; exit 1; }

# meson cross file for Android/arm64
XFILE="$WORK/android-arm64.ini"
cat > "$XFILE" <<EOF
[binaries]
c = '$CC'
cpp = '$CXX'
ar = '$AR'
strip = '$STRIP'
pkg-config = 'pkg-config'
[host_machine]
system = 'android'
cpu_family = 'aarch64'
cpu = 'aarch64'
endian = 'little'
EOF

meson_dep() { # srcdir  extra-meson-args...
  local src="$1"; shift
  ( cd "$src" && meson setup _b --cross-file "$XFILE" --prefix="$PREFIX" \
      --buildtype=release --default-library=shared "$@" \
    && ninja -C _b -j"$JOBS" && ninja -C _b install )
}

# ---- 2. deps: libffi, pcre2, glib, pixman, libslirp ----
echo "== libffi $FFI_VER"
fetch "https://github.com/libffi/libffi/releases/download/v$FFI_VER/libffi-$FFI_VER.tar.gz"
( cd "$WORK/src/libffi-$FFI_VER" && ./configure --host="$TARGET" --prefix="$PREFIX" \
    --disable-static --enable-shared && make -j"$JOBS" && make install )

echo "== pcre2 $PCRE2_VER"
fetch "https://github.com/PCRE2Project/pcre2/releases/download/pcre2-$PCRE2_VER/pcre2-$PCRE2_VER.tar.gz"
( cd "$WORK/src/pcre2-$PCRE2_VER" && ./configure --host="$TARGET" --prefix="$PREFIX" \
    --disable-static --enable-shared && make -j"$JOBS" && make install )

echo "== glib $GLIB_VER"
fetch "https://download.gnome.org/sources/glib/$GLIB_SERIES/glib-$GLIB_VER.tar.xz"
meson_dep "$WORK/src/glib-$GLIB_VER" -Dtests=false -Dselinux=disabled -Dlibmount=disabled \
  -Dnls=disabled -Dman-pages=disabled -Dintrospection=disabled -Dglib_debug=disabled

echo "== pixman $PIXMAN_VER"
fetch "https://www.cairographics.org/releases/pixman-$PIXMAN_VER.tar.gz"
meson_dep "$WORK/src/pixman-$PIXMAN_VER" -Dtests=disabled -Ddemos=disabled -Dgtk=disabled

echo "== libslirp $SLIRP_VER (user-mode networking)"
fetch "https://gitlab.freedesktop.org/slirp/libslirp/-/archive/v$SLIRP_VER/libslirp-v$SLIRP_VER.tar.gz"
meson_dep "$WORK/src/libslirp-v$SLIRP_VER"

# ---- 3. qemu ----
echo "== qemu $QEMU_VER"
fetch "https://download.qemu.org/qemu-$QEMU_VER.tar.xz"
cd "$WORK/src/qemu-$QEMU_VER"
./configure \
  --cc="$CC" --cxx="$CXX" --host-cc=cc \
  --cross-prefix="$TC/bin/llvm-" \
  --target-list=aarch64-softmmu \
  --enable-kvm --enable-slirp --enable-tcg \
  --disable-docs --disable-gtk --disable-sdl --disable-vnc-sasl \
  --disable-werror --disable-install-blobs \
  --extra-cflags="-I$PREFIX/include" --extra-ldflags="-L$PREFIX/lib"
ninja -C build -j"$JOBS" qemu-system-aarch64 qemu-img

# ---- 4. collect the native binary + the non-system libs it needs ----
cp build/qemu-system-aarch64 build/qemu-img "$OUT/"
"$STRIP" "$OUT/qemu-system-aarch64" "$OUT/qemu-img" 2>/dev/null || true
# bundle our built libs + the NDK C++ runtime (bionic libc/libm/libdl are on-device)
cp -a "$PREFIX/lib/"*.so* "$OUT/lib/" 2>/dev/null || true
cp -a "$TC/sysroot/usr/lib/$TARGET/libc++_shared.so" "$OUT/lib/" 2>/dev/null || true

echo
echo "== built native Android qemu → $OUT"
ls -la "$OUT" "$OUT/lib" 2>/dev/null | sed 's/^/   /'
echo "Bundle into the release: leave it in out/android-qemu and run 'make release'."
echo "Or copy to the phone: /data/flipos/bin/qemu-system-aarch64 (+ lib/* to /data/flipos/qemu/lib)."
