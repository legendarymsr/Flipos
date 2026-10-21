# flipos — App SDK (pacman)

flipos apps are **standard Arch packages** (`.pkg.tar.zst`) installed by
**pacman**. There is no custom `.fpk` format: you build a package with a
`PKGBUILD`, sign it, add it to the flipos repo, and `flipos-pkg` (pacman)
installs it. The OS root stays read-only; apps live in the `/data` app root
(`/data/appfs`) and are sandboxed at run time by `flipos-launch`
(`docs/SECURITY.md`). Status **[spec]**.

## 1. What an app is

A pacman package that installs:
- the app binary + assets under a per-app prefix `/opt/<id>/` (keeps apps tidy
  and easy to sandbox), and
- a **flipos manifest** at `/usr/share/flipos/apps/<id>.toml` (permissions,
  intents, entry point) that `permd`/`flipd` read. pacman has no permission or
  sandbox model, so the manifest supplies what Android-style apps need.

```
org.flipos.calculator      (pkgname)
├── /opt/org.flipos.calculator/bin/calculator
├── /opt/org.flipos.calculator/share/...
├── /usr/share/flipos/apps/org.flipos.calculator.toml   # the manifest
└── /usr/share/icons/.../org.flipos.calculator.svg
```

## 2. The flipos manifest

```toml
id      = "org.flipos.calculator"
name    = "Calculator"
entry   = "/opt/org.flipos.calculator/bin/calculator"
min_os  = 1
[permissions]
required = []
optional = ["network"]
[intents]
handles = []     # e.g. ["text/plain:view", "https:open"]
```

Permissions and their semantics are unchanged (`network`, `storage.media`,
`storage.documents`, `notifications`, `background`, `clipboard.read`, and the
no-hardware `camera`/`microphone`/`location`) — see `docs/SECURITY.md`. The
**version** comes from the pacman package (`pkgver`), not the manifest.

## 3. PKGBUILD

```bash
# PKGBUILD
pkgname=org.flipos.calculator
pkgver=1.0.0
pkgrel=1
pkgdesc="Calculator"
arch=('aarch64')
license=('GPL3')
depends=()                      # pacman resolves shared-lib deps for you
source=("calculator-$pkgver.tar.gz")
sha256sums=('…')

package() {
  install -Dm755 "$srcdir/calculator" \
    "$pkgdir/opt/$pkgname/bin/calculator"
  install -Dm644 "$srcdir/$pkgname.toml" \
    "$pkgdir/usr/share/flipos/apps/$pkgname.toml"
  install -Dm644 "$srcdir/icon.svg" \
    "$pkgdir/usr/share/icons/hicolor/scalable/apps/$pkgname.svg"
}
```

Build it with `makepkg` → `org.flipos.calculator-1.0.0-1-aarch64.pkg.tar.zst`.
A full reference app lives in `repo/org.flipos.calculator/`.

## 4. Signing & the flipos repo

Packages are **GPG-signed** (pacman's native signing) — no separate ed25519
scheme.

```sh
makepkg --sign --key flipos-packager          # builds + detached .sig
repo-add flipos.db.tar.gz *.pkg.tar.zst        # (re)build the repo database
```

Publish the `.pkg.tar.zst` + `.sig` + `flipos.db` to the flipos repo
(`Server = file:///data/appfs/repo`, or an HTTPS mirror). pacman installs only
packages whose signature verifies against the **flipos keyring**
(`/data/system/pacman-gpg`, `SigLevel = Required`). Trust flows from the flipos
packager key, which is seeded from `/etc/flipos/keys/flipos-packager.gpg` on
first run (`flipos-pkg` does `pacman-key --init` + `--lsign-key`).

## 5. Install / remove

```sh
flipos-pkg -Sy                       # refresh the flipos repo db
flipos-pkg -S  org.flipos.calculator # install (verifies the signature)
flipos-pkg -Rns org.flipos.calculator # remove
flipos-pkg -Q                        # list installed apps
```

`flipos-pkg` is pacman pointed at the `/data` app root (`/etc/pacman.conf`).
After install, `flipd` creates the app's private data dir `/data/data/<id>`
(0700, per-app UID) and the app appears in the drawer; launching it runs the
manifest `entry` through `flipos-launch` (sandboxed).

## 6. Writing the app

An app is a normal Wayland client linked against the shared toolkit (Phase 2
decision, `docs/PACKAGES.md`). It gets the Wayland socket + its own data dir;
everything else is denied by the sandbox. Write to `$XDG_DATA_HOME`
(`/data/data/<id>`); request only the permissions you need.

## 7. Trust & updates

pacman does not enforce Android-style per-app signature continuity; instead the
**single flipos repo, signed by the flipos key**, is the trusted source — an app
update is a new package version in that repo, signed by the same key. If you add
third-party repos, each needs its signing key in the keyring, and you inherit
that repo's trust. Keep `SigLevel = Required`.
