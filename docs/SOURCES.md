# Pinned upstream sources (verified against upstream, not invented)

| Component | Version | Why / status |
|---|---|---|
| Buildroot | **2026.02.3** (LTS) | newest YYYY.02 LTS line; added as a pinned submodule in Phase 1 (`git ls-remote` confirmed the tag) |
| Linux | **6.18.57** (longterm/LTS) | newest LTS per kernel.org `releases.json`; Buildroot carries its hash and verifies the tarball |

Verified on this machine via `kernel.org/releases.json` and `git ls-remote
https://github.com/buildroot/buildroot`. Exact submodule commit SHA + per-source
SHA-256 are recorded here when Phase 1 pins them. All other package versions come
from the pinned Buildroot tree (so one tag fixes the whole set).

## Exact pins (recorded)
- Buildroot submodule commit: `679b9ead7620bbf193620d1ebf56f53c1764d37a` (tag `2026.02.3`)
- Linux: `6.18.57` (fetched + hash-verified by Buildroot at build time)

- pacman 7.1.0 tarball: SHA-256 `530e50d7…2171`, GPG-verified (Allan McRae, Arch keyring).

## Android host bundle (QEMU on the phone)
- **QEMU 10.2.0** and its libraries (glib, pixman, libslirp, dtc, zlib, …) and
  **glibc** come from the pinned Buildroot tree, built by the same toolchain as the
  guest and hash-verified by Buildroot. `board/flipos/android-bundle.py` moves them
  out of the guest into `out/images/android-host/`, so the guest never ships QEMU.
- **noVNC 1.7.0** (MPL-2.0): `https://github.com/novnc/noVNC.git` tag `v1.7.0`,
  pinned to commit `63107bd06d9e1f6136ff21aeda8cd62cbf0d433e`; `scripts/mkrelease.sh`
  refuses any other commit.
