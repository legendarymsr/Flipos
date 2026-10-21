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
| s6 + s6-rc | init + service supervision (not systemd): tiny, auditable |
| eudev | device-node manager + udev API that libinput/wlroots need |
| seatd | seat/session management (GPU + input access without logind) |

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

## Phase 3 — app package manager (pacman)
| Package | What / why |
|---|---|
| **pacman** 7.1.0 | the app package manager — installs GPG-signed apps into /data |
| **gpgme** 1.24.2 | GPG signature verification for pacman (br2-external package) |
| gnupg2 | provides `gpg`/`pacman-key` for the flipos keyring |
| libarchive, libcurl, openssl | pacman deps: package extraction, downloads, crypto |

## Rendering note
Under QEMU there is no GPU, so the session launches with `WLR_RENDERER=pixman`
(CPU/software rendering) and the DRM backend over virtio-gpu. If the pinned
wlroots is built GLES2-only, a software GL (mesa3d swrast) is added instead — this
is finalized in the first on-device build and recorded here.

## Not in the image (deliberately)
- No systemd, no dbus-broker (custom `SO_PEERCRED` IPC instead — `docs/SECURITY.md`).
- No X11/XWayland (pure Wayland).
- No GPU userspace unless software GL turns out to be required.
