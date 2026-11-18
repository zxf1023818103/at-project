ROOT := $(abspath ../..)
SDK := $(ROOT)/platform/bouffalo_sdk_bl618dg
ROLE ?= host
OUT ?= $(ROOT)/build/usb-throughput/bl618dg-$(ROLE)
ifneq ($(filter $(ROLE),host device),$(ROLE))
$(error ROLE must be host or device)
endif
export CHIP := bl618dg
export BOARD := bl618dgdk
export CPU_MODEL := b0
export PATH := $(ROOT)/platform/toolchain_gcc_t-head_linux/bin:$(ROOT)/platform/toolchain_gcc_zephyr_linux/bin:$(PATH)
export CONFIG_USB_BENCH_HOST := $(if $(filter host,$(ROLE)),y,n)
.PHONY: all whole clean
all:
	$(MAKE) -C app BUILD_DIR=$(OUT)/app CPU_ID=ap
whole: all
	cp flash_prog_cfg.ini $(OUT)/flash_prog_cfg.ini
	cd $(OUT) && $(SDK)/tools/bflb_tools/bouffalo_flash_cube/BLFlashCommand-ubuntu --chipname=bl618dg --config=flash_prog_cfg.ini --build
	cp $(SDK)/tools/bflb_tools/bouffalo_flash_cube/chips/bl618dg/img_create/whole_flash_data.bin $(OUT)/whole_flash_data.bin
clean:
	$(MAKE) -C app clean BUILD_DIR=$(OUT)/app CPU_ID=ap
