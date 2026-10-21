# flipos — Architecture

> Status legend: **[spec]** designed here, not yet built · **[scaffold]** config
> exists in the repo · **[done]** implemented & tested. As of this writing only
> the build-system spine is **[scaffold]**; everything below the kernel is
> **[spec]**. This document is the plan the code is held to, not a claim it exists.

## 1. Principles

- **Minimal attack surface.** Read-only root, monolithic kernel (no module
  loading), one app in focus, per-app sandbox. Every package is justified in
  `docs/PACKAGES.md`.
- **FOSS-only, reproducible.** Pinned sources (`docs/SOURCES.md`),
  `BR2_REPRODUCIBLE`, `SOURCE_DATE_EPOCH`; `make verify-repro` builds twice and
  diffs.
- **Honest about the target.** flipos runs under `qemu-system-aarch64 -M virt`.
  On the Z Flip 4 that QEMU runs inside a Debian chroot; QEMU — not hardware — is
  the root of trust here (see `docs/SECURITY.md`, `docs/LIMITATIONS.md`).

## 2. Layered view

```
            ┌──────────────────────────────────────────────────────────┐
  apps      │  Settings  Terminal  Files  Browser  Calculator   (.fpk)  │  UID 10000–19999
            └──────────────────────────────────────────────────────────┘
                     │ Wayland (client)        │ IPC (AF_UNIX + SO_PEERCRED)
            ┌────────┴─────────────────────────┴───────────────────────┐
  shell /   │  flipos-wm (wlroots compositor)   flipd  pkgd  permd       │
  services  │  status bar · lock · home · drawer · shade · recents       │  notifyd
            │  flipos-launch (per-app sandbox launcher)                  │
            └──────────────────────────────────────────────────────────┘
                     │ seatd · input/drm           │ syscalls (seccomp-filtered)
            ┌────────┴─────────────────────────────┴───────────────────┐
  init      │  s6 / s6-rc service supervision     mdev (device nodes)    │
            └──────────────────────────────────────────────────────────┘
                     │
            ┌────────┴───────────────────────────────────────────────── ┐
  kernel    │  Linux 6.18.57, arm64, monolithic, hardened (docs/KERNEL)  │
            │  virtio-gpu / tablet / keyboard / blk / net / rng / balloon│
            └──────────────────────────────────────────────────────────┘
                     │
            ┌────────┴──────────────────────────────────────────────────┐
  machine   │  qemu-system-aarch64 -M virt,gic-version=3  (TCG or KVM)   │
            └──────────────────────────────────────────────────────────┘
```

## 3. Boot flow  [spec]

1. QEMU loads `Image` with `root=/dev/vda ro rootfstype=erofs console=ttyAMA0`.
2. Kernel mounts the **EROFS** root (virtio-blk `vda`) read-only.
3. **s6-linux-init** (PID 1) starts the `s6-rc` service tree:
   - early one-shots: mount `/proc /sys /dev(pts/shm) /run(tmpfs) /tmp(tmpfs)`;
     fsck + mount `/data` (ext4, `vdb`); bind `/data/var → /var`.
   - longruns: `mdev`/`eudev` (device nodes), `seatd`, `dropbear` (dev only),
     `flipd`, `notifyd`, `pkgd`, `permd`, then **`flipos-wm`**.
4. `flipos-wm` brings up the virtio-gpu output, starts the shell surfaces, and
   shows the **lock screen**. On unlock → **home**.
5. A `flipd` one-shot prints the serial marker **`flipos: boot complete`** (the
   boot test in `make test` waits for it).

## 4. Filesystem layout  [scaffold: root image; spec: the rest]

| Path | Backing | Mode | Purpose |
|---|---|---|---|
| `/` | EROFS on `vda` | ro | OS: kernel userland, `/usr`, `/etc` defaults |
| `/data` | ext4 on `vdb` | rw | apps, app data, settings, `/var` |
| `/data/app/<id>/` | ext4 | ro to app | installed app payload |
| `/data/data/<id>/` | ext4 | 0700, app UID | per-app private data |
| `/data/system/` | ext4 | flipd-owned | settings, permissions, PIN hash, boot ctrl |
| `/var` | bind of `/data/var` | rw | logs, spool |
| `/run`, `/tmp` | tmpfs | rw | runtime sockets, scratch |

Root is immutable at runtime; all mutable state is on `/data`, which is the only
thing that survives an A/B root swap (Phase 4).

## 5. Service model — s6 + s6-rc  [spec]

