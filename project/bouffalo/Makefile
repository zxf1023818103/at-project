# project/bouffalo — thin wrapper that drives cmake against the
# chip-toolchain-aware CMakeLists.txt. The legacy Makefile (delegating
# to platform/bouffalo_sdk/project.build with a post-build python step)
# is preserved as Makefile.legacy.
#
# Usage:
#   make CHIP=<bl602|bl616|bl616cl> [BOARD=<board>]
#
# The build directory is build_<CHIP>/ at the project root. The ELF
# lands at build_<CHIP>/at.elf (CMake's default location for the
# `at` target). bflb_add_executable in component/bouffalo_sdk/bflb_image_tool/CMakeLists.txt then
# generates build_<CHIP>/at_<CHIP>_whole_flash_data.bin as part of the
# build — no separate postbuild step needed.

AT_PROJECT_ROOT := $(realpath ../..)

ifndef CHIP
$(error CHIP is not set. Usage: make CHIP=<bl602|bl616|bl616cl> [BOARD=<board>])
endif

BOARD     ?= $(CHIP)dk
BUILD_DIR ?= build_$(CHIP)

# Pick the riscv toolchain bundled with the Bouffalo SDK submodule.
TOOLCHAIN_BIN := $(realpath $(AT_PROJECT_ROOT)/platform/toolchain_gcc_t-head_linux/bin)
export PATH := $(TOOLCHAIN_BIN):$(PATH)

CHIP_TOOLCHAIN := $(AT_PROJECT_ROOT)/cmake/$(CHIP)-toolchain.cmake

GENERATOR ?= Ninja
CMAKE     ?= cmake

.PHONY: all configure build clean distclean

all: build

$(BUILD_DIR)/CMakeCache.txt: CMakeLists.txt proj.conf
	$(CMAKE) -S . -B $(BUILD_DIR) -G "$(GENERATOR)" \
	    -DCMAKE_TOOLCHAIN_FILE=$(CHIP_TOOLCHAIN) \
	    -DBOUFFALO_BOARD=$(BOARD)

configure: $(BUILD_DIR)/CMakeCache.txt

build: configure
	$(CMAKE) --build $(BUILD_DIR)
	@if [ -f $(BUILD_DIR)/at_$(CHIP)_whole_flash_data.bin ]; then \
	    md5sum $(BUILD_DIR)/at_$(CHIP)_whole_flash_data.bin; \
	fi

clean:
	@if [ -d $(BUILD_DIR) ]; then $(CMAKE) --build $(BUILD_DIR) --target clean; fi

distclean:
	rm -rf $(BUILD_DIR)
