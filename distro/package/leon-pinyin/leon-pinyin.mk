################################################################################
# Unicode Unihan data for the independent LeonMobile input method
################################################################################
LEON_PINYIN_VERSION = 17.0.0
LEON_PINYIN_SOURCE = Unihan.zip
LEON_PINYIN_SITE = https://www.unicode.org/Public/$(LEON_PINYIN_VERSION)/ucd
LEON_PINYIN_LICENSE = Unicode-3.0
LEON_PINYIN_LICENSE_FILES = LICENSE
LEON_PINYIN_DEPENDENCIES = host-python3

define LEON_PINYIN_EXTRACT_CMDS
	unzip -q $(LEON_PINYIN_DL_DIR)/$(LEON_PINYIN_SOURCE) -d $(@D)
	cp $(BR2_EXTERNAL_LEON_PATH)/../third_party/Unicode-LICENSE $(@D)/LICENSE
endef

define LEON_PINYIN_BUILD_CMDS
	$(HOST_DIR)/bin/python3 $(BR2_EXTERNAL_LEON_PATH)/../scripts/build-pinyin.py $(LEON_PINYIN_DL_DIR)/$(LEON_PINYIN_SOURCE) $(@D)/pinyin.tsv
endef

define LEON_PINYIN_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/pinyin.tsv $(TARGET_DIR)/usr/share/leon/ime/pinyin.tsv
	$(INSTALL) -D -m 0644 $(@D)/LICENSE $(TARGET_DIR)/usr/share/licenses/leon-pinyin/LICENSE
endef
$(eval $(generic-package))
