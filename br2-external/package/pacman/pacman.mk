################################################################################
# pacman
################################################################################
PACMAN_VERSION = 7.1.0
PACMAN_SOURCE = pacman-$(PACMAN_VERSION).tar.xz
PACMAN_SITE = https://gitlab.archlinux.org/pacman/pacman/-/releases/v$(PACMAN_VERSION)/downloads
PACMAN_LICENSE = GPL-2.0+
PACMAN_LICENSE_FILES = COPYING
PACMAN_DEPENDENCIES = host-pkgconf libarchive libcurl openssl gpgme gnupg2
# openssl for crypto, gpgme for signatures; no docs/i18n; shell scriptlets via /bin/sh.
PACMAN_CONF_OPTS = \
	-Ddoc=disabled -Ddoxygen=disabled -Di18n=false \
	-Dcrypto=openssl -Dscriptlet-shell=/bin/sh -Dfile-seccomp=disabled
$(eval $(meson-package))
