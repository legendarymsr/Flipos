################################################################################
# flipos-shell — status bar + home screen (built from this repo's src/shell)
################################################################################
FLIPOS_SHELL_VERSION = 0.1.0
FLIPOS_SHELL_SITE = $(BR2_EXTERNAL_FLIPOS_PATH)/../src/shell
FLIPOS_SHELL_SITE_METHOD = local
FLIPOS_SHELL_LICENSE = GPL-3.0-or-later AND MIT
FLIPOS_SHELL_DEPENDENCIES = wayland wayland-protocols fcft pixman host-pkgconf host-wayland

FLIPOS_SHELL_MAKE_OPTS = \
	CC="$(TARGET_CC)" \
	PKG_CONFIG="$(PKG_CONFIG_HOST_BINARY)" \
	WAYLAND_SCANNER="$(HOST_DIR)/bin/wayland-scanner" \
	WAYLAND_PROTOCOLS="$(STAGING_DIR)/usr/share/wayland-protocols"

define FLIPOS_SHELL_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) $(FLIPOS_SHELL_MAKE_OPTS) -C $(@D)
endef

define FLIPOS_SHELL_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/flipos-shell $(TARGET_DIR)/usr/bin/flipos-shell
endef

$(eval $(generic-package))
