LIBVTERM_LEON_VERSION = 0.3.3
LIBVTERM_LEON_SOURCE = libvterm-$(LIBVTERM_LEON_VERSION).tar.gz
LIBVTERM_LEON_SITE = https://www.leonerd.org.uk/code/libvterm
LIBVTERM_LEON_LICENSE = MIT
LIBVTERM_LEON_LICENSE_FILES = LICENSE
LIBVTERM_LEON_INSTALL_STAGING = YES
define LIBVTERM_LEON_BUILD_CMDS
	for source in $(@D)/src/*.c; do \
		$(TARGET_CC) $(TARGET_CFLAGS) -fPIC -std=c99 -I$(@D)/include \
			-c $$source -o $(@D)/$$(basename $$source .c).o || exit 1; \
	done
	$(TARGET_CC) -shared $(TARGET_LDFLAGS) -Wl,-soname,libvterm.so.0 \
		-o $(@D)/libvterm.so.0.3.3 $(@D)/*.o
endef
define LIBVTERM_LEON_INSTALL_STAGING_CMDS
	$(INSTALL) -D -m 0755 $(@D)/libvterm.so.0.3.3 $(STAGING_DIR)/usr/lib/libvterm.so.0.3.3
	ln -sf libvterm.so.0.3.3 $(STAGING_DIR)/usr/lib/libvterm.so.0
	ln -sf libvterm.so.0 $(STAGING_DIR)/usr/lib/libvterm.so
	$(INSTALL) -D -m 0644 $(@D)/include/vterm.h $(STAGING_DIR)/usr/include/vterm.h
	$(INSTALL) -D -m 0644 $(@D)/include/vterm_keycodes.h $(STAGING_DIR)/usr/include/vterm_keycodes.h
	mkdir -p $(STAGING_DIR)/usr/lib/pkgconfig
	sed -e 's,@INCDIR@,/usr/include,' -e 's,@LIBDIR@,/usr/lib,' \
		-e 's,@VERSION@,$(LIBVTERM_LEON_VERSION),' $(@D)/vterm.pc.in \
		> $(STAGING_DIR)/usr/lib/pkgconfig/vterm.pc
endef
define LIBVTERM_LEON_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/libvterm.so.0.3.3 $(TARGET_DIR)/usr/lib/libvterm.so.0.3.3
	ln -sf libvterm.so.0.3.3 $(TARGET_DIR)/usr/lib/libvterm.so.0
endef
$(eval $(generic-package))
