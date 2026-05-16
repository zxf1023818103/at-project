SDK_DEMO_PATH ?= $(realpath .)
BL_SDK_BASE ?= $(realpath ../../platform/bouffalo_sdk)

export BL_SDK_BASE

export PATH:=$(realpath $(BL_SDK_BASE)/../toolchain_gcc_t-head_$(shell uname | cut -d '_' -f1 | tr '[:upper:]' '[:lower:]')/bin):$(PATH)

ifndef CHIP
$(error CHIP is not set. Usage: make CHIP=<bl602|bl616|bl616cl> BOARD=<bl602dk|bl616dk|bl616cldk>)
endif
BOARD ?= $(CHIP)dk
CROSS_COMPILE ?= riscv64-unknown-elf-

BUILD_DIR ?= build_$(CHIP)

.DEFAULT_GOAL := postbuild

# add custom cmake definition
#cmake_definition+=-Dxxx=sss

include $(BL_SDK_BASE)/project.build

postbuild: $(BUILD_DIR)
	@python3 tools/build_whole.py \
	    --chip $(CHIP) \
	    --board $(BOARD) \
	    --build-dir $(BUILD_DIR) \
	    --sdk-base $(BL_SDK_BASE)
	@md5sum $(BUILD_DIR)/build_out/whole_flash_data.bin
