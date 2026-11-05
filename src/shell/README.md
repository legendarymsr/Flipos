# flipos-shell — status bar + home screen

A small C Wayland client using **wlr-layer-shell**, drawn with **fcft + pixman**
into `wl_shm` buffers (the same libraries foot uses — no new dependencies).

- **Status bar** (TOP layer, 36 px exclusive zone): `flipos`, network state
  (`/sys/class/net/eth0/operstate`), clock (redrawn each minute). No battery:
  QEMU has none and the simulated battery (flipd) isn't built yet.
- **Home screen** (BACKGROUND layer, the rest of the screen): a 4-column grid of
  apps from flipos manifests in `/usr/share/flipos/apps/*.toml` and
  `/data/appfs/usr/share/flipos/apps/*.toml` (pacman-installed; their absolute
  `entry` paths are resolved under `/data/appfs`). Tap an icon to launch.
  The list is rescanned when the home screen is touched and once a minute.
- Icons are letter tiles (no image decoding in the image yet).

The compositor provides the rest: a bottom **gesture bar** (tap = home, also
Alt+H) that hides apps without killing them; apps are laid out between the bar
and the gesture bar; tapping an app's icon again brings back a new instance.

**Not sandboxed yet:** launched apps run as the session user (root) because
`flipos-launch` isn't built — see `docs/LIMITATIONS.md`.

`wlr-layer-shell-unstable-v1.xml` is vendored from wlroots 0.19.2 (MIT).
