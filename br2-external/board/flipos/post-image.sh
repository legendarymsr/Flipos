#!/bin/sh
# Assembles out/images via genimage (root EROFS + SHA256SUMS). data.img is NOT
# created here — scripts/run.sh makes it on first boot if missing.
set -eu
BOARD_DIR="$(dirname "$0")"
GENIMAGE_CFG="${BOARD_DIR}/genimage.cfg"
# Buildroot provides support/scripts/genimage.sh in $BUILD_DIR; invoked by BR.
( cd "${BINARIES_DIR}" && sha256sum Image rootfs.erofs > SHA256SUMS 2>/dev/null ) || true
exit 0
