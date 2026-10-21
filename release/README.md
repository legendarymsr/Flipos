# flipos — KernelSU release module

A **systemless** KernelSU module that runs flipos in QEMU **directly on Android**
(no Debian chroot). It is deliberately inert toward the host.

## Safety (what it will and won't do)
- Installs to `/data/adb/modules/flipos/` and keeps everything else under
  `/data/flipos/`.
- Ships **no `system/` overlay**, defines **no `service.sh`/`post-fs-data.sh`**,
  so it **auto-runs nothing at boot** and overlays nothing onto Android.
- **Never** writes a partition or block device, never runs `dd`/`mkfs`/`fastboot`,
  never touches `/system`, `/vendor`, `boot`, `vbmeta`, or `/dev/block/*`.
- QEMU is a **plain userspace process**; its "disks" are files
  (`/data/flipos/images/*`). Nothing is mounted over Android.
- **Uninstall** removes the module and `rm -rf /data/flipos` — host untouched.

## Install
1. Build the guest images on the phone (`scripts/bootstrap-phone.sh`, then
   `make image`) and package: `make release` → `out/flipos-<ver>.zip`.
   (Or install the zip without images and drop `Image` + `rootfs.erofs` into
   `/data/flipos/images/` yourself.)
2. KernelSU app → **Modules → Install from storage** → pick the zip. Reboot.

## Provide a native qemu (once)
QEMU runs **directly on Android's kernel** — no chroot. Get a native
`qemu-system-aarch64` one of these ways (the module tries them in order):
```sh
su -c sh /data/flipos/bin/bootstrap-qemu.sh
```
- **Bundled** — if the release zip was built with `out/android-qemu/`, it's
  already installed, nothing to do.
- **Termux** (recommended if you don't bundle one): `pkg install
  qemu-system-aarch64-headless` in Termux; the script copies it + its libs out.
  Termux binaries are native Android (bionic), not a chroot.
- **Your own build**: `QEMU_SRC=/path/to/qemu-system-aarch64 su -c sh
  /data/flipos/bin/bootstrap-qemu.sh`. A static (musl) qemu is a single
  relocatable binary that runs on the Android kernel with no extra libs.

## Boot flipos
```sh
su -c sh /data/flipos/bin/run-flipos.sh
```
Then open a FOSS VNC client (e.g. **AVNC** from F-Droid) to **127.0.0.1:5901** to
see the phone UI. `RES=`, `MEM_MB=`, `SMP=`, `ACCEL=` env vars override defaults.

## Notes
- Acceleration is auto-detected: **KVM** (`-accel kvm -cpu host`, near-native)
  when `/dev/kvm` is usable — a custom kernel enables it on the Z Flip 4 —
  otherwise **TCG** software emulation. Force software mode with `ACCEL=tcg`.
  Running as root (KernelSU), qemu opens `/dev/kvm` directly.
- `data.img` is a plain file; the flipos guest formats it on first boot, so no
  host `mkfs` is needed.
- The module zip is produced by `scripts/mkrelease.sh` (`make release`); bundle a
  native qemu by placing it in `out/android-qemu/` before packaging.
