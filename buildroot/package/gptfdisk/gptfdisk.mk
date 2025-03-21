################################################################################
#
# gptfdisk
#
################################################################################

GPTFDISK_VERSION = 1.0.9
GPTFDISK_SITE = http://downloads.sourceforge.net/sourceforge/gptfdisk
GPTFDISK_LICENSE = GPL-2.0+
GPTFDISK_LICENSE_FILES = COPYING

GPTFDISK_TARGETS_$(BR2_PACKAGE_GPTFDISK_GDISK) += gdisk
GPTFDISK_TARGETS_$(BR2_PACKAGE_GPTFDISK_SGDISK) += sgdisk
GPTFDISK_TARGETS_$(BR2_PACKAGE_GPTFDISK_CGDISK) += cgdisk

GPTFDISK_DEPENDENCIES += util-linux
GPTFDISK_LDLIBS += -luuid

ifeq ($(BR2_PACKAGE_GPTFDISK_SGDISK),y)
GPTFDISK_DEPENDENCIES += host-pkgconf popt
GPTFDISK_SGDISK_LDLIBS += `$(PKG_CONFIG_HOST_BINARY) --libs popt`
endif
ifeq ($(BR2_PACKAGE_GPTFDISK_CGDISK),y)
GPTFDISK_DEPENDENCIES += ncurses
endif

ifneq ($(BR2_STATIC_LIBS)$(BR2_PACKAGE_GPTFDISK_STATIC),)
# gptfdisk dependencies may link against libiconv, so we need to do so
# as well when linking statically
ifeq ($(BR2_PACKAGE_LIBICONV),y)
GPTFDISK_DEPENDENCIES += libiconv
GPTFDISK_LDLIBS += -liconv
endif

GPTFDISK_CFLAGS += $(TARGET_CFLAGS) -static
endif

define GPTFDISK_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) $(TARGET_CONFIGURE_OPTS) -C $(@D) \
		LDLIBS='$(GPTFDISK_LDLIBS)' CFLAGS='$(GPTFDISK_CFLAGS)' \
		SGDISK_LDLIBS='$(GPTFDISK_SGDISK_LDLIBS)' $(GPTFDISK_TARGETS_y)
endef

define GPTFDISK_INSTALL_TARGET_CMDS
	for i in $(GPTFDISK_TARGETS_y); do \
		$(INSTALL) -D -m 0755 $(@D)/$$i $(TARGET_DIR)/usr/sbin/$$i || exit 1; \
	done
endef

HOST_GPTFDISK_DEPENDENCIES = host-util-linux host-popt

define HOST_GPTFDISK_BUILD_CMDS
	$(HOST_MAKE_ENV) $(MAKE) $(HOST_CONFIGURE_OPTS) -C $(@D) sgdisk
endef

define HOST_GPTFDISK_INSTALL_CMDS
	$(INSTALL) -D -m 0755 $(@D)/sgdisk $(HOST_DIR)/sbin/sgdisk
endef

$(eval $(generic-package))
$(eval $(host-generic-package))
