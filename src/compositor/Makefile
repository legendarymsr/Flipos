# flipos-wm build. In a Buildroot cross build the .mk overrides CC, PKG_CONFIG,
# WAYLAND_SCANNER (host tool) and WAYLAND_PROTOCOLS (staging data dir).
PKG_CONFIG ?= pkg-config
WAYLAND_PROTOCOLS ?= $(shell $(PKG_CONFIG) --variable=pkgdatadir wayland-protocols)
WAYLAND_SCANNER   ?= $(shell $(PKG_CONFIG) --variable=wayland_scanner wayland-scanner)
PKGS = wlroots-0.19 wayland-server xkbcommon
CFLAGS += $(shell $(PKG_CONFIG) --cflags $(PKGS)) -DWLR_USE_UNSTABLE -I. -std=c11 -O2
LIBS   = $(shell $(PKG_CONFIG) --libs $(PKGS))

all: flipos-wm

# wayland-scanner turns the protocol XML into a C header wlroots needs on the include path.
xdg-shell-protocol.h:
	$(WAYLAND_SCANNER) server-header \
		$(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@

flipos-wm: flipos-wm.c xdg-shell-protocol.h
	$(CC) $(CFLAGS) flipos-wm.c $(LDFLAGS) $(LIBS) -o $@

clean:
	rm -f flipos-wm xdg-shell-protocol.h
.PHONY: all clean
