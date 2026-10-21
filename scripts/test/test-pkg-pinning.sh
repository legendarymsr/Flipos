#!/usr/bin/env bash
# End-to-end test of flipos-pkg per-app key pinning with real pacman/makepkg/gpg.
# Needs root + pacman, makepkg, repo-add, gpg (Arch, or Debian/Ubuntu's
# pacman-package-manager + makepkg). Uses only a temp dir; touches nothing else.
#
# Scenario: TWO keys, BOTH trusted by the pacman keyring. Plain pacman would
# accept a package signed by either — pinning must not.
set -euo pipefail
HERE="$(cd "$(dirname "$0")/../.." && pwd)"
PKG="$HERE/br2-external/board/flipos/rootfs-overlay/usr/bin/flipos-pkg"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
chmod 755 "$T"   # makepkg runs as an unprivileged user
pass=0; fail=0
ok()  { echo "  ✓ $*"; pass=$((pass+1)); }
bad() { echo "  ✗ $*"; fail=$((fail+1)); }

id -u builder >/dev/null 2>&1 || useradd -m builder
mkdir -p "$T"/{signers,gpg,repo,root,pins,build} && chmod 700 "$T/signers"
chown builder "$T/build"

# --- two signing keys ---
for who in "flipos-packager <pkg@flipos.local>" "rogue <rogue@evil.local>"; do
  gpg --homedir "$T/signers" --batch --passphrase '' --quick-gen-key "$who" ed25519 sign never 2>/dev/null
done
GOOD=$(gpg --homedir "$T/signers" --with-colons -k flipos-packager | awk -F: '/^fpr/{print $10; exit}')
ROGUE=$(gpg --homedir "$T/signers" --with-colons -k rogue | awk -F: '/^fpr/{print $10; exit}')

# --- pacman keyring trusting BOTH ---
pacman-key --gpgdir "$T/gpg" --init >/dev/null 2>&1
for k in "$GOOD" "$ROGUE"; do
  gpg --homedir "$T/signers" --export --armor "$k" > "$T/$k.asc"
  pacman-key --gpgdir "$T/gpg" --add "$T/$k.asc" >/dev/null 2>&1
  pacman-key --gpgdir "$T/gpg" --lsign-key "$k" >/dev/null 2>&1
done

cat > "$T/pacman.conf" <<EOF
[options]
RootDir = $T/root
DBPath = $T/root/var/lib/pacman/
CacheDir = $T/root/var/cache/pacman/pkg/
LogFile = $T/root/var/log/pacman.log
GPGDir = $T/gpg
Architecture = auto
SigLevel = Required DatabaseOptional
[flipos]
Server = file://$T/repo
EOF

build() { # version -> path of built pkg
  local d="$T/build/v$1"; mkdir -p "$d"; chown builder "$d"
  cat > "$d/PKGBUILD" <<EOF
pkgname=org.test.app
pkgver=$1
pkgrel=1
arch=('any')
package() { install -Dm644 /dev/null "\$pkgdir/opt/org.test.app/VERSION-$1"; }
EOF
  su builder -c "cd '$d' && PKGEXT=.pkg.tar.zst makepkg -f >/dev/null 2>&1"
  ls "$d"/org.test.app-$1-1-any.pkg.tar.zst
}
publish() { # pkgfile signer-fpr
  rm -f "$T/repo"/*
  gpg --homedir "$T/signers" --batch --yes --local-user "$2" --detach-sign -o "$1.sig" "$1"
  cp "$1" "$1.sig" "$T/repo/"
  repo-add -q "$T/repo/flipos.db.tar.gz" "$T/repo/$(basename "$1")" >/dev/null
  rm -rf "$T/root/var/cache/pacman/pkg"/*      # force a fresh download each round
  sleep 1                                     # distinct db mtime, like a real repo update
}

run() { FLIPOS_APPROOT="$T/root" FLIPOS_GPGDIR="$T/gpg" FLIPOS_PINS="$T/pins" \
        FLIPOS_PACMAN="pacman --config $T/pacman.conf --noconfirm" sh "$PKG" "$@"; }
installed() { pacman --config "$T/pacman.conf" -Q org.test.app 2>/dev/null | awk '{print $2}'; }

V1=$(build 1.0); V2=$(build 2.0)

echo "1) first install (legit key) pins the signer"
publish "$V1" "$GOOD"
run -Sy org.test.app >/dev/null 2>&1 && [ "$(installed)" = 1.0-1 ] && ok "installed 1.0" || bad "install failed"
[ "$(cat "$T/pins/org.test.app" 2>/dev/null)" = "$GOOD" ] && ok "pin = legit key" || bad "pin missing/wrong"

echo "2) upgrade signed by a DIFFERENT (but keyring-trusted) key"
publish "$V2" "$ROGUE"
out=$(run -Syyu 2>&1) && bad "rogue upgrade was ACCEPTED" || ok "rogue upgrade refused"
echo "$out" | grep -q "KEY MISMATCH" && ok "reported KEY MISMATCH" || bad "no mismatch message: $out"
[ "$(installed)" = 1.0-1 ] && ok "still on 1.0" || bad "version changed to $(installed)"

echo "3) same rogue file via -U is refused too"
run -U "$T/repo/$(basename "$V2")" >/dev/null 2>&1 && bad "-U accepted rogue" || ok "-U rogue refused"

echo "4) control: plain pacman (no pinning) WOULD accept the rogue upgrade"
cp -a "$T/root" "$T/root-ctl"
pacman --config "$T/pacman.conf" --root "$T/root-ctl" --dbpath "$T/root-ctl/var/lib/pacman" \
  --noconfirm -Syyu >/dev/null 2>&1 || true
[ "$(pacman --config "$T/pacman.conf" --root "$T/root-ctl" --dbpath "$T/root-ctl/var/lib/pacman" -Q org.test.app | awk '{print $2}')" = 2.0-1 ] \
  && ok "plain pacman DID install the rogue 2.0 (so pinning is what blocks it)" \
  || bad "control did not upgrade — test inconclusive"

echo "5) legit upgrade (same key) goes through"
publish "$V2" "$GOOD"
run -Syyu >/dev/null 2>&1 && [ "$(installed)" = 2.0-1 ] && ok "upgraded to 2.0" || bad "legit upgrade failed"

echo "6) remove forgets the pin; reinstall re-pins"
run -R org.test.app >/dev/null 2>&1 && [ ! -f "$T/pins/org.test.app" ] && ok "pin removed" || bad "pin not removed"

echo "7) --unpin allows deliberate key rotation"
publish "$V1" "$GOOD"; run -Syy org.test.app >/dev/null 2>&1 || bad "reinstall failed"
publish "$V2" "$ROGUE"; run --unpin org.test.app >/dev/null
run -Syyu >/dev/null 2>&1 && [ "$(installed)" = 2.0-1 ] && [ "$(cat "$T/pins/org.test.app")" = "$ROGUE" ] \
  && ok "rotated to new key after explicit unpin" || bad "rotation failed"

echo; echo "pinning tests: $pass passed, $fail failed"; [ "$fail" -eq 0 ]
