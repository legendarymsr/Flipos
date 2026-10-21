# flipos — KernelSU release module

A **systemless** KernelSU module that runs flipos in QEMU inside a self-contained
Debian chroot under `/data`. It is deliberately inert toward the host.

## Safety (what it will and won't do)
- Installs to `/data/adb/modules/flipos/` and keeps everything else under
  `/data/flipos/`.
- Ships **no `system/` overlay**, defines **no `service.sh`/`post-fs-data.sh`**,
  so it **auto-runs nothing at boot** and overlays nothing onto Android.
- **Never** writes a partition or block device, never runs `dd`/`mkfs`/`fastboot`,
  never touches `/system`, `/vendor`, `boot`, `vbmeta`, or `/dev/block/*`.
- The QEMU "disks" are plain files (`/data/flipos/images/*`). The chroot's
  bind-mounts are process-scoped under `/data`.
- **Uninstall** removes the module and `rm -rf /data/flipos` — host untouched.

## Install
1. Build the guest images on the phone (`scripts/bootstrap-phone.sh`, then
   `make image`) and package: `make release` → `out/flipos-<ver>.zip`.
   (Or install the zip without images and drop `Image` + `rootfs.erofs` into
   `/data/flipos/images/` yourself.)
2. KernelSU app → **Modules → Install from storage** → pick the zip. Reboot.

## First run (once)
```sh
su -c sh /data/flipos/bin/bootstrap-chroot.sh   # builds the minimal Debian chroot + installs qemu (needs network)
```
Uses `debootstrap` if present (cleanest, truly minimal Debian); otherwise set
`DEBIAN_ROOTFS_URL` + `DEBIAN_ROOTFS_SHA256` to a pinned minimal arm64 rootfs.

## Boot flipos
```sh
su -c sh /data/flipos/bin/run-flipos.sh
```
Then open a FOSS VNC client (e.g. **AVNC** from F-Droid) to **127.0.0.1:5901** to
see the phone UI. `RES=`, `MEM_MB=`, `SMP=` env vars override the defaults.

## Notes
- Acceleration is auto-detected: **KVM** (`-accel kvm -cpu host`, near-native)
  when `/dev/kvm` is usable — a custom kernel enables it on the Z Flip 4 —
  otherwise **TCG** software emulation. Force software mode with `ACCEL=tcg`.
  For KVM the chroot needs `/dev/kvm` (the launcher bind-mounts `/dev`, so it is
  present); the qemu process runs as root and can open it.
- The module zip itself is produced by `scripts/mkrelease.sh` (`make release`).
