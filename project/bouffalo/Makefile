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
# AWS TLS-over-TCP backend for bl602 (component/aws_iot_config picks the transport):
#   picotls  = TLS 1.3 only (picotls_core + picotls_mbedtls). DEFAULT for AWSOTA=ON: bl602's mbedTLS
#              TLS 1.2 client handshake to AWS IoT stalls at state=12 (client Finished not accepted, AWS
#              goes silent) -- a mbedTLS-TLS-1.2-path issue, NOT a HW-crypto miscompute: picotls drives
#              the SAME Sec_Eng HW crypto (secp256r1 PKA + HACC AES + HW SHA, incl. the 2048-bit RSA
#              verify) and connects. Verified end-to-end (MQTT + OTA). Caps the parsed server chain to
#              leaf+intermediate so the RSA-2048 verify fits bl602's ~50 KB heap.
#   mbedtls  = mbedTLS 1.2/1.3, IN 6144 -- the lean default for the non-AWS telemetry client. Does NOT
#              connect to AWS IoT on bl602 (state=12).
#   fallback = the picotls-first -> mbedTLS-1.2 dispatcher (transport_fallback.c): co-links both backends,
#              tries picotls (connects on AWS), falls back to mbedTLS 1.2 only for a non-1.3 server.
# Switch requires a clean configure (make distclean or rm build_<chip>): the CMake cache is sticky.
# DEFAULT picotls (TLS 1.3) on BOTH platforms (aligned with project/amebad/Makefile). `fallback`
# (co-link picotls+mbedTLS, per-connection NetworkCredentials_t.backendMode) and `mbedtls` stay
# selectable for a non-TLS-1.3 server, but AWS + S3 are TLS 1.3, and bl602's mbedTLS never connects.
TLS_BACKEND ?= picotls
ifeq ($(filter $(TLS_BACKEND),mbedtls picotls fallback),)
$(error TLS_BACKEND must be 'mbedtls', 'picotls', or 'fallback')
endif

# HW AES-GCM (BL602 Sec_Eng: chunked HW AES-CTR + software GHASH, component/bouffalo_sdk/mbedtls_port/
# gcm_alt.c). ON defines MBEDTLS_GCM_ALT so the picotls AEAD's AES bulk runs on the Sec_Eng engine in
# batched CTR passes instead of per-16-byte software-mbedtls_gcm calls (throughput, esp. OTA download).
# GHASH stays software (cheap) because the whole-record HW-GMAC DMA scratch would OOM bl602's tight
# picotls handshake heap. Default ON for the AWSOTA picotls path; OFF keeps the pure software GCM.
# Switch requires a clean configure (make distclean or rm build_<chip>): the CMake cache is sticky.
ifeq ($(AWSOTA),ON)
HWGCM ?= ON
else
HWGCM ?= OFF
endif
ifeq ($(filter $(HWGCM),ON OFF),)
$(error HWGCM must be 'ON' or 'OFF')
endif

# c-ares async DNS resolver (component/c-ares_config), same cross-platform component AmebaD uses under
# CARES=ON. OFF (default) = the FreeRTOS-Plus-TCP resolver (FreeRTOS_getaddrinfo). ON links cares_port
# and stamps AT_PROJECT_CARES onto the DNS call sites (network_transport_sockets + sntp_time), so the
# device resolves via c-ares (connected-UDP shim + select loop over component/syscall_config).
# Switch requires a clean configure (make distclean or rm build_<chip>): the CMake cache is sticky.
CARES ?= ON
ifeq ($(filter $(CARES),ON OFF),)
$(error CARES must be 'ON' or 'OFF')
endif

# QUIC client self-test (ngtcp2 + picotls/mbedTLS TLS 1.3, component/quic_selftest_config), the same
# cross-platform stack AmebaD uses under QUIC=ON. OFF (default). ON links ngtcp2 + quic_selftest and
# runs the handshake/perf self-test on network-up. On bl602 it builds in QUIC_SMOKE mode (tiny
# handshake+download, shrunk stack) -- best-effort, as the TLS 1.3 handshake peak may exceed the heap.
# No TF-M is involved (Bouffalo has no TrustZone; HW crypto is mbedtls_port NS-direct).
# Switch requires a clean configure (make distclean or rm build_<chip>): the CMake cache is sticky.
QUIC ?= OFF
ifeq ($(filter $(QUIC),ON OFF),)
$(error QUIC must be 'ON' or 'OFF')
endif

