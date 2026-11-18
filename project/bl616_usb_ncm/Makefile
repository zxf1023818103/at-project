.DEFAULT_GOAL := all
ROOT := $(abspath ../..)
SDK := $(ROOT)/platform/bouffalo_sdk_bl618dg
OUT ?= $(CURDIR)/$(if $(filter bl618dg,$(CHIP)),build_bl618dg,build)
CHIP ?= bl616
BOARD ?= $(CHIP)dk
export CHIP BOARD
export PATH := $(ROOT)/platform/toolchain_gcc_t-head_linux/bin:$(ROOT)/platform/toolchain_gcc_zephyr_linux/bin:$(PATH)
ifeq ($(CHIP),bl618dg)
export CPU_MODEL := b0
CPU_ARG := CPU_ID=ap
FLASH_CONFIG := flash_prog_cfg_bl618dg.ini
else ifeq ($(CHIP),bl616)
FLASH_CONFIG := flash_prog_cfg.ini
else
$(error USB NCM validation supports bl616 and bl618dg only)
endif
.PHONY: all whole clean
all:
	$(MAKE) -C app BUILD_DIR=$(OUT)/app $(CPU_ARG)
whole: all
	cp $(FLASH_CONFIG) $(OUT)/flash_prog_cfg.ini
	cd $(OUT) && $(SDK)/tools/bflb_tools/bouffalo_flash_cube/BLFlashCommand-ubuntu --chipname=$(CHIP) --config=flash_prog_cfg.ini --build
	cp $(SDK)/tools/bflb_tools/bouffalo_flash_cube/chips/$(CHIP)/img_create/whole_flash_data.bin $(OUT)/whole_flash_data.bin
clean:
	$(MAKE) -C app clean BUILD_DIR=$(OUT)/app $(CPU_ARG)
