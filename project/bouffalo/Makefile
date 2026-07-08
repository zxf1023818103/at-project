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

# CRT_BUNDLE: verify the TLS server cert against the embedded compressed Mozilla CA root bundle
# (component/esp_crt_bundle_config, ~121 roots in ~56 KB .rodata) instead of a single pinned root.
# This is what makes the live-AWS handshake succeed: AWS over-sends its FULL chain (server -> Amazon
# RSA 2048 M04 -> Amazon Root CA 1 [cross-signed] -> Starfield G2), so a single-root trust store fails
# to chain (mbedTLS follows the sent chain to the untrusted Starfield top -> X509 NOT_TRUSTED). The
# bundle's verify callback matches each chain cert against the ~121 embedded roots by subject+public
# key, so the over-sent copy of a root we hold is accepted. Same component + macro AmebaD uses
# (AT_ENABLE_CRT_BUNDLE). Only meaningful for the AWS TLS path (AWSOTA=ON); default ON there.
# Switch requires a clean configure (make distclean or rm build_<chip>): the CMake cache is sticky.
ifeq ($(AWSOTA),ON)
CRT_BUNDLE ?= ON
else
CRT_BUNDLE ?= OFF
endif
ifeq ($(filter $(CRT_BUNDLE),ON OFF),)
$(error CRT_BUNDLE must be 'ON' or 'OFF')
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
	    -DAT_ENABLE_AWS_OTA=$(AWSOTA) \
	    -DAT_ENABLE_CRT_BUNDLE=$(CRT_BUNDLE) \
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
