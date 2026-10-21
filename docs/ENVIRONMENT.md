# flipos — docs/ENVIRONMENT.md (Phase 0 audit)

> **Headline finding:** this session is **NOT** running in the environment the
> build prompt assumes. The prompt targets a *Debian arm64 chroot on a rooted
> Samsung Galaxy Z Flip 4*. I am actually in an **x86_64 Ubuntu 24.04 cloud VM**
> with no phone, no display, and no KVM. Everything below is the real audit of
> *this* host; the **phone-side audit is still pending and can only be run on the
> device** (commands provided at the end).

## 1. What this host actually is

| Fact | Value | Prompt expected | Match |
|---|---|---|---|
| Kernel / userspace | `Linux vm 6.18.44 x86_64` | aarch64 phone chroot | ❌ arch |
| Distro | Ubuntu 24.04.4 LTS | Debian arm64 | ❌ |
| CPU | 4 × x86_64 (no big/little) | Snapdragon 8+ Gen 1 aarch64 | ❌ |
| RAM / swap | 15 GiB / 0 | phone RAM | n/a |
| Disk free (build fs) | **25 GiB** of 252 GiB | ≥30 GiB (warn <40) | ⚠️ below floor |
| `/dev/kvm` | **absent** | maybe present on phone | ❌ no accel |
| QEMU | **not installed** | qemu-system-aarch64 | ❌ |
| Android/Termux markers | none | Termux + KernelSU | ❌ not the phone |
| SELinux | N/A (no userspace) | Samsung enforcing | n/a |
| Exec from build fs | ✅ works (not noexec) | — | ✅ |
| Root fs | `/dev/vda` rw, relatime | — | ✅ |

## 2. Network (through the agent HTTPS proxy)

- `buildroot.org` → 200 ✅
- `cdn.kernel.org` → 200 ✅ (kernel/source tarballs reachable)
- `github.com` → 400 over the plain proxy, **but** git clone/fetch/push work
  through the session's dedicated git proxy (already used this session).

## 3. Tooling present / missing

Present: gcc/g++ 13.3.0, GNU make 4.3, git 2.43, python3 3.11, rustc/cargo 1.94.1,
ninja 1.11.1, bison 3.8.2, bc, wget 1.21.

Missing (would need `apt install` — this VM is not the target, so not installed yet):
**qemu-system-arm, qemu-utils, meson, ccache, flex, cpio, bzip2, unzip**, plus the
rest of Buildroot's host deps (libncurses-dev, rsync, file, etc.).

## 4. Capability assessment — what can and cannot happen *here*

**Can be done on this cloud VM (as a cross-build/authoring host):**
- Author the entire repo: Buildroot `br2-external`, defconfig, kernel fragment,
  the wlroots/smithay compositor, shell, services, SDK, apps, scripts, docs.
- Cross-compile aarch64 images (Buildroot cross-toolchain on an x86_64 host is
  the normal, supported path) — **if disk allows** (see risk below).
- Headless boot/serial/QMP tests under **TCG only** (slow: pure software
  emulation on 4 x86 cores, no KVM), screenshots via `screendump`.

**Cannot be done here (needs the phone):**
- Any **display** testing — no phone screen, no Termux:X11/Wayland/VNC endpoint
  a human can see. Gesture nav, lock screen, cover-screen output: on-device only.
- KVM acceleration (none here; and the phone's Samsung kernel likely lacks it too).
- The **phone-side Phase 0 audit** (§1 of the prompt): the Flip 4's `/dev/kvm`,
  SELinux denials, Termux:X11 socket path, big/little freqs, Android LMK/freezer
  mitigations, thermal behaviour. None of that is observable from this VM.
- Final acceptance: "runs as a phone OS" can only be judged on the phone.

**Risks on this host:**
- **Disk**: 25 GiB free is *below* the prompt's 30 GiB floor. A Buildroot build
  (toolchain + dl dir + ccache + output) for a lean system is ~12–25 GiB and can
  overrun. Mitigation: put `BR2_DL_DIR` and `ccache` on the roomy part, prune
  aggressively, or get more disk.
- **No swap + 15 GiB RAM**: parallel LTO/WebKit builds can OOM. Keep `-j` modest.

## 5. Phone-side audit — PENDING (run these in the Z Flip 4 Debian chroot)

```sh
uname -a; cat /etc/os-release                 # confirm Debian arm64
ls -l /dev/kvm                                 # KVM on the phone?
nproc; lscpu | grep -E 'Model name|CPU\(s\)'
for f in /sys/devices/system/cpu/cpu*/cpufreq/cpuinfo_max_freq; do cat "$f"; done
df -h ~; free -h                               # need >=30G free, check RAM
getenforce 2>/dev/null; dmesg | grep -i avc | tail   # SELinux denials
mount | grep -E 'noexec|nosuid|nodev'          # chroot mount flags
ls /data/data/com.termux/files/usr/tmp/.X11-unix 2>/dev/null  # Termux:X11 path
which qemu-system-aarch64 || echo "install qemu via apt in the chroot"
```

## 6. Recommendation

Split the roles honestly:

- **This cloud VM = authoring + cross-build + headless CI** (boot/serial/sandbox
  tests under TCG, no display). Good for ~90% of the engineering.
- **The Z Flip 4 chroot = the real target**: run the phone-side audit there, and
  do all display/gesture/on-device testing + final acceptance there. The repo is
  portable (git), so the phone just `git pull`s and runs `make run`.

This keeps the prompt's non-negotiables intact (never touch the phone's real
partitions; everything is image files; be honest about limits) while letting most
of the work proceed where it's efficient.

**Open decisions before Phase 1** (see chat): where the `flipos` repo should live
(it is a new project, unrelated to this `legenddots` dotfiles repo, which is the
only GitHub repo this session can push to), and whether to use this VM as the
build host or treat it purely as an authoring host and build on the phone.
