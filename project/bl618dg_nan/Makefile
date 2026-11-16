ROOT := $(abspath ../..)
SDK := $(ROOT)/platform/bouffalo_sdk_bl618dg
NATIVE_WHC ?= 0
USB_DIAG ?= 0
ifeq ($(NATIVE_WHC),1)
ifeq ($(USB_DIAG),1)
OUT ?= $(ROOT)/build/native-whc/bl618dg-diag
else
OUT ?= $(ROOT)/build/native-whc/bl618dg-host
endif
PROFILE_FLAGS := CONFIG_NAN_NATIVE_WHC_USB=y
else
OUT ?= $(ROOT)/build/nan-usb/bl618dg-host
PROFILE_FLAGS := CONFIG_NAN_NATIVE_WHC_USB=n
endif
PROFILE_FLAGS += CONFIG_NAN_USB_DIAGNOSTICS=$(if $(filter 1,$(USB_DIAG)),y,n)
export CHIP := bl618dg
export BOARD := bl618dgdk
export CPU_MODEL := b0
export PATH := $(ROOT)/platform/toolchain_gcc_t-head_linux/bin:$(ROOT)/platform/toolchain_gcc_zephyr_linux/bin:$(PATH)
.PHONY: all whole clean
all:
	$(MAKE) -C app BUILD_DIR=$(OUT)/app CPU_ID=ap $(PROFILE_FLAGS)
whole: all
	cp flash_prog_cfg.ini $(OUT)/flash_prog_cfg.ini
	cd $(OUT) && $(SDK)/tools/bflb_tools/bouffalo_flash_cube/BLFlashCommand-ubuntu --chipname=bl618dg --config=flash_prog_cfg.ini --build
	cp $(SDK)/tools/bflb_tools/bouffalo_flash_cube/chips/bl618dg/img_create/whole_flash_data.bin $(OUT)/whole_flash_data.bin
clean:
	$(MAKE) -C app clean BUILD_DIR=$(OUT)/app CPU_ID=ap
