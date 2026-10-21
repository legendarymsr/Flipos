# flipos — Security model & threat model

> Status: **[spec]** unless marked. This is the design the sandbox code in
> `src/services/` + `flipos-launch` is held to; only the kernel config that backs
> it is **[scaffold]** so far. Honesty rule: anything emulated or unprotected is
> said so plainly here and in `docs/LIMITATIONS.md`.

## 1. What we protect, from whom

| Asset | Adversary | Goal |
|---|---|---|
| One app's data | another installed app | isolation — app B cannot read app A's `/data/data` |
| The system | a malicious/compromised app | containment — a broken app can't escalate to the OS |
| User data at rest | someone who picks up the (locked) device | lock screen gate; fscrypt (stretch) |
| Install integrity | a tampered/forged `.fpk` | signature + continuity |
| Inter-process calls | an app impersonating another | kernel-attested caller identity |

Out of scope (be honest): a hostile **host** (Android/KernelSU underneath QEMU) is
**inside** the trusted base here — see §9. Hardware/firmware attacks, physical bus
probing, and side channels are not addressed.

## 2. Trust model

- **Under QEMU, QEMU is the root of trust.** There is no secure boot, no
  hardware keystore, no TPM. Verified-boot semantics (dm-verity + signed cmdline,
  Phase 4) are implemented but only *mean* something on real hardware.
- The **dev signing key** ships in `/etc/flipos/keys/` for local builds; it proves
  nothing about origin. A real deployment replaces it.
- The read-only **EROFS root** means the OS image can't be modified at runtime
  without swapping the whole slot (A/B, Phase 4).

## 3. App sandbox — `flipos-launch` layers  [spec]

Defense in depth: each layer independently limits a compromised app. Applied in
this order, every app launch:

1. **Identity** — drop to the app's UID/GID (10000–19999), no supplementary
   groups unless a permission grants one. *Property:* DAC isolation of
   `/data/data/<id>` (0700, app-owned).
2. **Namespaces** — new mount, pid, ipc, uts, cgroup; **net namespace only if
   `network` is NOT granted** (empty netns ⇒ no sockets leave the app).
   *Property:* an app without `network` physically cannot reach the network.
3. **Mount tree** — read-only `/usr`,`/lib`, read-only app dir, read-write app
   data dir, private `/tmp`, the Wayland socket, nothing else. Media/documents
   bind-mounted **only** when granted. *Property:* the filesystem the app sees is
   the allowlist, not the whole OS.
4. **Landlock** — an LSM ruleset mirroring the mount tree. *Property:* even a
   mount-ns escape attempt is still denied path access by the kernel.
5. **seccomp** — a per-app-class syscall **allowlist**. Always denied:
   `ptrace`, `mount`, `bpf`, `perf_event_open`, `kexec_load`, `init_module`,
   `finit_module`, `process_vm_readv/writev`, `userfaultfd`. *Property:* kernel
   attack surface shrinks to the syscalls the app class needs; denied calls get
   `EPERM`/kill.
6. **cgroup v2 slice** — per-app memory + CPU limits; **freeze** when
   backgrounded without the `background` permission (Android cached-app freezer).
   *Property:* a runaway or background app can't exhaust RAM/CPU or work silently.
7. **No-new-privs + caps** — `PR_SET_NO_NEW_PRIVS`, all capabilities dropped.
   *Property:* no setuid/fscaps path can regain privilege.

Each property has a dedicated test under `scripts/test/` (prompt §5): an app
without `network` fails `socket()`; an app can't open another app's data dir; a
denied syscall returns `EPERM`.

## 4. Package security  [spec]

- `.fpk` carries a detached **ed25519** `SIGNATURE` over the canonical hash of all
  other files. `pkgd` verifies against trusted keys in `/etc/flipos/keys/` and
  **rejects unsigned or tampered** packages.
- **Signature continuity:** an upgrade must be signed by the **same** key as the
  installed version (Android's rule) — a different key is rejected, so a thief of
  the app id can't push a malicious update.

## 5. Permissions  [spec]

Declared in `manifest.toml`, granted at **first use** via `permd` → shell dialog
(Allow / Allow once / Deny), persisted to `/data/system/permissions.toml`, and
revocable in Settings. `camera`, `microphone`, `location` exist in the model but
have **no backing hardware** under QEMU and are labelled "no hardware".

## 6. IPC authentication  [spec]

System services listen on `/run/*.sock` and read the caller's UID/PID/GID from
**`SO_PEERCRED`** (kernel-attested, unspoofable). The service maps UID→app id and
checks the permission before acting. No app can forge another's identity, and no
app talks directly to another app's socket.

## 7. Lock screen & PIN  [spec]

PIN stored as an **Argon2id** hash in `/data/system/` (never plaintext, never
reversible). Unlock attempts are **rate-limited with exponential backoff**.
Lock-screen notification previews are hideable in Settings.

## 8. Data at rest (stretch)  [spec]

Optional **fscrypt** policy on `/data/data/*`, key derived from the PIN via
Argon2id. Threat model: protects app data if the device is seized **locked and
powered off**; gives nothing against a live, unlocked device or a hostile host.

## 9. What flipos does NOT protect against (read this)

- **The host is trusted.** flipos runs as a QEMU guest on Android+KernelSU. Root
  on the phone can read guest RAM and the image files on `/data`. flipos protects
  apps *from each other and from the guest OS*, **not** from the machine it runs
  on. This is inherent to the emulator model.
- **No hardware root of trust** under QEMU: no secure boot, no StrongBox/TEE, no
  hardware-bound keys. CPU mitigations (`PTR_AUTH`,`BTI`) are **emulated** under
  TCG and only real on the phone's ARMv9 cores.
- **Side channels, Spectre-class, DoS by resource exhaustion within limits,** and
  physical attacks are out of scope.

## 10. Delivery safety (KernelSU module)

The release is a **systemless KernelSU module** (`release/kernelsu-module/`): it
installs to `/data/adb/modules/<id>/`, ships **no `system/` overlay**, and
**auto-runs nothing at boot**. QEMU runs **directly on Android** as a plain
userspace process (no Debian chroot); flipos — the native qemu, its images, and
data — lives wholly under `/data`. It never writes a partition, never calls
`dd`/`mkfs`/`fastboot`, and never touches `/system`,`/vendor`,`boot`,`vbmeta`, or
`/dev/block/*`. QEMU opens `/dev/kvm` directly (root via KernelSU) for hardware
acceleration. Uninstalling the module and deleting its `/data` dir removes flipos
completely, leaving the host untouched. See `release/README.md`.
