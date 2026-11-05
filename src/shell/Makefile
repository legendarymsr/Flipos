# flipos-shell build. In a Buildroot cross build the .mk overrides CC,
# PKG_CONFIG, WAYLAND_SCANNER (host tool) and WAYLAND_PROTOCOLS (staging).
PKG_CONFIG ?= pkg-config
WAYLAND_PROTOCOLS ?= $(shell $(PKG_CONFIG) --variable=pkgdatadir wayland-protocols)
WAYLAND_SCANNER   ?= $(shell $(PKG_CONFIG) --variable=wayland_scanner wayland-scanner)
PKGS = wayland-client fcft pixman-1 libargon2
CFLAGS += $(shell $(PKG_CONFIG) --cflags $(PKGS)) -I. -std=gnu11 -O2 -Wall
LIBS   = $(shell $(PKG_CONFIG) --libs $(PKGS))
LS = wlr-layer-shell-unstable-v1
SL = ext-session-lock-v1
SL_XML = $(WAYLAND_PROTOCOLS)/staging/ext-session-lock/$(SL).xml

all: flipos-shell

# layer-shell XML vendored from wlroots 0.19.2 (MIT). Its get_popup request
# references xdg_popup, so xdg-shell's interface code is linked in too.
$(LS)-client-protocol.h: $(LS).xml
	$(WAYLAND_SCANNER) client-header $< $@
$(LS)-protocol.c: $(LS).xml
	$(WAYLAND_SCANNER) private-code $< $@
$(SL)-client-protocol.h:
	$(WAYLAND_SCANNER) client-header $(SL_XML) $@
$(SL)-protocol.c:
	$(WAYLAND_SCANNER) private-code $(SL_XML) $@
xdg-shell-protocol.c:
	$(WAYLAND_SCANNER) private-code $(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@

flipos-shell: flipos-shell.c $(LS)-client-protocol.h $(LS)-protocol.c $(SL)-client-protocol.h $(SL)-protocol.c xdg-shell-protocol.c
	$(CC) $(CFLAGS) flipos-shell.c $(LS)-protocol.c $(SL)-protocol.c xdg-shell-protocol.c $(LDFLAGS) $(LIBS) -o $@

clean:
	rm -f flipos-shell $(LS)-client-protocol.h $(LS)-protocol.c $(SL)-client-protocol.h $(SL)-protocol.c xdg-shell-protocol.c
.PHONY: all clean