Chosen over systemd for size and auditability. Each service is a small `run`
script under an `s6-rc` source tree baked into the root overlay
(`br2-external/board/flipos/rootfs-overlay`). Dependencies are explicit; the shell
is the last longrun. Crash supervision restarts a dead service with backoff.
No socket activation, no unit generators — the whole tree is readable at a glance.

## 6. Graphics & input  [spec]

- **Compositor `flipos-wm`** (wlroots, C): a *phone* compositor — one focused
  fullscreen app at a time; shell surfaces layered via `wlr-layer-shell`.
  Software rendering (pixman) so it's usable under TCG; no blur/shaders.
- **virtio-gpu** is the single output; default **540×1320** portrait, with
  1080×2640@2 and 720×1760 selectable in `theme.toml`. Optional cover-screen =
  a second virtio-gpu head (stretch).
- **Input translation:** `virtio-tablet` absolute events become touch
  (down/motion/up) inside the compositor, so the whole UI is "touch". `virtio-
  keyboard` hardware keys map to Power (lock), Vol±. On-screen keyboard via
  `zwp_input_method_v2` / `zwp_virtual_keyboard_v1`.
- **Shell surfaces:** status bar, lock, home (4×5 grid), app drawer, notification
  shade, recents, gesture nav (edge-swipe back, bottom-swipe home/recents).

## 7. App model — `.fpk`  [spec]

A zstd tarball: `manifest.toml`, `bin/ lib/ share/`, `icon.svg`, and a detached
**ed25519** `SIGNATURE` over the canonical hash of all other files. Schema in
`src/sdk/manifest.schema.json`. Permissions are declared and granted at runtime
(see `permd`). Each app gets a UID/GID from **10000–19999**, a read-only app dir,
and a 0700 data dir.

## 8. Sandbox — `flipos-launch`  [spec]

Every app starts through a launcher that applies, in order:
UID/GID drop → namespaces (mount/pid/ipc/uts/cgroup; **net only if `network`
granted**) → minimal read-only mount tree + private `/tmp` + Wayland socket →
**Landlock** matching the mounts → **seccomp** allowlist (no `ptrace`/`mount`/
`bpf`/`perf_event_open`/`kexec`/`init_module`) → cgroup-v2 slice (mem/cpu limits,
freeze when backgrounded) → `PR_SET_NO_NEW_PRIVS` + drop all caps. Each layer has
a dedicated test (`scripts/test/`). Full threat model in `docs/SECURITY.md`.

## 9. System services  [spec]

- **flipd** — app lifecycle (launch/foreground/background/freeze/kill), recents,
  screen on/off, simulated battery (labelled simulated), time/NTP.
- **pkgd** + `fpkg` CLI — install/remove/verify `.fpk`; signature + continuity
  checks; lays out app/data dirs.
- **permd** — runtime permission broker; asks the shell for Allow/Once/Deny;
  persists to `/data/system/permissions.toml`.
- **notifyd** — notification store/delivery, Do-Not-Disturb, heads-up, actions.

Settings live as TOML under `/data/system/`, mutated only through `flipd` IPC.

## 10. IPC — custom AF_UNIX + `SO_PEERCRED`  [spec]

Chosen over dbus-broker: lighter, fewer moving parts, trivially auditable. Each
service listens on a Unix socket in `/run`; it reads the caller's UID/PID/GID from
`SO_PEERCRED` (kernel-attested, unspoofable), maps UID→app id, and checks the
permission before acting. Messages are length-prefixed CBOR. No app ever talks to
another app's socket; cross-app actions go through a system service (e.g. intents
via `flipd`).

## 11. Updates & integrity (Phase 4)  [spec]

A/B EROFS slots (`root_a`/`root_b`) + a boot-control file in `/data/system/boot`;
an updater writes the inactive slot, flips the pointer, and auto-rolls-back after
3 failed "boot complete"s. dm-verity over the root with the hash on the (signed)
kernel command line. Under QEMU the trust chain ends at QEMU; on real hardware
this maps to verified boot. Optional fscrypt on `/data/data/*` keyed from the PIN
(Argon2id).

## 12. Component status

| Component | State |
|---|---|
| Build system (Buildroot br2-external, Makefile, run.sh) | **[scaffold]** |
| Kernel config (fragment, 6.18.57) | **[scaffold]** — builds in Phase 1 |
| s6 service tree | [spec] |
| flipos-wm compositor | [spec] |
| Shell surfaces | [spec] |
| .fpk / pkgd / flipos-launch / permd | [spec] |
| flipd / notifyd | [spec] |
| Bundled apps | [spec] |
| A/B + dm-verity + fscrypt | [spec] |
