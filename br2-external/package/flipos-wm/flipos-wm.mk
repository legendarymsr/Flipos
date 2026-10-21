################################################################################
# flipos-wm — the mobile Wayland compositor (built from this repo's src/)
################################################################################
FLIPOS_WM_VERSION = 0.1.0
FLIPOS_WM_SITE = $(BR2_EXTERNAL_FLIPOS_PATH)/../src/compositor
FLIPOS_WM_SITE_METHOD = local
FLIPOS_WM_LICENSE = GPL-3.0-or-later AND CC0-1.0
FLIPOS_WM_DEPENDENCIES = wlroots wayland wayland-protocols libxkbcommon host-pkgconf

# wayland-scanner runs on the HOST; the protocol XML lives in staging.
FLIPOS_WM_MAKE_OPTS = \
	CC="$(TARGET_CC)" \
	PKG_CONFIG="$(PKG_CONFIG_HOST_BINARY)" \
	WAYLAND_SCANNER="$(HOST_DIR)/bin/wayland-scanner" \
	WAYLAND_PROTOCOLS="$(STAGING_DIR)/usr/share/wayland-protocols"

define FLIPOS_WM_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) $(FLIPOS_WM_MAKE_OPTS) -C $(@D)
endef

define FLIPOS_WM_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/flipos-wm $(TARGET_DIR)/usr/bin/flipos-wm
endef

$(eval $(generic-package))
