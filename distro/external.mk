include $(BR2_EXTERNAL_LEON_PATH)/leon-source.mk
include $(sort $(wildcard $(BR2_EXTERNAL_LEON_PATH)/package/*/*.mk))
