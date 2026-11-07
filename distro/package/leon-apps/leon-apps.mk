################################################################################
#
# leon-apps
#
################################################################################

LEON_APPS_VERSION = $(LEON_SOURCE_VERSION)
LEON_APPS_SITE = $(LEON_SOURCE_SITE)
LEON_APPS_SITE_METHOD = local
LEON_APPS_SUPPORTS_IN_SOURCE_BUILD = NO
LEON_APPS_LICENSE = Apache-2.0
LEON_APPS_LICENSE_FILES = LICENSE
LEON_APPS_DEPENDENCIES = host-pkgconf leon-core lmui json-for-modern-cpp libvterm-leon jpeg dejavu ncurses
LEON_APPS_CONF_OPTS = $(LEON_SOURCE_CMAKE_OPTS) -DLEON_GRAPHICS=ON -DLEON_APPS=ON
LEON_APPS_INSTALL_TARGET_OPTS = --component Runtime
LEON_APPS_OVERRIDE_SRCDIR_RSYNC_EXCLUSIONS = $(LEON_SOURCE_EXCLUSIONS)

$(eval $(cmake-package))

leon-apps-legal-info: RSYNC_VCS_EXCLUSIONS += $(LEON_SOURCE_EXCLUSIONS)
