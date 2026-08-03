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
AWS    ?= OFF
# AWSOTA=ON builds the full AWS IoT agent (coreMQTT-Agent + FreeRTOS OTA v3.3.0) and
# implies AWS=ON. The OTA job's payload is fetched over the MQTT data plane or a
# presigned HTTPS URL, depending on what the job specifies.
AWSOTA ?= OFF
ifeq ($(AWSOTA),ON)
AWS := ON
endif

# NimBLE host over the on-chip BLE controller (ble1m10s1bredr0 profile, same as bl616).
# OFF by default; `make NIMBLE=ON` adds the host + the `bletp` throughput command, and
# main.cpp starts nimble_start() from a boot task. Coexists with WiFi.
NIMBLE ?= OFF

# PSRAM heap (on-die on BL618DG). Opt-in: board_psram_x8_init() does real detection.
PSRAM ?= OFF

# Firmware version stamp, surfaced in the AWS telemetry payload ("fw":...) — this is
# how an OTA is confirmed to have taken effect. Build the update with a different
# value, e.g. `make AWS=ON FW_VERSION=ota-B`.
FW_VERSION ?= ota-A

# appFirmwareVersion build number (ota_app.c), consumed by component/aws_iot_config. The OTA
# library commits a new image only if its version is strictly HIGHER than the running one, so an
# OTA *target* build must bump this above whatever the device is running -- otherwise the download
# succeeds, the new image boots, and self-test then rejects it with OtaErrSameFirmwareVersion and
# rolls back. FW_VERSION is only a display string and does NOT gate this; they must be bumped
# together. Same knob as project/{bouffalo,amebad}/Makefile's APP_BUILD.
APP_BUILD ?= 1

# Prebuilt LittleFS image packed into the PSM partition. Without it the whole-flash
# image carries no filesystem at all, so every flash wipes the AWS credentials
# (aws_ca/cert/key.der) and the device comes up with an unformatted LFS. Set
# LFS_BIN= (empty) to build an image that leaves PSM untouched.
LFS_BIN ?= $(REPO)/project/lfs.bin

TC_AP := $(REPO)/cmake/bl618dg-ap-toolchain.cmake
TC_NP := $(REPO)/cmake/bl618dg-np-toolchain.cmake

BUILD_AP := $(REPO)/build/bl618dg_ap
BUILD_NP := $(REPO)/build/bl618dg_np

# Nuclei GCC 14.2.1 + newlib. GCC 14 is mandatory here (the vendor wireless libs
# are slim-LTO GCC-14 bytecode) — see cmake/bl618dg-ap-toolchain.cmake. Note the
# binary prefix is riscv64-unknown-elf-, identical to the T-Head GCC, so this
# must stay an absolute path: putting the wrong one first on PATH silently picks
# a compiler that cannot read those archives.
TOOLCHAIN_BIN := $(realpath $(AT_PROJECT_ROOT)/toolchain/nuclei_riscv_newlibc_2025.10/bin)
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
	    -DAT_ENABLE_NIMBLE=$(NIMBLE) \
	    -DAT_ENABLE_AWS=$(AWS) \
	    -DAT_ENABLE_AWS_OTA=$(AWSOTA) \
	    -DAT_ENABLE_PSRAM=$(PSRAM) \
	    -DAT_FW_VERSION=$(FW_VERSION) \
	    -DAT_APP_BUILD=$(APP_BUILD) \
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
	    --append-np $(NP_BIN) \
	    $(if $(LFS_BIN),--lfs-bin $(LFS_BIN),)
	@md5sum $(WHOLE)
	@echo "flashable image: $(WHOLE)"

clean:
	@if [ -d $(BUILD_NP) ]; then $(CMAKE) --build $(BUILD_NP) --target clean; fi
	@if [ -d $(BUILD_AP) ]; then $(CMAKE) --build $(BUILD_AP) --target clean; fi

distclean:
	rm -rf $(BUILD_NP) $(BUILD_AP)
