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

## Arch Linux ARM keyring (vendored, read-only image)
`br2-external/board/flipos/rootfs-overlay/usr/share/pacman/keyrings/archlinuxarm*`
are copied from `archlinuxarm-keyring-20240419-2-any.pkg.tar.xz`
(SHA-256 `3cb36869edfe413672a6e932cc55d7f8386e1a9d3b38663cfb3bc6fe0d146e21`,
mirror.archlinuxarm.org `aarch64/core`), whose detached signature verifies with the
key it trusts: **`68B3537F39A313B3E574D06777193F152BDBE6A6`** "Arch Linux ARM Build
System <builder@archlinuxarm.org>". The same fingerprint is in Arch Linux ARM's
2014 keyring on GitHub (`archlinuxarm/archlinuxarm-keyring`, commit `91e6b11`), an
independent source fetched over HTTPS. Vendored rather than downloaded at build
time because mirrors drop old keyring versions.

| file | SHA-256 |
|---|---|
| archlinuxarm.gpg | `6ce771e853f04a38a5b533cb33e61f877b9b06b58b6db051eb8a15d737a2332f` |
| archlinuxarm-trusted | `f2a7250f2a2b77542f82f4219b2bae7895f27b3dcfdf350b497e2be306af776d` |
| archlinuxarm-revoked | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` (empty) |

## Android host bundle (QEMU on the phone)
- **QEMU 10.2.0** and its libraries (glib, pixman, libslirp, dtc, zlib, …) and
  **glibc** come from the pinned Buildroot tree, built by the same toolchain as the
  guest and hash-verified by Buildroot. `board/flipos/android-bundle.py` moves them
  out of the guest into `out/images/android-host/`, so the guest never ships QEMU.
- **noVNC 1.7.0** (MPL-2.0): `https://github.com/novnc/noVNC.git` tag `v1.7.0`,
  pinned to commit `63107bd06d9e1f6136ff21aeda8cd62cbf0d433e`; `scripts/mkrelease.sh`
  refuses any other commit.
