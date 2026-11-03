LEONMOBILE_VERSION = 1.0.0
LEONMOBILE_SITE = $(BR2_EXTERNAL_LEON_PATH)/..
LEONMOBILE_SITE_METHOD = local
LEONMOBILE_LICENSE = Apache-2.0, CC0-1.0 (compositor foundation)
LEONMOBILE_LICENSE_FILES = LICENSE third_party/tinywl-LICENSE
LEONMOBILE_INSTALL_STAGING = YES
LEONMOBILE_DEPENDENCIES = host-pkgconf host-wayland wlroots wayland wayland-protocols cairo pango json-for-modern-cpp libarchive openssl libvterm-leon jpeg
LEONMOBILE_CONF_OPTS = -DBUILD_TESTING=OFF
LEONMOBILE_OVERRIDE_SRCDIR_RSYNC_EXCLUSIONS = --exclude .cache --exclude build --exclude output --exclude dist --exclude .git

# Buildroot's upstream wlroots recipe enables GLES only. Keep pixman for the
# reference software renderer and headless output for automated boot checks.
WLROOTS_RENDERERS += pixman
WLROOTS_BACKENDS += headless

$(eval $(cmake-package))
