# BL618DG (dual-core AP + NP) image build — CMake + Ninja.
#
# BL618DG ships only as B0 silicon (Nuclei-900 cores). Like AmebaD, each core is
# its own standalone CMake project configured into its own build tree with a
# per-core toolchain file (which FORCE-sets BOUFFALO_CHIP + BOUFFALO_CPU_ID):
#
#   app_np -> build/bl618dg_np   minimal core image   (AmebaD KM0's role)
#   app_ap -> build/bl618dg_ap   application core     (AmebaD KM4's role; WiFi lives here)
#
# The NP .bin is then APPENDED INTO the AP .bin (the SDK's CONFIG_DUALCORE_NP_IMAGE
# step, via byai/multi_bins.py) and the AP image is re-packed into the flashable
# whole-flash image. A lone AP image does not boot — the ROM expects the NP
# payload to be present.
#
# Usage:
#   make -C project/bl618dg            # both cores + combined flashable image
#   make -C project/bl618dg app_np     # one core only
#   make -C project/bl618dg clean|distclean
#
# Flash (COM30 = data/console, COM31 = control: DTR->BOOT, RTS->reset):
#   see the bouffalo-cklink-flash skill / D:\MCU_Tools\bflb-cklink\bl618dg_flash_com30.ps1

AT_PROJECT_ROOT := $(realpath ../..)
REPO            := $(AT_PROJECT_ROOT)

CHIP  ?= bl618dg
BOARD ?= bl618dgdk

# AWS IoT telemetry client (coreMQTT + TLS). OFF by default; `make AWS=ON` builds
# the `aws` console command + the telemetry task, same flag name as the other
# platforms. Only affects the AP core (the NP image has no network stack).
AWS   ?= OFF

TC_AP := $(REPO)/cmake/bl618dg-ap-toolchain.cmake
TC_NP := $(REPO)/cmake/bl618dg-np-toolchain.cmake

BUILD_AP := $(REPO)/build/bl618dg_ap
BUILD_NP := $(REPO)/build/bl618dg_np

# Pick the riscv toolchain bundled with the Bouffalo SDK submodule. B0 is a
# Nuclei-900 core but builds with this T-Head GCC as plain rv32imafdc/ilp32d —
# see cmake/bl618dg-ap-toolchain.cmake for why (keeps newlib, not picolibc).
TOOLCHAIN_BIN := $(realpath $(AT_PROJECT_ROOT)/platform/toolchain_gcc_t-head_linux/bin)
export PATH := $(TOOLCHAIN_BIN):$(PATH)

GENERATOR ?= Ninja
CMAKE     ?= cmake
PYTHON    ?= python3

MULTI_BINS := $(REPO)/component/bouffalo_sdk/bflb_image_tool/byai/multi_bins.py
AP_BIN     := $(BUILD_AP)/app_ap_$(CHIP).bin
NP_BIN     := $(BUILD_NP)/app_np_$(CHIP).bin
WHOLE      := $(BUILD_AP)/app_ap_$(CHIP)_whole_flash_data.bin

.PHONY: all image app_ap app_np clean distclean

all: image

# Each core: configure once (toolchain file only on the first configure — the
# CMake cache is sticky), then build through the *_bin target so the ELF ->
# .bin/.xz/whole-flash pipeline runs.
app_np:
	$(CMAKE) -S $(REPO)/project/bl618dg/app_np -B $(BUILD_NP) -G "$(GENERATOR)" \
	    $$([ -f $(BUILD_NP)/CMakeCache.txt ] || echo -DCMAKE_TOOLCHAIN_FILE=$(TC_NP))
	$(CMAKE) --build $(BUILD_NP) --target app_np_bin

app_ap:
	$(CMAKE) -S $(REPO)/project/bl618dg/app_ap -B $(BUILD_AP) -G "$(GENERATOR)" \
	    -DAT_ENABLE_AWS=$(AWS) \
	    $$([ -f $(BUILD_AP)/CMakeCache.txt ] || echo -DCMAKE_TOOLCHAIN_FILE=$(TC_AP))
	$(CMAKE) --build $(BUILD_AP) --target app_ap_bin

# NP must be built first: its .bin is an input to the AP image.
#
# The AP image is then re-run through the ELF->bin pipeline with --append-np, which
# inserts the NP payload at the one correct point (after bflb_fw_post_proc, before
# the whole-flash pack). Doing the append by hand between the two would not work:
# the pipeline re-objcopies the .bin from the ELF each run and would drop it.
image: app_np app_ap
	$(PYTHON) $(REPO)/component/bouffalo_sdk/bflb_image_tool/bflb_elf_to_bin.py \
	    --chip $(CHIP) --board $(BOARD) --name app_ap \
	    --build-dir $(BUILD_AP) --elf $(BUILD_AP)/app_ap_$(CHIP).elf \
	    --objcopy $(TOOLCHAIN_BIN)/riscv64-unknown-elf-objcopy \
	    --append-np $(NP_BIN)
	@md5sum $(WHOLE)
	@echo "flashable image: $(WHOLE)"

clean:
	@if [ -d $(BUILD_NP) ]; then $(CMAKE) --build $(BUILD_NP) --target clean; fi
	@if [ -d $(BUILD_AP) ]; then $(CMAKE) --build $(BUILD_AP) --target clean; fi

distclean:
	rm -rf $(BUILD_NP) $(BUILD_AP)
