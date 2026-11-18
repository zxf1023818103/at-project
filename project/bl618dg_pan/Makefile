.DEFAULT_GOAL := all
ROOT := $(abspath ../..)
SDK := $(ROOT)/platform/bouffalo_sdk_bl618dg
OUT ?= $(CURDIR)/$(if $(filter bl616,$(CHIP)),build_bl616,build)
export PATH := $(ROOT)/platform/toolchain_gcc_t-head_linux/bin:$(ROOT)/platform/toolchain_gcc_zephyr_linux/bin:$(PATH)
CHIP ?= bl618dg
BOARD ?= $(CHIP)dk
export CHIP BOARD
ifeq ($(CHIP),bl618dg)
export CPU_MODEL := b0
else ifneq ($(CHIP),bl616)
$(error Supported chips: bl618dg, bl616)
endif
BLUEDROID_COC ?= OFF
export CONFIG_PAN_BLUEDROID_COC := $(if $(filter ON,$(BLUEDROID_COC)),y,n)
NIMBLE_GATT ?= OFF
export CONFIG_PAN_NIMBLE_GATT := $(if $(filter ON,$(NIMBLE_GATT)),y,n)
NIMBLE_COC ?= OFF
export CONFIG_PAN_NIMBLE_COC := $(if $(filter ON,$(NIMBLE_COC)),y,n)
RF_DIAG ?= OFF
SOFT_AP ?= OFF
export CONFIG_PAN_SOFT_AP := $(if $(filter ON,$(SOFT_AP)),y,n)
WIFI_ONLY ?= $(if $(filter ON,$(RF_DIAG) $(SOFT_AP)),ON,OFF)
HOST ?= $(if $(filter ON,$(WIFI_ONLY) $(NIMBLE_COC) $(BLUEDROID_COC) $(NIMBLE_GATT)),OFF,ON)
HCI_TRACE ?= OFF
export CONFIG_PAN_HCI_TRACE := $(if $(filter ON,$(HCI_TRACE)),y,n)
GATT_BR ?= OFF
GATT_LE ?= OFF
export CONFIG_PAN_GATT_LE := $(if $(filter ON,$(GATT_LE)),y,n)
export CONFIG_PAN_GATT_BR := $(if $(filter ON,$(GATT_BR)),y,n)
SPP ?= OFF
export CONFIG_PAN_SPP := $(if $(filter ON,$(SPP)),y,n)
ifeq ($(SPP),ON)
ifneq ($(HOST),ON)
$(error SPP=ON requires HOST=ON)
endif
endif
COEX ?= OFF
ifeq ($(NIMBLE_GATT),ON)
ifneq ($(CHIP):$(HOST):$(WIFI_ONLY):$(RF_DIAG):$(SPP):$(COEX):$(GATT_LE):$(GATT_BR):$(NIMBLE_COC):$(BLUEDROID_COC),bl618dg:OFF:OFF:OFF:OFF:OFF:OFF:OFF:OFF:OFF)
$(error NIMBLE_GATT=ON is an isolated BL618DG mode; disable other host/radio modes)
endif
else ifneq ($(NIMBLE_GATT),OFF)
$(error NIMBLE_GATT must be ON or OFF)
endif
ifeq ($(BLUEDROID_COC),ON)
ifneq ($(CHIP):$(HOST):$(WIFI_ONLY):$(RF_DIAG):$(SPP):$(COEX):$(GATT_LE):$(GATT_BR):$(NIMBLE_COC),bl618dg:OFF:OFF:OFF:OFF:OFF:OFF:OFF:OFF)
$(error BLUEDROID_COC=ON is an isolated BL618DG mode; disable other host/radio modes)
endif
else ifneq ($(BLUEDROID_COC),OFF)
$(error BLUEDROID_COC must be ON or OFF)
endif
ifeq ($(NIMBLE_COC),ON)
ifneq ($(CHIP):$(HOST):$(WIFI_ONLY):$(RF_DIAG):$(SPP):$(COEX):$(GATT_LE):$(GATT_BR),bl618dg:OFF:OFF:OFF:OFF:OFF:OFF:OFF)
$(error NIMBLE_COC=ON is an isolated BL618DG mode; disable other host/radio modes)
endif
else ifneq ($(NIMBLE_COC),OFF)
$(error NIMBLE_COC must be ON or OFF)
endif
ifeq ($(GATT_LE),ON)
ifneq ($(CHIP),bl618dg)
$(error GATT_LE validation currently supports CHIP=bl618dg only)
endif
ifneq ($(HOST):$(SPP):$(COEX):$(GATT_BR),ON:OFF:OFF:OFF)
$(error GATT_LE=ON requires HOST=ON SPP=OFF COEX=OFF GATT_BR=OFF)
endif
else ifneq ($(GATT_LE),OFF)
$(error GATT_LE must be ON or OFF)
endif
ifeq ($(SPP),ON)
ifneq ($(COEX),OFF)
$(error SPP=ON requires COEX=OFF)
endif
else ifneq ($(SPP),OFF)
$(error SPP must be ON or OFF)
endif
ifeq ($(GATT_BR),ON)
ifneq ($(HOST):$(SPP):$(COEX),ON:OFF:OFF)
$(error GATT_BR=ON requires HOST=ON SPP=OFF COEX=OFF)
endif
else ifneq ($(GATT_BR),OFF)
$(error GATT_BR must be ON or OFF)
endif
ifeq ($(SOFT_AP),ON)
ifneq ($(CHIP):$(WIFI_ONLY):$(RF_DIAG),bl618dg:ON:OFF)
$(error SOFT_AP=ON requires CHIP=bl618dg WIFI_ONLY=ON RF_DIAG=OFF)
endif
else ifneq ($(SOFT_AP),OFF)
$(error SOFT_AP must be ON or OFF)
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
ifeq ($(CHIP),bl616)
ifneq ($(COEX),OFF)
$(error BL616 coexistence is not yet validated; use COEX=OFF)
endif
ifneq ($(RF_DIAG),OFF)
$(error RF_DIAG is BL618DG-specific)
endif
endif
.PHONY: all whole clean prepare-sdk
prepare-sdk:
ifneq ($(filter ON,$(GATT_BR) $(GATT_LE)),)
	@if git -C $(ROOT)/component/bluedroid_config/esp-idf apply --reverse --check $(ROOT)/component/bluedroid_config/patches/gatt-control-response.patch 2>/dev/null; then :; else \
		git -C $(ROOT)/component/bluedroid_config/esp-idf apply --check $(ROOT)/component/bluedroid_config/patches/gatt-control-response.patch && \
		git -C $(ROOT)/component/bluedroid_config/esp-idf apply $(ROOT)/component/bluedroid_config/patches/gatt-control-response.patch; fi
