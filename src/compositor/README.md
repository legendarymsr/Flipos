# flipos-wm — mobile Wayland compositor

A minimal, phone-shaped wlroots compositor in C. Derived from wlroots' **tinywl**
reference (exact 0.19.2 API), made mobile:

- **One app at a time, maximized** between the status bar and a bottom gesture
  bar; apps are asked to drop their own title bars (xdg-decoration, server-side).
- **wlr-layer-shell** for the phone shell (`flipos-shell`: status bar + home
  screen), which is what starts at boot (`-s "<cmd>"` overrides).
- **Gesture bar**: tapping it (or `Alt+H`) goes home — apps are hidden, not
  killed. `Alt+F1` cycles apps · `Alt+Return` terminal · `Alt+Esc` quit.
- Input: the virtio tablet/keyboard via `wlr_cursor`/xkb (touch-like).
- Software rendering (`WLR_RENDERER=pixman`) so it works with no GPU under QEMU.

Built by the `flipos-wm` Buildroot package (`br2-external/package/flipos-wm`),
installed to `/usr/bin/flipos-wm`; the s6 session runs `/usr/bin/flipos-session`
(sets XDG_RUNTIME_DIR + renderer, then execs the compositor).

The shell surfaces (status bar, lock, home, drawer, shade, recents, gestures)
layer on top in later work; this is the compositor core that makes apps run.