# Full AWS IoT OTA agent (bl602 only): the coreMQTT-Agent + FreeRTOS OTA v3.3.0 agent over the MQTT
# data plane (single TLS reused from telemetry), sharing AmebaD's ota_app/ota_pal + agent support
# (component/aws_ota_config) + the bl602 slot writer. OFF (default) = the lean telemetry-only `aws`
# client (aws_telemetry.c). ON pulls coreMQTT-Agent + ota/corejson/tinycbor and swaps in the shared
# aws_iot_demo.c orchestration -- a heavier image (may stress bl602's flash/heap; the feasibility gate).
# Switch requires a clean configure (make distclean or rm build_<chip>): the CMake cache is sticky.
AWSOTA ?= OFF
ifeq ($(filter $(AWSOTA),ON OFF),)
$(error AWSOTA must be 'ON' or 'OFF')
endif

# Lean AWS IoT telemetry client (component/aws_iot_config: aws_telemetry.c) + the `aws` provisioning
# CLI + the shared mbedTLS/picotls TLS transport + the `ota_http <url>` command (which rides that same
# transport). OFF (default) links NONE of that stack -> a leaner image with no AWS/TLS-OTA footprint
# (e.g. a pure NIMBLE/MCUMGR BLE-DFU image). ON links the telemetry client + ota_http. AWSOTA=ON implies
# AWS=ON (the coreMQTT-Agent OTA variant builds on the same TLS transport). Switching requires a clean
# configure (rm build_<chip> or delete CMakeCache.txt): the CMake cache is sticky.
AWS ?= OFF
ifeq ($(AWSOTA),ON)
override AWS := ON
endif
ifeq ($(filter $(AWS),ON OFF),)
$(error AWS must be 'ON' or 'OFF')
endif

# OTA firmware version-gate build number (appFirmwareVersion .build). The FreeRTOS OTA agent only
# commits an update whose version EXCEEDS the running image, so bump this above the running build for
# an OTA target: make CHIP=bl602 BOARD=bl602dk AWSOTA=ON APP_BUILD=2
APP_BUILD ?= 1

# Firmware version string stamped into the AWS image (printed + published in the telemetry `fw`
# field); bump for an OTA target build, e.g. make ... AWSOTA=ON FW_VERSION=ota-B. Same flag as amebad.
FW_VERSION ?= ota-A

# CRT_BUNDLE: verify the TLS server cert against the embedded compressed Mozilla CA root bundle
# (component/esp_crt_bundle_config, ~121 roots in ~56 KB .rodata) instead of a single pinned root.
# This is what makes the live-AWS handshake succeed: AWS over-sends its FULL chain (server -> Amazon
# RSA 2048 M04 -> Amazon Root CA 1 [cross-signed] -> Starfield G2), so a single-root trust store fails
# to chain (mbedTLS follows the sent chain to the untrusted Starfield top -> X509 NOT_TRUSTED). The
# bundle's verify callback matches each chain cert against the ~121 embedded roots by subject+public
# key, so the over-sent copy of a root we hold is accepted. Same component + macro AmebaD uses
# (AT_ENABLE_CRT_BUNDLE). Only meaningful for the AWS TLS path; default ON for ANY AWS image (same rule as amebad).
# Switch requires a clean configure (make distclean or rm build_<chip>): the CMake cache is sticky.
ifeq ($(AWS),ON)
CRT_BUNDLE ?= ON
else
CRT_BUNDLE ?= OFF
endif
ifeq ($(filter $(CRT_BUNDLE),ON OFF),)
$(error CRT_BUNDLE must be 'ON' or 'OFF')
endif

# PSRAM: register the on-die PSRAM as heap (mm MM_HEAP_PSRAM_0; pvPortMalloc uses MM_FLAG_HEAP_ANY so
# it falls back there when OCRAM/WRAM are full). DEFAULT OFF: a plain BL616 die has no PSRAM and
# board_psram_x8_init() hangs at detection. Set PSRAM=ON only on a BL618 (PSRAM) die - it gives ota_http
# room for its 2nd (S3) TLS alongside the live AWS MQTT TLS without OOM.
PSRAM ?= OFF
ifeq ($(filter $(PSRAM),ON OFF),)
$(error PSRAM must be 'ON' or 'OFF')
endif

