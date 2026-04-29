#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#

PROJECT_NAME := at
PROJECT_PATH := $(abspath .)
PROJECT_BOARD := evb
export PROJECT_PATH PROJECT_BOARD
#CONFIG_TOOLPREFIX :=

BLECONTROLLER_LIBS := std m0s1 m0s1s
BLECONTROLLER_LIB_DEFAULT := std
BLECONTROLLER_LIB_PREFIX := blecontroller_602_

-include ./proj_config.mk

ifeq ($(origin BL60X_SDK_PATH), undefined)
BL60X_SDK_PATH_GUESS ?= $(shell pwd)
BL60X_SDK_PATH ?= $(BL60X_SDK_PATH_GUESS)/../../platform/BL602_SDK
$(info ****** Please SET BL60X_SDK_PATH ******)
$(info ****** Trying SDK PATH [$(BL60X_SDK_PATH)])
endif

COMPONENTS_NETWORK := sntp dns_server
COMPONENTS_BLSYS   := bltime blfdt blmtd blota bloop loopadc looprt loopset
COMPONENTS_VFS     := romfs cjson

ifeq ($(CONFIG_BLECONTROLLER_LIB),all)
COMPONENTS_BLECONTROLLER := $(addprefix $(BLECONTROLLER_LIB_PREFIX), $(BLECONTROLLER_LIBS))
else
ifeq ($(findstring $(CONFIG_BLECONTROLLER_LIB), $(BLECONTROLLER_LIBS)),)
COMPONENTS_BLECONTROLLER := $(addprefix $(BLECONTROLLER_LIB_PREFIX), $(BLECONTROLLER_LIB_DEFAULT))
else
COMPONENTS_BLECONTROLLER := $(addprefix $(BLECONTROLLER_LIB_PREFIX), $(CONFIG_BLECONTROLLER_LIB))
endif
endif

ifeq ($(CONFIG_BT_TL),1)
COMPONENTS_BLE     := $(COMPONENTS_BLECONTROLLER)
else
COMPONENTS_BLE     := $(COMPONENTS_BLECONTROLLER) blestack
endif

INCLUDE_COMPONENTS += freertos_riscv_ram newlibc bl602 bl602_std newlibc wifi blcrypto_suite wifi_manager wpa_supplicant bl_os_adapter wifi_hosal hosal lwip lwip_dhcpd mbedtls_lts vfs yloop utils cli httpc netutils blog blog_testc wifi_bt_coex
INCLUDE_COMPONENTS += easyflash4 lwip_altcp_tls_mbedtls coredump rfparam_adapter_tmp
INCLUDE_COMPONENTS += $(COMPONENTS_NETWORK)
INCLUDE_COMPONENTS += $(COMPONENTS_BLSYS)
INCLUDE_COMPONENTS += $(COMPONENTS_VFS)
INCLUDE_COMPONENTS += src

EXTRA_COMPONENT_DIRS += $(shell pwd)/../..

ifeq ($(CONFIG_COMPONENT_BUGKILLER_ENABLE),1)
INCLUDE_COMPONENTS += bugkiller_freertos
endif

ifeq ($(CONFIG_BT),1)
INCLUDE_COMPONENTS += $(COMPONENTS_BLE)
ifeq ($(CONFIG_BT_MESH),1)
INCLUDE_COMPONENTS += blemesh
ifeq ($(CONFIG_BT_MESH_MODEL),1)
INCLUDE_COMPONENTS += blemesh_model
endif
endif

ifeq ($(CONFIG_BT_MESH_SYNC),1)
INCLUDE_COMPONENTS += blsync_ble
endif
ifeq ($(CONFIG_AUTO_PTS),1)
INCLUDE_COMPONENTS += bleptstest
endif
endif
include $(BL60X_SDK_PATH)/make_scripts_riscv/project.mk

ifeq ($(CONFIG_BLECONTROLLER_LIB),all)
LDFLAGS := $(filter-out $(addprefix -l$(BLECONTROLLER_LIB_PREFIX), $(filter-out $(BLECONTROLLER_LIB_DEFAULT), $(BLECONTROLLER_LIBS))), $(LDFLAGS))
endif

