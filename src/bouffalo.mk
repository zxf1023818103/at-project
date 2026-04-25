#
# "main" pseudo-component makefile.
#
# (Uses default behaviour of compiling all source files in directory, adding 'include' to include path.)

include $(BL60X_SDK_PATH)/components/network/ble/ble_common.mk
CXXFLAGS += $(CFLAGS)

ifeq ($(CONFIG_ENABLE_PSM_RAM),1)
CPPFLAGS += -DCONF_USER_ENABLE_PSRAM
endif

ifeq ($(CONFIG_ENABLE_CAMERA),1)
CPPFLAGS += -DCONF_USER_ENABLE_CAMERA
endif

ifeq ($(CONFIG_ENABLE_BLSYNC),1)
CPPFLAGS += -DCONF_USER_ENABLE_BLSYNC
endif

ifeq ($(CONFIG_ENABLE_VFS_SPI),1)
CPPFLAGS += -DCONF_USER_ENABLE_VFS_SPI
endif

ifeq ($(CONFIG_ENABLE_VFS_ROMFS),1)
CPPFLAGS += -DCONF_USER_ENABLE_VFS_ROMFS
endif

ifeq ($(CONFIG_TD_DIAGNOSIS_STA),1)
CPPFLAGS += -DTD_DIAGNOSIS_STA
endif

ifeq ($(CONFIG_CHIP_NAME),BL602)
CPPFLAGS += -DBL602
endif

ifeq ($(CONFIG_CHIP_NAME),BL702)
CPPFLAGS += -DBL702
endif

ifeq ($(CONFIG_CHIP_NAME),BL702L)
CPPFLAGS += -DBL702L
endif

ifeq ($(CONFIG_CHIP_NAME),BL808)
CPPFLAGS += -DBL808
endif

ifeq ($(CONFIG_CHIP_CODE),BL616)
CPPFLAGS += -DBL616
endif

ifeq ($(CONFIG_CHIP_CODE),WB03)
CPPFLAGS += -DWB03
endif
