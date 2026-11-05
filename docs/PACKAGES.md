# flipos — Packages

Every package in the image, justified in one line (prompt §0.4). Minimalism is a
security property: each entry is attack surface we chose to accept. Versions come
from the pinned Buildroot tree (`docs/SOURCES.md`), so one tag fixes the set.

## Core system
| Package | What / why |
|---|---|
| Linux 6.18.57 | the kernel (hardened, monolithic — `docs/KERNEL.md`) |
| glibc | C library — chosen for later WebKitGTK browser compatibility |
| BusyBox | core userland utilities in one small binary |
| BusyBox init | PID 1 + respawn of the session (Buildroot 2026.02 has no s6 init choice; s6 remains the plan) |
| eudev | device-node manager + udev API that libinput/wlroots need |
| libseat (built-in backend) | seat access for the compositor without logind or a seatd daemon |

## Phase 2 — Wayland compositor + terminal
| Package | What / why |
|---|---|
| wayland | the Wayland core protocol library (display server + client IPC) |
| wayland-protocols | protocol XML (xdg-shell etc.) the compositor implements |
| libxkbcommon | keyboard keymap handling for the compositor |
| libinput | input device handling (the virtio tablet/keyboard) |
| libdrm | DRM/KMS access for the virtio-gpu output |
| wlroots 0.19 | the compositor library flipos-wm is built on |
| **foot** | the bundled **terminal** — a tiny, fast FOSS Wayland VT |
| **flipos-wm** | our **self-built** mobile compositor (`src/compositor/`) |
| **flipos-shell** | our status bar + home screen (`src/shell/`) |
| fcft | font rasterizing for the shell (already required by foot) |
| libargon2 | Argon2id hashing of the lock-screen PIN (memory-hard, salted) |

## Phase 3 — app package manager (pacman)
| Package | What / why |
|---|---|
| **pacman** 7.1.0 | the app package manager — installs GPG-signed apps into /data |
| **gpgme** 1.24.2 | GPG signature verification for pacman (br2-external package) |
| gnupg2 | provides `gpg`/`pacman-key` for the flipos keyring |
| libarchive, libcurl, openssl | pacman deps: package extraction, downloads, crypto |
| bash | pacman-key, makepkg and repo-add are bash scripts |
| xkeyboard-config | keyboard layout data; without it the compositor cannot build a keymap |
| DejaVu fonts | foot needs at least one font |
| e2fsprogs | formats /data on first boot |
| mesa3d (softpipe) | wlroots requires EGL/GLES/GBM at build time; pixman still renders at run time |

## Android host bundle (not in the guest)
Built by the same Buildroot run, then moved OUT of the guest rootfs by
`board/flipos/android-bundle.py` into `out/images/android-host/` for the zip.
| Package | What / why |
|---|---|
| **qemu** 10.2.0 | runs flipos on the phone: KVM + TCG, VNC + websocket, slirp networking |
| glib, pixman, libslirp, dtc, zlib, … | QEMU's libraries (bundled alongside it) |
| glibc + `ld-linux-aarch64.so.1` | lets these binaries run on Android without a chroot |
| busybox `httpd` | serves noVNC on 127.0.0.1 (the guest's busybox also gains the applet; unused there) |
| libvncserver | not used; Buildroot gates QEMU's `--enable-vnc` on selecting it, and it's stripped from the guest |
| noVNC 1.7.0 | in-browser VNC viewer (not built; static JS pinned by commit) |

## Rendering note
Under QEMU there is no GPU, so the session launches with `WLR_RENDERER=pixman`
(CPU/software rendering) and the DRM backend over virtio-gpu. If the pinned
wlroots is built GLES2-only, a software GL (mesa3d swrast) is added instead — this
is finalized in the first on-device build and recorded here.

## Not in the image (deliberately)
- No systemd, no dbus-broker (custom `SO_PEERCRED` IPC instead — `docs/SECURITY.md`).
- No X11/XWayland (pure Wayland).
- No GPU userspace unless software GL turns out to be required.
