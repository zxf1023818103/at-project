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
#   mbedtls (default) = mbedTLS 1.2/1.3, IN 6144 -- the heap-tight default that fits bl602.
#   picotls           = TLS 1.3 only (picotls_core + picotls_mbedtls). Larger buffers; may not fit
#                       bl602's heap. `fallback` is not wired for bouffalo yet.
# Switch requires a clean configure (make distclean or rm build_<chip>): the CMake cache is sticky.
TLS_BACKEND ?= mbedtls
ifeq ($(filter $(TLS_BACKEND),mbedtls picotls),)
$(error TLS_BACKEND must be 'mbedtls' or 'picotls')
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
	    -DBOUFFALO_TLS_BACKEND=$(TLS_BACKEND) \
	    -DAT_ENABLE_CARES=$(CARES) \
	    -DAT_ENABLE_QUIC=$(QUIC) \
	    -DAT_ENABLE_AWS_OTA=$(AWSOTA)

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
