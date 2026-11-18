# Independent CMake/Ninja image projects, matching project/amebad.
REPO := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
BUILD ?= $(REPO)/build/amebadplus
JOBS ?= 8
include ../common_flags.mk
ifneq ($(filter ON,$(AWS) $(MINIMAL)),)
IPERF ?= OFF
else
IPERF ?= ON
endif
PKCS11 ?= OFF
BENCH ?= OFF
VFSTEST ?= OFF
CPPTEST ?= OFF
QUIC ?= OFF
WPS ?= OFF
EAP ?= OFF
HCI ?= OFF
SBOOT ?= ON
# COM42 has RSIP fused; use OFF only for an unprovisioned board.
RSIP ?= ON
HWGCM ?= OFF
ROMKAT ?= OFF
EFUSE_DEBUG ?= OFF
TLS_HEAP_TRACE ?= OFF
TFM ?= OFF
STORAGE ?= OFF
ifeq ($(STORAGE),ON)
override TFM := ON
endif
SECURITY_KEYS ?= $(REPO)/project/amebadplus/keys/com42.json
ifeq ($(HCI)$(NIMBLE),ONON)
$(error HCI and NIMBLE both own the Bluetooth controller; select one)
endif
QUIC_SERVER ?= 192.168.4.250
PORT ?= COM42
EXTRA_FLAGS += -DAT_ENABLE_TFM=$(TFM) -DAT_ENABLE_STORAGE=$(STORAGE)
EXTRA_FLAGS += -DAT_ENABLE_HWGCM=$(HWGCM) -DAT_ENABLE_ROM_KAT=$(ROMKAT) -DAT_EFUSE_DEBUG=$(EFUSE_DEBUG) -DAT_TLS_HEAP_TRACE=$(TLS_HEAP_TRACE) -DAT_ENABLE_AWS_OTA=$(AWSOTA) -DAT_ENABLE_MCUMGR=$(MCUMGR) -DAT_ENABLE_WPS=$(WPS) -DAT_ENABLE_EAP=$(EAP) -DAT_ENABLE_HCI=$(HCI)
.PHONY: all image bootloader_km0 bootloader_km0_2nd bootloader_km4 app_km0 app_km4 app_km4_secure deps tfm_deps test flash flashloader cli_test clean distclean
all: image
bootloader_km0 bootloader_km0_2nd bootloader_km4 app_km0 app_km4:
	cmake -S $(REPO)/project/amebadplus/$@ -B $(BUILD)/$@ -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel -DAT_ENABLE_NIMBLE=$(NIMBLE) -DAT_ENABLE_AWS=$(AWS) -DTLS_BACKEND=$(TLS_BACKEND) -DAT_ENABLE_CRT_BUNDLE=$(CRT_BUNDLE) -DAT_FW_VERSION=$(FW_VERSION) -DAT_APP_BUILD=$(APP_BUILD) -DAT_ENABLE_CARES=$(CARES) -DAT_ENABLE_IPERF=$(IPERF) -DAT_ENABLE_MINIMAL=$(MINIMAL) -DAT_CRYPTO_SELFTESTS=$(CRYPTO_SELFTESTS) -DAT_ENABLE_PKCS11=$(PKCS11) -DAT_ENABLE_BENCH=$(BENCH) -DAT_VFS_SELFTEST=$(VFSTEST) -DAT_CPP_SELFTEST=$(CPPTEST) -DAT_ENABLE_QUIC=$(QUIC) -DAT_QUIC_SERVER=$(QUIC_SERVER) $(EXTRA_FLAGS) -DCMAKE_TOOLCHAIN_FILE=$(REPO)/cmake/amebadplus-$(if $(filter app_km0 bootloader_km0 bootloader_km0_2nd,$@),km0,km4).cmake
	cmake --build $(BUILD)/$@ --target $@ -j $(JOBS)
image: bootloader_km4 app_km0 app_km4
	$(MAKE) -C $(REPO)/project lfs_image
	python3 $(REPO)/component/amebadplus/image_tool/build_image.py merge --build $(BUILD) --lfs $(REPO)/project/lfs.bin --sboot $(SBOOT) --rsip $(RSIP) --keys $(SECURITY_KEYS) $(if $(filter ON,$(TFM)),--tfm $(BUILD)/app_km4/app_km4_secure/tfm_spe/bin/tfm_s.bin)
app_km4_secure:
	cmake -S $(REPO)/project/amebadplus/app_km4_secure -B $(BUILD)/app_km4_secure -G Ninja -DAT_ENABLE_STORAGE=$(STORAGE) -DAT_EFUSE_DEBUG=$(EFUSE_DEBUG) -DCMAKE_TOOLCHAIN_FILE=$(REPO)/cmake/amebadplus-km4.cmake
	cmake --build $(BUILD)/app_km4_secure --target app_km4_secure -j $(JOBS)
deps:
	python3 -m pip install --target $(REPO)/build/amebadplus/python-deps -r $(REPO)/component/amebadplus/image_tool/requirements.txt
tfm_deps:
	python3 -m venv $(REPO)/build/amebadplus/tfm-python
	$(REPO)/build/amebadplus/tfm-python/bin/pip install -r $(REPO)/component/trusted-firmware-m_config/trusted-firmware-m/tools/requirements.txt
test:
	python3 -m unittest discover -s $(REPO)/component/amebadplus/image_tool -p 'test_*.py'
	python3 -m unittest discover -s $(REPO)/component/amebadplus/ota_flash_port -p 'test_*.py'
	python3 -m unittest discover -s $(REPO)/component/amebadplus/efuse_port -p 'test_*.py'
flashloader:
	python3 $(REPO)/component/amebadplus/image_tool/prepare_flashloader.py --keys $(SECURITY_KEYS) --output $(BUILD)/security-tools
flash: image flashloader
	powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$$(wslpath -w $(REPO)/project/amebadplus/flash.ps1)" -Port $(PORT) -Image "$$(wslpath -w $(BUILD)/firmware.bin)" -FirmwareOnly -ToolDir "$$(wslpath -w $(BUILD)/security-tools)" $(if $(filter ON,$(RSIP)),-AllowEncrypted)
cli_test:
	powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$$(wslpath -w $(REPO)/project/amebadplus/cli_test.ps1)" -Port $(PORT)
clean:
	@for image in bootloader_km0 bootloader_km0_2nd bootloader_km4 app_km0 app_km4 app_km4_secure; do if [ -f $(BUILD)/$$image/build.ninja ]; then cmake --build $(BUILD)/$$image --target clean; fi; done
distclean:
	rm -rf $(BUILD)/bootloader_km0 $(BUILD)/bootloader_km0_2nd $(BUILD)/bootloader_km4 $(BUILD)/app_km0 $(BUILD)/app_km4 $(BUILD)/app_km4_secure
