# flipos-shell build. In a Buildroot cross build the .mk overrides CC,
# PKG_CONFIG, WAYLAND_SCANNER (host tool) and WAYLAND_PROTOCOLS (staging).
PKG_CONFIG ?= pkg-config
WAYLAND_PROTOCOLS ?= $(shell $(PKG_CONFIG) --variable=pkgdatadir wayland-protocols)
WAYLAND_SCANNER   ?= $(shell $(PKG_CONFIG) --variable=wayland_scanner wayland-scanner)
PKGS = wayland-client fcft pixman-1
CFLAGS += $(shell $(PKG_CONFIG) --cflags $(PKGS)) -I. -std=gnu11 -O2 -Wall
LIBS   = $(shell $(PKG_CONFIG) --libs $(PKGS))
LS = wlr-layer-shell-unstable-v1

all: flipos-shell

# layer-shell XML vendored from wlroots 0.19.2 (MIT). Its get_popup request
# references xdg_popup, so xdg-shell's interface code is linked in too.
$(LS)-client-protocol.h: $(LS).xml
	$(WAYLAND_SCANNER) client-header $< $@
$(LS)-protocol.c: $(LS).xml
	$(WAYLAND_SCANNER) private-code $< $@
xdg-shell-protocol.c:
	$(WAYLAND_SCANNER) private-code $(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@

flipos-shell: flipos-shell.c $(LS)-client-protocol.h $(LS)-protocol.c xdg-shell-protocol.c
	$(CC) $(CFLAGS) flipos-shell.c $(LS)-protocol.c xdg-shell-protocol.c $(LDFLAGS) $(LIBS) -o $@

clean:
	rm -f flipos-shell $(LS)-client-protocol.h $(LS)-protocol.c xdg-shell-protocol.c
.PHONY: all clean