# 用 --whole-archive 整体拉入 libfreertos_riscv_ram.a，让 RAM 版本的强符号
# 覆盖 flash_rom.ld 中 PROVIDE 的 ROM 符号。
# 起因：ROM/RAM 的 StreamBuffer_t 结构布局不一致，混用 ROM xStreamBufferSend
# 与 RAM xStreamBufferReceiveFromISR/Create 会读错字段、产生非法指针、
# 触发 misaligned/load access fault。整体切到 RAM 版以保持内部布局一致。
LDFLAGS := $(subst -lfreertos_riscv_ram,--whole-archive -lfreertos_riscv_ram --no-whole-archive,$(LDFLAGS))

CPPFLAGS += -DMBEDTLS_CIPHER_MODE_CFB

clean:
	@rm -rf build_out

ifeq ($(shell uname), Generic)
TOOL_SUFFIX := .exe
endif
ifeq ($(shell uname), Linux)
TOOL_SUFFIX := -ubuntu
endif
ifeq ($(shell uname), Darwin)
TOOL_SUFFIX := -macos
endif

CHIP := $(shell echo -n $(CONFIG_CHIP_NAME) | tr '[:upper:]' '[:lower:]')

BL_SDK_BASE ?= $(realpath $(BL60X_SDK_PATH)/../bouffalo_sdk)
BFLB_IOT_TOOL_PATH ?= $(realpath $(BL_SDK_BASE)/tools/bflb_tools/bouffalo_flash_cube)
BFLB_IOT_TOOL := $(realpath $(BFLB_IOT_TOOL_PATH)/bflb_iot_tool$(TOOL_SUFFIX))

BFLB_IOT_TOOL_ARGS := --chipname $(CHIP)
BFLB_IOT_TOOL_ARGS += --boot2 $(BFLB_IOT_TOOL_PATH)/chips/$(CHIP)/boot2/boot2.bin
BFLB_IOT_TOOL_ARGS += --dts $(BFLB_IOT_TOOL_PATH)/chips/$(CHIP)/device_tree/bl_factory_params.dts
BFLB_IOT_TOOL_ARGS += --firmware $(APP_BIN)
BFLB_IOT_TOOL_ARGS += --build
BFLB_IOT_TOOL_ARGS += --pt  $(BFLB_IOT_TOOL_PATH)/chips/$(CHIP)/partition/partition_cfg.toml
BFLB_IOT_TOOL_ARGS += --outdir $(BUILD_DIR_BASE)
BFLB_IOT_TOOL_ARGS += --ota $(BUILD_DIR_BASE)

ifneq ($(CONFIG_AES_KEY),)
BFLB_IOT_TOOL_ARGS += --key $(CONFIG_AES_KEY)
endif

ifneq ($(CONFIG_AES_IV),)
BFLB_IOT_TOOL_ARGS += --iv $(CONFIG_AES_IV)
endif

ifneq ($(CONFIG_PUBLIC_KEY),)
BFLB_IOT_TOOL_ARGS += --pk $(CONFIG_PUBLIC_KEY)
endif

ifneq ($(CONFIG_PRIVATE_KEY),)
BFLB_IOT_TOOL_ARGS += --sk $(CONFIG_PRIVATE_KEY)
endif

flash_bin: all_binaries
	@cd $(BUILD_DIR_BASE) && $(BFLB_IOT_TOOL) $(BFLB_IOT_TOOL_ARGS)
	@md5sum $(BUILD_DIR_BASE)/whole_flash_data.bin

.DEFAULT_GOAL := flash_bin

flash: flash_bin
	@powershell.exe -NoProfile -WorkingDirectory "C:\\MCU_Tools\\bl602_flash_download_tool" -Command ".\\bflb_iot_tool.exe --chipname $(CHIP) --firmware //wsl.localhost/Ubuntu-24.04$(realpath $(BUILD_DIR_BASE)/whole_flash_data.bin) --interface JLink --single --port 601023163"

start-gdb-server:
	@powershell.exe -NoProfile -WorkingDirectory "C:\\Program Files\\SEGGER\\JLink_V938a" -Command ".\\JLinkGDBServerCL.exe -select USB -device RISC-V -endian little -if JTAG -speed auto -noir -noLocalhostOnly -nologtofile -port 3333 -SWOPort 2332 -TelnetPort 2333"

gdb:
	$(CONFIG_TOOLPREFIX)gdb --init-command=$(BL60X_SDK_PATH)/tools/debug/602/602_gdb.init $(BUILD_DIR_BASE)/at.elf 