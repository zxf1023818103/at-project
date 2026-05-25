# Top-level make entry. Builds the cross-platform LittleFS image
# (project/lfs.bin) and dispatches to each per-platform makefile,
# which then embeds lfs.bin into the final merged firmware.
#
# Usage:
#   make -C project lfs_image    # only generate lfs.bin
#   make -C project bl602        # bouffalo BL602    (auto-builds lfs.bin first)
#   make -C project bl616        # bouffalo BL616
#   make -C project bl616cl      # bouffalo BL616CL
#   make -C project amebad       # Ameba D (RTL8721D / RTL8710C)
#   make -C project amebadplus   # Ameba Dplus (RTL8711Dx)
#   make -C project amebagreen2  # Ameba Green2 (RTL8711F)
#   make -C project all          # all six platforms
#   make -C project clean
#
# LFS params used here MUST match the runtime adapter configs across all
# three SDKs. See doc/lfs.md for the canonical numbers.

SHELL := /bin/bash

# lfs.bin is produced by the host-platform-independent cmake builder in
# CMakeLists.txt + cmake/lfs_image.cmake — it compiles mklfs from source
# against component/littlefs on whatever host cc is available, instead of
# relying on a Linux-only prebuilt under platform/bouffalo_sdk/tools/genlfs/.
LFS_BUILD_DIR := .lfs_build

RESOURCE_FILES := $(shell find resource -type f 2>/dev/null)

.PHONY: all lfs_image bl602 bl616 bl616cl amebad amebadplus amebagreen2 clean

all: bl602 bl616 bl616cl amebad amebadplus amebagreen2

# Real file target so parallel/repeated builds reuse it; rebuilds only when
# the resource tree changes (CONFIGURE_DEPENDS in cmake/lfs_image.cmake
# re-runs cmake configure if files are added / removed under resource/).
lfs.bin: $(RESOURCE_FILES)
	@cmake -S . -B $(LFS_BUILD_DIR) >/dev/null
	@cmake --build $(LFS_BUILD_DIR) --target lfs_image
	@ls -l lfs.bin

lfs_image: lfs.bin

bl602: lfs.bin
	$(MAKE) -C bouffalo CHIP=bl602 BOARD=bl602dk

bl616: lfs.bin
	$(MAKE) -C bouffalo CHIP=bl616 BOARD=bl616dk

bl616cl: lfs.bin
	$(MAKE) -C bouffalo CHIP=bl616cl BOARD=bl616cldk

amebad: lfs.bin
	$(MAKE) -C amebad

amebadplus: lfs.bin
	$(MAKE) -C ameba amebadplus

amebagreen2: lfs.bin
	$(MAKE) -C ameba amebagreen2

clean:
	-rm -f lfs.bin
	-rm -rf $(LFS_BUILD_DIR)
	-$(MAKE) -C amebad clean
	-$(MAKE) -C ameba clean
