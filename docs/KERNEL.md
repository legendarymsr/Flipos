# flipos — Kernel

**Version:** Linux **6.18.57** (longterm/LTS, verified via kernel.org
`releases.json`). Built by Buildroot, which fetches and hash-verifies the tarball.
**Base config:** upstream arm64 `defconfig` + `br2-external/board/flipos/linux.fragment`.
**Form:** **monolithic** — `CONFIG_MODULES` is off, so there is no runtime module
loader to attack. Target machine: `qemu-system-aarch64 -M virt,gic-version=3`.

This file explains every option the fragment sets on top of `defconfig`. If a
symbol moved or a dependency changed in 6.18, Phase 1 reconciles it against the
real Kconfig at build time (and this file is updated) — nothing here is invented.

## Command line

```
root=/dev/vda ro rootfstype=erofs console=ttyAMA0 quiet
```

`vda` = the read-only EROFS root (virtio-blk); `vdb` (the ext4 `/data`) is mounted
later by the init scripts, not the kernel. `console=ttyAMA0` is the QEMU virt
PL011 UART used for the serial boot log and the `flipos: boot complete` marker.
Phase 4 adds the dm-verity root hash here (and signs the command line).

## 1. virtio (QEMU `virt` hardware)

| Option | Why |
|---|---|
| `VIRTIO_PCI` | transport for all virtio devices on `-M virt` |
| `VIRTIO_BLK` | root EROFS and `/data` ext4 disks |
| `VIRTIO_NET` | QEMU user-mode networking (NTP, dev SSH) |
| `VIRTIO_INPUT` | virtio-tablet + virtio-keyboard events |
| `HW_RANDOM_VIRTIO` | entropy early (virtio-rng), so crypto/KASLR aren't starved |
| `DRM_VIRTIO_GPU` | the single display output the compositor renders to |
| `VIRTIO_BALLOON` | let the host reclaim guest RAM (phone is RAM-tight) |
| `VIRTIO_CONSOLE` | auxiliary serial/console channel |

## 2. Filesystems

| Option | Why |
|---|---|
| `EROFS_FS` + `EROFS_FS_ZIP` + `..._ZIP_LZMA` | compressed read-only root; small + immutable |
| `EXT4_FS` | the writable `/data` image |
| `OVERLAY_FS` | overlays for app mount trees / future layering |
| `TMPFS` + `TMPFS_POSIX_ACL` | `/run`, `/tmp`, and per-app private tmp |

## 3. Sandbox primitives (prompt §5)

These are the kernel features `flipos-launch` builds the app jail from.

| Option | Why |
|---|---|
| `NAMESPACES`, `USER_NS`, `PID_NS`, `NET_NS`, `UTS_NS`, `IPC_NS` | per-app isolation; an app without the `network` permission gets an empty net namespace |
| `CGROUPS`, `MEMCG`, `CGROUP_SCHED`, `CGROUP_FREEZER` | per-app cgroup-v2 slice: memory/CPU limits and **freezing** backgrounded apps |
| `SECCOMP`, `SECCOMP_FILTER` | per-app-class syscall allowlists (no `ptrace`/`mount`/`bpf`/`kexec`/…) |
| `SECURITY`, `SECURITY_LANDLOCK` | filesystem-access LSM matching each app's mount tree (defense in depth) |
| `BPF_SYSCALL` | required by seccomp-cBPF tooling and Landlock plumbing; apps themselves are denied `bpf()` via seccomp |

## 4. Hardening (prompt §3)

| Option | Why | Note |
|---|---|---|
| `STACKPROTECTOR` + `STACKPROTECTOR_STRONG` | stack-canary coverage | |
| `RANDOMIZE_BASE` | KASLR | needs good early entropy → virtio-rng above |
| `STRICT_KERNEL_RWX` + `STRICT_MODULE_RWX` | W^X on kernel mappings | module one is moot with `MODULES=n` but harmless |
| `INIT_ON_ALLOC_DEFAULT_ON` | zero heap on alloc; kills many use-of-uninitialized/UAF-read bugs | small perf cost, acceptable |
| `HARDENED_USERCOPY` | bounds-check copies across the user/kernel boundary | |
| `ARM64_PTR_AUTH` (+`_KERNEL`) | pointer authentication (ARMv8.3) — ROP/JOP resistance | emulated under TCG `-cpu max`; **native under KVM `-cpu host`** on the Flip's ARMv9 cores |
| `ARM64_BTI` (+`_KERNEL`) | branch target identification (ARMv8.5) | same: emulated on TCG, native on KVM |

### Not enabled (and why)
- **`SHADOW_CALL_STACK`** — requires a **clang-built** kernel. Buildroot's default
  is GCC; enabling it means switching the kernel toolchain to clang. Deferred to a
  Phase-1 decision, documented here when taken, not silently skipped.
- **`CONFIG_MODULES`** — intentionally **off** (monolithic). No `init_module`,
  no out-of-tree drivers, smaller attack surface. Everything virt needs is `=y`.

## 5. What this does NOT protect against

- On an x86 build host (TCG) the CPU-level mitigations (`PTR_AUTH`, `BTI`) are
  **emulated** — they exercise the code paths but give no silicon guarantee. On
  the phone under **KVM `-cpu host`** they run **natively** on the ARMv9 cores,
  so they are real there.
- No Secure Boot / measured boot in QEMU — the machine (QEMU) is the root of
  trust. Verified boot semantics (dm-verity + signed cmdline) land in Phase 4 and
  only mean something on hardware.
- KASLR/entropy quality depends on virtio-rng being present (it is, §1).

## 6. Regenerate / verify

```sh
make menuconfig          # inspect the merged config in out/build/linux-*/.config
# confirm a fragment option actually stuck:
grep -E 'EROFS_FS|LANDLOCK|PTR_AUTH|MODULES' out/build/linux-*/.config
```

Every line of the fragment is expected to appear (or be a documented, deliberate
`# ... is not set`) in the built `.config`. A mismatch is a Phase-1 bug to fix,
not to ignore.
