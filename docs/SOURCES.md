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

## Native Android qemu (scripts/build-qemu-android.sh)
All download-verified upstream; bump via env vars.
- Android NDK `r27d` · min API `28`
- qemu `9.2.0` · glib `2.82.4` · pixman `0.44.2` · libslirp `4.8.0` · libffi `3.4.6` · pcre2 `10.44`
- Output: `out/android-qemu/` (bionic-native binary + bundled libs), bundled by `make release`.
