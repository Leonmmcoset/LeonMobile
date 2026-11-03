################################################################################
#
# leon-source
#
################################################################################

# Shared policy for local source packages. Build/output state must never enter
# source snapshots or legal-info archives. Include before each package eval.
LEON_SOURCE_VERSION = $(strip $(shell cat $(BR2_EXTERNAL_LEON_PATH)/../VERSION))
LEON_SOURCE_SITE = $(BR2_EXTERNAL_LEON_PATH)/..
LEON_SOURCE_EXCLUSIONS = --exclude .cache --exclude build --exclude output --exclude dist --exclude .git
LEON_SOURCE_CMAKE_OPTS = -DBUILD_TESTING=OFF -DLEON_SYSTEM=OFF -DLEON_TOOLS=OFF \
	-DLEON_GRAPHICS=OFF -DLEON_LMUI=OFF -DLEON_APPS=OFF -DLEON_SHELL=OFF -DLEON_COMPOSITOR=OFF
