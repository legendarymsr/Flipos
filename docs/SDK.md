# flipos — App SDK

How to write, package, sign, and install a flipos app. Status **[spec]** — the
`fpkg` CLI and schema land in Phase 3; this is the contract they implement.

## 1. The `.fpk` package

A **zstd-compressed tar** with this layout:

```
myapp.fpk
├── manifest.toml            # required — identity, perms, intents
├── bin/                     # executables (entry point lives here)
├── lib/                     # private shared libs (optional)
├── share/                   # data files, assets (optional)
├── icon.svg                 # launcher icon (required)
└── SIGNATURE                # detached ed25519 over the canonical hash of all the above
```

The payload targets the flipos runtime: **glibc**, Wayland, and the chosen GUI
toolkit (see §5). Apps are plain native binaries — no VM, no bytecode.

## 2. `manifest.toml`

```toml
id            = "org.flipos.calculator"   # reverse-DNS, immutable identity
name          = "Calculator"
version       = "1.0.0"                    # human semver
version_code  = 1                          # monotonic integer; upgrades must increase it
entry         = "bin/calculator"           # path inside the package
min_os        = 1                          # minimum flipos API level

[permissions]
required = []                              # granted at install or hard-fail
optional = ["network"]                     # requested at first use via permd

[intents]
handles = []                               # e.g. ["text/plain:view", "https:open"]
```

Full JSON Schema: `src/sdk/manifest.schema.json`. `id` and the signing key together
are the app's permanent identity — neither can change across an upgrade.

### Permissions (semantics)

| Permission | Meaning | QEMU note |
|---|---|---|
| `network` | app keeps its net namespace (can use sockets) | works (QEMU user-net) |
| `storage.media` | bind-mount the media dir (read) | works |
| `storage.documents` | bind-mount the documents dir (read/write) | works |
| `notifications` | post to `notifyd` | works |
| `background` | not frozen when backgrounded | works |
| `clipboard.read` | read the shared clipboard | works |
| `camera` / `microphone` / `location` | defined, prompted | **no hardware** — returns "unavailable" |

Absence of `network` means the launcher gives the app an **empty net namespace**
— it is not just policy, the app physically cannot reach the network.

## 3. Intents (minimal)

An app calls `flipd` to "open URL" or "share text"; `flipd` matches installed
handlers from their manifests' `[intents].handles` and shows a chooser if more
than one matches. Handlers declare `"<mime-or-scheme>:<action>"`.

## 4. Signing an app

Keys are **ed25519**. The signature covers a **canonical hash**: the SHA-256 of a
deterministic manifest listing `path → sha256(contents)` for every file except
`SIGNATURE`, sorted by path. This makes the signature independent of tar ordering
and compression.

```sh
# one-time: make a key (keep the private half OFFLINE / out of git)
fpkg keygen --out keys/mykey            # writes keys/mykey.key (private) + keys/mykey.pub

# build the tree, then sign + pack:
fpkg pack  ./myapp-tree --key keys/mykey.key --out myapp.fpk

# inspect / verify before shipping:
fpkg info   myapp.fpk
fpkg verify myapp.fpk --pubkey keys/mykey.pub
```

For local dev, the OS trusts `keys/flipos-dev.pub` (shipped in
`/etc/flipos/keys/`); real apps are signed by your own key and that key's public
half is added to the trust store.

## 5. Writing the app (toolkit)

flipos ships **one** GUI toolkit so the image stays small (chosen in Phase 2 —
GTK4 vs a Rust toolkit like slint/iced; the decision + rationale are recorded in
`docs/PACKAGES.md`). An app is a normal Wayland client:

```
myapp-tree/
├── manifest.toml
├── icon.svg
└── bin/myapp           # a Wayland client linked against the toolkit
```

It receives the Wayland socket and its own data dir; everything else is denied by
the sandbox (`docs/SECURITY.md` §3). Write to `$XDG_DATA_HOME` (your
`/data/data/<id>`); do not assume access to anything you didn't request.

## 6. Installing

```sh
fpkg install myapp.fpk      # verifies signature, lays out /data/app/<id> + /data/data/<id>, assigns a UID
fpkg list
fpkg remove org.flipos.calculator
```

`pkgd` enforces **signature continuity** on upgrade: a new `.fpk` for an existing
`id` must be signed by the same key and have a higher `version_code`, or it is
rejected.

## 7. Reference app

`src/apps/calculator/` is the canonical minimal example — the smallest complete
app (manifest, icon, a few-hundred-line Wayland client) to copy from. Build it,
sign it with the dev key, `fpkg install`, and it appears in the app drawer.

## 8. API levels

`min_os` gates against runtime/ABI changes. Level **1** is the first release.
Breaking changes bump the level and are listed in `docs/SDK.md` changelog (here)
so app authors know what a level requires.