endif
	@if git -C $(SDK) apply --unidiff-zero --reverse --check $(ROOT)/component/bouffalo_wifi_tcp_config/sdk-net-al.patch 2>/dev/null; then :; else \
		git -C $(SDK) apply --unidiff-zero --check $(ROOT)/component/bouffalo_wifi_tcp_config/sdk-net-al.patch && \
		git -C $(SDK) apply --unidiff-zero $(ROOT)/component/bouffalo_wifi_tcp_config/sdk-net-al.patch; fi
all: prepare-sdk
ifeq ($(CHIP),bl618dg)
	$(MAKE) -C pan_np BUILD_DIR=$(OUT)/np CPU_ID=np
	$(MAKE) -C pan_ap BUILD_DIR=$(OUT)/ap CPU_ID=ap CONFIG_DUALCORE_NP_IMAGE=../np/build_out/pan_bl618dg_np.bin
	cp $(OUT)/ap/build_out/pan_bl618dg_ap.bin $(OUT)/pan_bl618dg.bin
else
	$(MAKE) -C pan_ap BUILD_DIR=$(OUT)/ap
	cp $(OUT)/ap/build_out/pan_$(CHIP).bin $(OUT)/pan_$(CHIP).bin
endif
whole: all
	cp flash_prog_cfg$(if $(filter bl616,$(CHIP)),_bl616,).ini $(OUT)/flash_prog_cfg.ini
	cd $(OUT) && $(SDK)/tools/bflb_tools/bouffalo_flash_cube/BLFlashCommand-ubuntu --chipname=$(CHIP) --config=flash_prog_cfg.ini --build
	cp $(SDK)/tools/bflb_tools/bouffalo_flash_cube/chips/$(CHIP)/img_create/whole_flash_data.bin $(OUT)/whole_flash_data.bin
clean:
	$(MAKE) -C pan_ap clean BUILD_DIR=$(OUT)/ap
ifeq ($(CHIP),bl618dg)
	$(MAKE) -C pan_np clean BUILD_DIR=$(OUT)/np
endif
