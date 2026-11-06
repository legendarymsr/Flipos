# flipos — KernelSU release module

A **systemless** KernelSU module that installs and configures everything needed to
run flipos on the phone: the guest OS, a bundled QEMU that runs **directly on
Android** (KVM with a TCG fallback), VNC, and an in-browser noVNC viewer. It is
deliberately inert toward the host.

## What the zip contains
| Path in zip | Installed to | What |
|---|---|---|
| `payload/images/` | `/data/flipos/images/` | `Image` (kernel) + `rootfs.erofs` (read-only OS) |
| `payload/host/` | `/data/flipos/host/` | QEMU 10.2.0 (`qemu-system-aarch64`, `qemu-img`), busybox (httpd), and the glibc runtime they need — all built from source by flipos' own Buildroot |
| `payload/novnc/` | `/data/flipos/novnc/` | noVNC 1.7.0 (MPL-2.0), pinned by git commit |
| `payload/flipos` | `/data/flipos/bin/flipos` | the controller (`start/stop/status/url/log`) |
| `action.sh` | module | KernelSU **Action** button: start (opens the viewer) / stop |
| `webroot/` | module | KernelSU **WebUI**: Start, Open viewer, Status, Stop, console log |

The bundled binaries are ordinary glibc/aarch64 programs. They run on Android's
kernel through the bundled loader
(`host/lib/ld-linux-aarch64.so.1 --library-path host/lib …`), so no chroot,
no Termux and no Debian are involved.

## Install
1. KernelSU manager → **Modules → Install from storage** → `flipos-v0.3.1.zip`.
   The installer copies the files, self-tests the bundled QEMU on your phone and
   reports whether `/dev/kvm` is present. Your existing `data.img` is kept on upgrade.
2. No reboot is needed to use it (nothing runs at boot anyway).

## Use
- **Action button** (in the module card): starts flipos and opens
  `http://127.0.0.1:6080/vnc.html…` in your browser. The lock screen appears after
  about 20 s under KVM (minutes under TCG). Press Action again to stop it.
- **WebUI** (tap the module card): Start / Open viewer / Status / Stop / log.
- **Shell**: `su -c sh /data/flipos/bin/flipos start|stop|status|url|log`.
- **VNC app** instead of the browser: e.g. AVNC → `127.0.0.1:5901`.

First boot: the guest formats its 4 GB sparse `data.img` and asks you to **set a
PIN** (4–8 digits). Gestures: swipe up from the bottom bar = home; swipe in from
the left/right edge = back.

### Acceleration
`flipos start` tries, in order:
1. **KVM pinned to one CPU cluster** (`KVM_CPUS=4-6`, the Z Flip 4's three
   Cortex-A710 cores). On big.LITTLE phones, `-cpu host` vCPUs can fail when the
   thread moves between core types, so pinning is tried first.
2. **KVM unpinned.**
3. **TCG** (software emulation, slow but always works).

`flipos status` shows which one is running. Overrides: `ACCEL=tcg`,
`KVM_CPUS=""` (no pin) or another range, `SMP`, `MEM_MB` (default 2048), `RES`
(default `540x1320`), `DATA_SIZE` (first boot only).

## Safety (what it will and won't do)
- Files live under `/data/adb/modules/flipos/` and `/data/flipos/` only.
- **No `system/` overlay**, no `service.sh`/`post-fs-data.sh`, so it **auto-runs
  nothing at boot** and overlays nothing onto Android.
- **Never** writes a partition or block device, never runs `dd`/`mkfs`/`fastboot`,
  never touches `/system`, `/vendor`, `boot`, `vbmeta`, or `/dev/block/*`.
- QEMU and httpd are plain processes; the VM's disks are files in
  `/data/flipos/images` (mode 0700). VNC, websocket and httpd listen on
  **127.0.0.1 only**, so nothing is exposed to the network. QEMU's
  `oom_score_adj` is lowered so Android's low-memory killer spares it.
- **Uninstall** stops the VM and removes `/data/flipos`, **including your
  `data.img`**. Back it up first if you want to keep it.

## Known limits (honest)
- **Untested on the real phone.** The bundle was boot-tested only inside an
  aarch64 VM, not on Android/Exynos/Snapdragon hardware with KVM.
- **Guest DNS:** QEMU's user networking reads the host's `/etc/resolv.conf`,
  which Android does not have, so name lookups inside flipos may fail. IP
  connectivity still works.
- When the phone's screen is off, Android may suspend the CPU, which pauses the VM.

## Build the zip
`make image && make release` → `out/flipos-<ver>.zip` (`scripts/mkrelease.sh`).
