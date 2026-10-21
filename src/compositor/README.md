# flipos-wm — mobile Wayland compositor

A minimal, phone-shaped wlroots compositor in C. Derived from wlroots' **tinywl**
reference (exact 0.19.2 API), made mobile:

- **One app at a time, fullscreen** — every xdg-toplevel is sized to the output
  and fullscreened on its initial commit; pinned to the top-left.
- **Ships a terminal** — launches `foot` on start, so the OS always has a shell.
  `-s "<cmd>"` overrides the startup app.
- **Keys**: `Alt+Esc` quit · `Alt+F1` cycle apps · `Alt+Return` new terminal.
- Input: the virtio tablet/keyboard via `wlr_cursor`/xkb (touch-like).
- Software rendering (`WLR_RENDERER=pixman`) so it works with no GPU under QEMU.

Built by the `flipos-wm` Buildroot package (`br2-external/package/flipos-wm`),
installed to `/usr/bin/flipos-wm`; the s6 session runs `/usr/bin/flipos-session`
(sets XDG_RUNTIME_DIR + renderer, then execs the compositor).

The shell surfaces (status bar, lock, home, drawer, shade, recents, gestures)
layer on top in later work; this is the compositor core that makes apps run.
