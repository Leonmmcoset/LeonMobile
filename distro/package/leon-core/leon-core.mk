################################################################################
#
# leon-core
#
################################################################################

LEON_CORE_VERSION = $(LEON_SOURCE_VERSION)
LEON_CORE_SITE = $(LEON_SOURCE_SITE)
LEON_CORE_SITE_METHOD = local
LEON_CORE_LICENSE = Apache-2.0
LEON_CORE_LICENSE_FILES = LICENSE
LEON_CORE_DEPENDENCIES = json-for-modern-cpp libarchive openssl
LEON_CORE_CONF_OPTS = $(LEON_SOURCE_CMAKE_OPTS) -DLEON_SYSTEM=ON -DLEON_TOOLS=ON
LEON_CORE_INSTALL_STAGING = YES
LEON_CORE_INSTALL_TARGET_OPTS = --component Runtime
LEON_CORE_OVERRIDE_SRCDIR_RSYNC_EXCLUSIONS = $(LEON_SOURCE_EXCLUSIONS)

$(eval $(cmake-package))

leon-core-legal-info: RSYNC_VCS_EXCLUSIONS += $(LEON_SOURCE_EXCLUSIONS)
