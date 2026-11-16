# Independent CMake/Ninja image projects, matching project/amebad.
REPO := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
BUILD ?= $(REPO)/build/amebagreen2
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
USB_BENCH ?= OFF
USB_ROLE ?= device
NAN ?= OFF
NAN_USB ?= OFF
ifeq ($(NAN_USB),ON)
override NAN := ON
endif
NAN_HEAP_CHECKS ?= OFF
NAN_NET_DIAG ?= OFF
SBOOT ?= OFF
# Enable signing/encryption only with this board's explicitly provisioned keys.
RSIP ?= OFF
HWGCM ?= OFF
ROMKAT ?= OFF
EFUSE_DEBUG ?= OFF
TLS_HEAP_TRACE ?= OFF
TFM ?= OFF
STORAGE ?= OFF
ifeq ($(STORAGE),ON)
override TFM := ON
endif
SECURITY_KEYS ?= $(REPO)/project/amebagreen2/keys/com15.json
ifeq ($(HCI)$(NIMBLE),ONON)
$(error HCI and NIMBLE both own the Bluetooth controller; select one)
endif
QUIC_SERVER ?= 192.168.4.250
PORT ?= COM15
BOOT_PORT ?= COM19
EXTRA_FLAGS += -DAT_ENABLE_TFM=$(TFM) -DAT_ENABLE_STORAGE=$(STORAGE)
EXTRA_FLAGS += -DAT_ENABLE_USB_BENCH=$(USB_BENCH) -DAT_USB_BENCH_ROLE=$(USB_ROLE)
EXTRA_FLAGS += -DAT_ENABLE_NAN=$(NAN) -DAT_NAN_HEAP_CHECKS=$(NAN_HEAP_CHECKS)
EXTRA_FLAGS += -DAT_NAN_USB_BRIDGE=$(NAN_USB)
EXTRA_FLAGS += -DAT_NAN_NET_DIAG=$(NAN_NET_DIAG)
EXTRA_FLAGS += -DAT_ENABLE_HWGCM=$(HWGCM) -DAT_ENABLE_ROM_KAT=$(ROMKAT) -DAT_EFUSE_DEBUG=$(EFUSE_DEBUG) -DAT_TLS_HEAP_TRACE=$(TLS_HEAP_TRACE) -DAT_ENABLE_AWS_OTA=$(AWSOTA) -DAT_ENABLE_MCUMGR=$(MCUMGR) -DAT_ENABLE_WPS=$(WPS) -DAT_ENABLE_EAP=$(EAP) -DAT_ENABLE_HCI=$(HCI)
.PHONY: all image bootloader_km4ns bootloader_km4tz app_km4ns app_km4tz app_km4tz_secure deps tfm_deps test flash flashloader cli_test clean distclean
all: image
bootloader_km4ns bootloader_km4tz app_km4ns app_km4tz:
	cmake -S $(REPO)/project/amebagreen2/$@ -B $(BUILD)/$@ -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel -DAT_ENABLE_NIMBLE=$(NIMBLE) -DAT_ENABLE_AWS=$(AWS) -DTLS_BACKEND=$(TLS_BACKEND) -DAT_ENABLE_CRT_BUNDLE=$(CRT_BUNDLE) -DAT_FW_VERSION=$(FW_VERSION) -DAT_APP_BUILD=$(APP_BUILD) -DAT_ENABLE_CARES=$(CARES) -DAT_ENABLE_IPERF=$(IPERF) -DAT_ENABLE_MINIMAL=$(MINIMAL) -DAT_CRYPTO_SELFTESTS=$(CRYPTO_SELFTESTS) -DAT_ENABLE_PKCS11=$(PKCS11) -DAT_ENABLE_BENCH=$(BENCH) -DAT_VFS_SELFTEST=$(VFSTEST) -DAT_CPP_SELFTEST=$(CPPTEST) -DAT_ENABLE_QUIC=$(QUIC) -DAT_QUIC_SERVER=$(QUIC_SERVER) $(EXTRA_FLAGS) -DCMAKE_TOOLCHAIN_FILE=$(REPO)/cmake/amebagreen2-$(if $(filter app_km4ns bootloader_km4ns,$@),km4ns,km4tz).cmake
	cmake --build $(BUILD)/$@ --target $@ -j $(JOBS)
image: bootloader_km4tz app_km4ns app_km4tz
	$(MAKE) -C $(REPO)/project lfs_image
	python3 $(REPO)/component/amebagreen2/image_tool/build_image.py merge --build $(BUILD) --lfs $(REPO)/project/lfs.bin --sboot $(SBOOT) --rsip $(RSIP) --keys $(SECURITY_KEYS) $(if $(filter ON,$(TFM)),--tfm $(BUILD)/app_km4tz/app_km4tz_secure/tfm_spe/bin/tfm_s.bin)
app_km4tz_secure:
	cmake -S $(REPO)/project/amebagreen2/app_km4tz_secure -B $(BUILD)/app_km4tz_secure -G Ninja -DAT_ENABLE_STORAGE=$(STORAGE) -DAT_EFUSE_DEBUG=$(EFUSE_DEBUG) -DCMAKE_TOOLCHAIN_FILE=$(REPO)/cmake/amebagreen2-km4tz.cmake
	cmake --build $(BUILD)/app_km4tz_secure --target app_km4tz_secure -j $(JOBS)
deps:
	python3 -m pip install --target $(REPO)/build/amebagreen2/python-deps -r $(REPO)/component/amebagreen2/image_tool/requirements.txt
tfm_deps:
	python3 -m venv $(REPO)/build/amebagreen2/tfm-python
	$(REPO)/build/amebagreen2/tfm-python/bin/pip install -r $(REPO)/component/trusted-firmware-m_config/trusted-firmware-m/tools/requirements.txt
test:
	python3 -m unittest discover -s $(REPO)/component/amebagreen2/image_tool -p 'test_*.py'
	python3 -m unittest discover -s $(REPO)/component/amebagreen2/ota_flash_port -p 'test_*.py'
	python3 -m unittest discover -s $(REPO)/component/amebagreen2/efuse_port -p 'test_*.py'
flashloader:
	python3 $(REPO)/component/amebagreen2/image_tool/prepare_flashloader.py --keys $(SECURITY_KEYS) --output $(BUILD)/security-tools
flash: image $(if $(filter ON,$(SBOOT)),flashloader)
	powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$$(wslpath -w $(REPO)/project/amebagreen2/flash.ps1)" -Port $(PORT) -BootPort $(BOOT_PORT) -Image "$$(wslpath -w $(BUILD)/firmware.bin)" -FirmwareOnly $(if $(filter ON,$(SBOOT)),-ToolDir "$$(wslpath -w $(BUILD)/security-tools)") $(if $(filter ON,$(RSIP)),-AllowEncrypted)
cli_test:
	powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$$(wslpath -w $(REPO)/project/amebagreen2/cli_test.ps1)" -Port $(PORT) -BootPort $(BOOT_PORT)
clean:
	@for image in bootloader_km4ns bootloader_km4tz app_km4ns app_km4tz app_km4tz_secure; do if [ -f $(BUILD)/$$image/build.ninja ]; then cmake --build $(BUILD)/$$image --target clean; fi; done
distclean:
	rm -rf $(BUILD)/bootloader_km4ns $(BUILD)/bootloader_km4tz $(BUILD)/app_km4ns $(BUILD)/app_km4tz $(BUILD)/app_km4tz_secure