# LP_FW: concatenate the low-power firmware blob into the FW image (the SDK's
# CONFIG_POST_BUILDS_CONCAT_WITH_LP_FW post-build). Needed only for a runtime WiFi power-save build:
# it patches the lp_fw SHA + appends <chip>_lp_fw.bin onto the 'LPFW' placeholder the linked image
# reserves at its tail. DEFAULT OFF (the driver links the low-power subsystem but the project doesn't
# run power-save, so the blob isn't required). On this SDK bl616's placeholder is gated off, so LP_FW=ON
# fails loudly there until the placeholder is enabled; bl616cl carries it.
LP_FW ?= OFF
ifeq ($(filter $(LP_FW),ON OFF),)
$(error LP_FW must be 'ON' or 'OFF')
endif

# Apache NimBLE host on the on-chip BLE controller (component/bouffalo_sdk/nimble_port). OFF (default,
# same as AmebaD's AMEBAD_ENABLE_NIMBLE): the base image is WiFi + console + sntp. ON builds a BLE image
# (advertises `bflb-nimble`, GAP/GATT sample service). proj.conf honors this -D (else defaults OFF).
#
# mcumgr SMP-over-BLE DFU (img_mgmt -> ota_flash slot writer + os/fs groups): rides the NimBLE host +
# smp_bt transport, so MCUMGR=ON forces NIMBLE=ON (NIMBLE is ?= so := overrides it), exactly as
# project/amebad/Makefile does. Standalone feasibility (no AWSOTA needed).
# Switch requires a clean configure (make distclean or rm build_<chip>): the CMake cache is sticky.
ifeq ($(MCUMGR),ON)
override NIMBLE := ON
endif
NIMBLE ?= OFF
ifeq ($(filter $(NIMBLE),ON OFF),)
$(error NIMBLE must be 'ON' or 'OFF')
endif
MCUMGR ?= OFF
ifeq ($(filter $(MCUMGR),ON OFF),)
$(error MCUMGR must be 'ON' or 'OFF')
endif

# Mbed TLS crypto self-test CLIs (bench + crt_bundle + pkcs11), opt-in: all three together overflow
# bl602's xip_memory (~100 KB over), so they're a diagnostic image. Threaded to AT_CRYPTO_SELFTESTS
# (component/bouffalo_sdk); amebad exposes the same umbrella flag.
CRYPTO_SELFTESTS ?= OFF
ifeq ($(filter $(CRYPTO_SELFTESTS),ON OFF),)
$(error CRYPTO_SELFTESTS must be 'ON' or 'OFF')
endif

# MINIMAL=ON: WiFi-connect-only image — strip the boot diagnostics (LFS self-test) + iperf,
# same flag + semantics as amebad's MINIMAL.
MINIMAL ?= OFF
ifeq ($(filter $(MINIMAL),ON OFF),)
$(error MINIMAL must be 'ON' or 'OFF')
endif

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
	    -DBOUFFALO_BOARD=$(BOARD) \
	    -DTLS_BACKEND=$(TLS_BACKEND) \
	    -DAT_ENABLE_CARES=$(CARES) \
	    -DAT_ENABLE_QUIC=$(QUIC) \
	    -DAT_ENABLE_AWS=$(AWS) \
	    -DAT_ENABLE_AWS_OTA=$(AWSOTA) \
	    -DAWS_APP_BUILD=$(APP_BUILD) \
	    -DAWS_FW_VERSION=$(FW_VERSION) \
	    -DAT_ENABLE_CRT_BUNDLE=$(CRT_BUNDLE) \
	    -DAT_ENABLE_NIMBLE=$(NIMBLE) \
	    -DAT_ENABLE_MCUMGR=$(MCUMGR) \
	    -DAT_CRYPTO_SELFTESTS=$(CRYPTO_SELFTESTS) \
	    -DAT_ENABLE_MINIMAL=$(MINIMAL) \
	    -DAT_ENABLE_PSRAM=$(PSRAM) \
	    -DAT_ENABLE_LP_FW=$(LP_FW) \
	    -DBOUFFALO_MBEDTLS_HW_GCM=$(HWGCM)

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
