# Pinned upstream sources (verified against upstream, not invented)

| Component | Version | Why / status |
|---|---|---|
| Buildroot | **2026.02.3** (LTS) | newest YYYY.02 LTS line; added as a pinned submodule in Phase 1 (`git ls-remote` confirmed the tag) |
| Linux | **6.18.57** (longterm/LTS) | newest LTS per kernel.org `releases.json`; Buildroot carries its hash and verifies the tarball |

Verified on this machine via `kernel.org/releases.json` and `git ls-remote
https://github.com/buildroot/buildroot`. Exact submodule commit SHA + per-source
SHA-256 are recorded here when Phase 1 pins them. All other package versions come
from the pinned Buildroot tree (so one tag fixes the whole set).
