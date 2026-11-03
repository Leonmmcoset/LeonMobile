################################################################################
#
# leon-desktop
#
################################################################################

LEON_DESKTOP_VERSION = $(LEON_SOURCE_VERSION)
LEON_DESKTOP_SITE = $(LEON_SOURCE_SITE)
LEON_DESKTOP_SITE_METHOD = local
LEON_DESKTOP_LICENSE = Apache-2.0, CC0-1.0 (compositor foundation)
LEON_DESKTOP_LICENSE_FILES = LICENSE third_party/tinywl-LICENSE
LEON_DESKTOP_DEPENDENCIES = host-pkgconf lmui wlroots json-for-modern-cpp
LEON_DESKTOP_CONF_OPTS = $(LEON_SOURCE_CMAKE_OPTS) -DLEON_GRAPHICS=ON -DLEON_SHELL=ON -DLEON_COMPOSITOR=ON
LEON_DESKTOP_INSTALL_TARGET_OPTS = --component Runtime
LEON_DESKTOP_OVERRIDE_SRCDIR_RSYNC_EXCLUSIONS = $(LEON_SOURCE_EXCLUSIONS)

$(eval $(cmake-package))

leon-desktop-legal-info: RSYNC_VCS_EXCLUSIONS += $(LEON_SOURCE_EXCLUSIONS)
