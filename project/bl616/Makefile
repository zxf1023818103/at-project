SDK_DEMO_PATH ?= $(realpath .)
BL_SDK_BASE ?= $(realpath ../../platform/bouffalo_sdk)

export BL_SDK_BASE

export PATH:=$(realpath $(BL_SDK_BASE)/../toolchain_gcc_t-head_$(shell uname | cut -d '_' -f1 | tr '[:upper:]' '[:lower:]')/bin):$(PATH)

CHIP ?= bl616
.DEFAULT_GOAL := postbuild
BOARD ?= bl616dk
CROSS_COMPILE ?= riscv64-unknown-elf-

# add custom cmake definition
#cmake_definition+=-Dxxx=sss

include $(BL_SDK_BASE)/project.build

postbuild: ninja
	@md5sum build/build_out/whole_flash_data.bin