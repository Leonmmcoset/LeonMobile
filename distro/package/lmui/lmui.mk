################################################################################
#
# lmui
#
################################################################################

LMUI_VERSION = $(LEON_SOURCE_VERSION)
LMUI_SITE = $(LEON_SOURCE_SITE)
LMUI_SITE_METHOD = local
LMUI_LICENSE = Apache-2.0
LMUI_LICENSE_FILES = LICENSE
LMUI_DEPENDENCIES = host-pkgconf host-wayland wayland wayland-protocols cairo pango libxkbcommon
LMUI_CONF_OPTS = $(LEON_SOURCE_CMAKE_OPTS) -DLEON_GRAPHICS=ON -DLEON_LMUI=ON
LMUI_INSTALL_TARGET_OPTS = --component Runtime
LMUI_OVERRIDE_SRCDIR_RSYNC_EXCLUSIONS = $(LEON_SOURCE_EXCLUSIONS)
LMUI_INSTALL_STAGING = YES

$(eval $(cmake-package))

lmui-legal-info: RSYNC_VCS_EXCLUSIONS += $(LEON_SOURCE_EXCLUSIONS)
