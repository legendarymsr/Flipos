# flipos app repository

flipos apps are pacman packages. Build a package with `makepkg`, sign it with the
flipos packager key, and add it to the repo database with `repo-add`.

## Build + sign + publish
```sh
cd repo/org.flipos.calculator
makepkg --sign --key flipos-packager          # -> org.flipos.calculator-1.0.0-1-aarch64.pkg.tar.zst (+ .sig)
repo-add ../flipos.db.tar.gz *.pkg.tar.zst    # (re)build the repo db
# copy *.pkg.tar.zst, *.sig and flipos.db* to the device: /data/appfs/repo/
```

## Install on device
```sh
flipos-pkg -Sy org.flipos.calculator          # pacman verifies the signature
```

`/etc/pacman.conf` points `[flipos]` at `file:///data/appfs/repo` by default;
set it to an HTTPS mirror to serve apps over the network. Signing uses the flipos
keyring at `/data/system/pacman-gpg` (`SigLevel = Required`).
