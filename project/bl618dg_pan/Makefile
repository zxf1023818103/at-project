.DEFAULT_GOAL := all
ROOT := $(abspath ../..)
SDK := $(ROOT)/platform/bouffalo_sdk_bl618dg
OUT ?= $(CURDIR)/build
export PATH := $(ROOT)/platform/toolchain_gcc_zephyr_linux/bin:$(PATH)
export CHIP := bl618dg
export BOARD := bl618dgdk
export CPU_MODEL := b0
RF_DIAG ?= OFF
WIFI_ONLY ?= $(if $(filter ON,$(RF_DIAG)),ON,OFF)
HOST ?= $(if $(filter ON,$(WIFI_ONLY)),OFF,ON)
SPP ?= OFF
export CONFIG_PAN_SPP := $(if $(filter ON,$(SPP)),y,n)
ifeq ($(SPP),ON)
ifneq ($(HOST),ON)
$(error SPP=ON requires HOST=ON)
endif
endif
COEX ?= OFF
ifeq ($(SPP),ON)
ifneq ($(COEX),OFF)
$(error SPP=ON requires COEX=OFF)
endif
else ifneq ($(SPP),OFF)
$(error SPP must be ON or OFF)
endif
COEX_RF ?= standalone
ifeq ($(COEX_RF),combo)
ifneq ($(COEX),ON)
$(error COEX_RF=combo requires COEX=ON)
endif
export CONFIG_PAN_COEX_COMBO := y
else ifeq ($(COEX_RF),standalone)
export CONFIG_PAN_COEX_COMBO := n
else
$(error COEX_RF must be combo or standalone)
endif
ifeq ($(RF_DIAG),ON)
ifneq ($(WIFI_ONLY),ON)
$(error RF_DIAG=ON requires WIFI_ONLY=ON)
endif
export CONFIG_PAN_RF_DIAG := y
else ifeq ($(RF_DIAG),OFF)
export CONFIG_PAN_RF_DIAG := n
else
$(error RF_DIAG must be ON or OFF)
endif
ifeq ($(WIFI_ONLY),ON)
ifneq ($(HOST),OFF)
$(error WIFI_ONLY=ON requires HOST=OFF)
endif
ifneq ($(COEX),OFF)
$(error WIFI_ONLY=ON requires COEX=OFF)
endif
export CONFIG_PAN_WIFI_ONLY := y
else ifeq ($(WIFI_ONLY),OFF)
export CONFIG_PAN_WIFI_ONLY := n
else
$(error WIFI_ONLY must be ON or OFF)
endif
ifeq ($(HOST),ON)
export CONFIG_PAN_HOST := y
else ifeq ($(HOST),OFF)
export CONFIG_PAN_HOST := n
else
$(error HOST must be ON or OFF)
endif
ifeq ($(COEX),ON)
ifneq ($(HOST),ON)
$(error COEX=ON requires HOST=ON)
endif
export CONFIG_PAN_COEX := y
else ifeq ($(COEX),OFF)
export CONFIG_PAN_COEX := n
else
$(error COEX must be ON or OFF)
endif
.PHONY: all whole clean prepare-sdk
prepare-sdk:
	@if git -C $(SDK) apply --unidiff-zero --reverse --check $(ROOT)/component/bouffalo_wifi_tcp_config/sdk-net-al.patch 2>/dev/null; then :; else \
		git -C $(SDK) apply --unidiff-zero --check $(ROOT)/component/bouffalo_wifi_tcp_config/sdk-net-al.patch && \
		git -C $(SDK) apply --unidiff-zero $(ROOT)/component/bouffalo_wifi_tcp_config/sdk-net-al.patch; fi
all: prepare-sdk
	$(MAKE) -C pan_np BUILD_DIR=$(OUT)/np CPU_ID=np
	$(MAKE) -C pan_ap BUILD_DIR=$(OUT)/ap CPU_ID=ap CONFIG_DUALCORE_NP_IMAGE=../np/build_out/pan_bl618dg_np.bin
	cp $(OUT)/ap/build_out/pan_bl618dg_ap.bin $(OUT)/pan_bl618dg.bin
whole: all
	cp flash_prog_cfg.ini $(OUT)/flash_prog_cfg.ini
	cd $(OUT) && $(SDK)/tools/bflb_tools/bouffalo_flash_cube/BLFlashCommand-ubuntu --chipname=bl618dg --config=flash_prog_cfg.ini --build
	cp $(SDK)/tools/bflb_tools/bouffalo_flash_cube/chips/bl618dg/img_create/whole_flash_data.bin $(OUT)/whole_flash_data.bin
clean:
	$(MAKE) -C pan_ap clean BUILD_DIR=$(OUT)/ap
	$(MAKE) -C pan_np clean BUILD_DIR=$(OUT)/np
