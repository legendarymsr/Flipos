################################################################################
# gpgme
################################################################################
GPGME_VERSION = 1.24.2
GPGME_SOURCE = gpgme-$(GPGME_VERSION).tar.bz2
GPGME_SITE = https://gnupg.org/ftp/gcrypt/gpgme
GPGME_LICENSE = LGPL-2.1+ (library), GPL-3.0+ (tools)
GPGME_LICENSE_FILES = COPYING.LESSER COPYING
GPGME_INSTALL_STAGING = YES
GPGME_DEPENDENCIES = host-pkgconf libgpg-error libassuan gnupg2
# No language bindings (no python/cpp/qt), skip the test suite.
GPGME_CONF_OPTS = --enable-languages= --disable-gpg-test --disable-gpgsm-test \
	--disable-gpgconf-test --disable-g13-test
$(eval $(autotools-package))
