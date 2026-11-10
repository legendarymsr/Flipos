# flipos — limitations (honest ledger)

## Environment (this build host)
- Built on an **x86_64 Ubuntu cloud VM**, not the Z Flip 4. This host
  **cross-compiles** aarch64 (normal) but **cannot show a phone UI** and has **no
  KVM** — any run here is **TCG software emulation**, headless (serial + VNC-over-
  SSH + QMP screendump) only.
- Disk here is ~25 GiB free (below the 30 GiB target); a full Buildroot build may
  need more room or a pruned config.
- The **phone-side Phase 0 audit is still pending** and must be run on-device
  (commands in docs/ENVIRONMENT.md §5).

## Inherent to QEMU (documented as they are implemented)
- No real camera, microphone, GPS, cellular modem, or battery — those permissions
  exist in the model but are backed by **simulated/absent** hardware and are
  labelled as such in the UI.
- Verified boot / dm-verity: under QEMU the root of trust is QEMU itself, not
  hardware. Real verified boot only applies on the phone.

## Not yet built
- Everything past the build-system spine: kernel build, compositor, shell,
  services, apps, sandbox. This file is updated as each lands.

## Arch Linux ARM programs are not sandboxed
`pacman` installs real Arch Linux ARM packages into `/data/arch`; they run as root
in a chroot (`arch-run`), which is not a security boundary. GUI apps are slow under
TCG, use software rendering, and some (e.g. Chromium-based) refuse to run as root
without flags.

## Apps are not sandboxed yet
The home screen launches an app's manifest `entry` directly, as the session
user (**root**). The `flipos-launch` sandbox (per-app UID, namespaces, Landlock,
seccomp, cgroups — `docs/SECURITY.md` §3) is designed but not built, so today an
app can do anything root can inside the guest. Only install apps you trust.
