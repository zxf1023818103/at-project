# AmebaD (RTL8721D) driver-only image build — CMake + Ninja.
#
# Replaces the old GCC-RELEASE SDK-Makefile build. Drives the two-core CMake
# trees (build/km0, build/km4) + component/amebad/image_tool/build_image_all.py
# and produces build/amebad/firmware.bin (KM0/KM4 boot + image2 + TF-M, no data
# partition). The final 4 MB build/amebad/Image_All.bin — with the 1024 KB
# LittleFS merged in at 0x300000 — is assembled by the `amebad` target in
# ../Makefile (project/Makefile), which runs this build then dd's lfs.bin in.
#
#   make                 # plain driver-only image (WiFi / iperf)
#   make NIMBLE=ON       # NimBLE host image (LE/GATT + wolfSSL SM crypto)
#   make HCI=ON          # bare HCI / DTM test image (mutually exclusive with NIMBLE)
#   make QUIC=ON         # ngtcp2 QUIC client self-test (full wolfSSL TLS1.3 + QUIC)
#   make BENCH=ON        # upstream Mbed TLS v3.6.4 crypto microbenchmark ([MCB] scores)
#   make MINIMAL=ON      # strip diagnostics (iperf / LFS+ROM-mbedTLS KAT / HW-crypto bench): WiFi connect only
#   make ROMKAT=ON       # ROM mbedTLS 2.4.0 + HW-crypto boot self-tests (no upstream mbedTLS; no md5sum/sha*sum)
#   make TFM=ON          # in-tree TF-M secure world (SPE via ExternalProject; app_km4 = NSPE)
#   make STORAGE=ON      # TF-M PSA secure storage (ITS + PS) + NS storage self-test (implies TFM=ON)
#   make AWS=ON          # AWS IoT Core MQTT/TLS demo (coreMQTT + mbedTLS; cert/endpoint->LFS, key->TF-M PS; implies STORAGE=ON)
#   make TLS_BACKEND=<picotls|mbedtls>   # TLS-over-TCP backend for AWS (default picotls: TLS 1.3 only,
#                        # ~112 KB smaller flash + ~24 KB less heap, shares the QUIC TLS stack). Use mbedtls
#                        # for TLS 1.2 compatibility / broader servers (1.2+1.3, mature, native X.509).
#                        # e.g. make AWS=ON TLS_BACKEND=mbedtls
#   make clean           # ninja clean both cores
#   make distclean       # delete build/km0 build/km4 build/amebad
#
# Output: build/amebad/firmware.bin  (intermediate, no LittleFS). `make -C project
#         amebad` merges lfs.bin into the final build/amebad/Image_All.bin
#         (flash @ 0x08000000; see project/amebad/flash.ps1).
# Prereqs: `git submodule update --init --recursive` + ASDK toolchain at toolchain/.

.PHONY: all image configure clean distclean

REPO       := $(abspath $(CURDIR)/../..)
GEN        := Ninja
BUILD_TYPE := MinSizeRel
NIMBLE     ?= OFF          # ON -> NimBLE host
HCI        ?= OFF          # ON -> bare HCI/DTM
QUIC       ?= OFF          # ON -> ngtcp2 QUIC client self-test (pulls full wolfSSL TLS1.3)
PKCS11     ?= OFF          # ON -> corePKCS11 token self-test (upstream mbedTLS backend + LittleFS PAL)
BENCH      ?= OFF          # ON -> upstream Mbed TLS v3.6.4 crypto microbenchmark ([MCB] scores)
MINIMAL    ?= OFF          # ON -> strip diagnostics (iperf / KAT / HW-crypto bench); WiFi-connect only (pairs with TFM=ON)
ROMKAT     ?= OFF          # ON -> ROM mbedTLS 2.4.0 / HW-crypto self-tests (no upstream mbedTLS; drops fs_cli md5sum/sha*sum)
IPERF      ?= OFF          # ON -> `iperf` console command over upstream iperf3 + BSD-sockets shim (syscall_config)
TFM        ?= OFF          # ON -> build + boot in-tree TF-M SPE (app_km4 becomes the NS side)
TLS_BACKEND ?= picotls     # AWS TLS-over-TCP backend (default: picotls — TLS 1.3 only; ~112 KB smaller flash + ~24 KB less heap; shares the QUIC TLS stack). Set mbedtls for TLS 1.2 compatibility / broader server support (1.2+1.3, mature, native X.509). Switch wiring: component/network_transport/transport_tls.h.
ifeq ($(filter $(TLS_BACKEND),mbedtls picotls),)
$(error TLS_BACKEND must be 'mbedtls' or 'picotls')
endif
HWGCM      ?= $(TFM)       # default = TFM: route AES-GCM + ChaCha20-Poly1305 to the RTL8721D IPSEC HW engine (needs TFM=ON's SAU, so on iff TFM is on). Both TLS backends use it: mbedTLS via the classic one-shot mbedtls_gcm; picotls via the encrypt-coalesce shim in transport_picotls.c that reduces its PSA multi-part AEAD to the engine's one-shot path.
STORAGE    ?= OFF          # ON -> TF-M PSA secure storage (ITS + PS) + NS storage self-test; implies TFM=ON
AWS        ?= OFF          # ON -> AWS IoT Core MQTT/TLS demo (coreMQTT + mbedTLS); cert/endpoint->LFS, private key->TF-M PS
FW_VERSION ?= ota-A        # firmware version string stamped into the AWS image (printed + in telemetry); bump for an OTA target build, e.g. make AWS=ON FW_VERSION=ota-B
APP_BUILD  ?= 1            # appFirmwareVersion build number (0.9.N) for the OTA self-test; an OTA target must be > the running image, e.g. make AWS=ON FW_VERSION=ota-E APP_BUILD=2
# coreSNTP UTC time-sync is now default-on (every non-QUIC image) - no SNTP flag.
ifeq ($(AWS),ON)
# AWS=ON: the MQTT client private key lives in TF-M secure storage by default (implies STORAGE/TFM).
# `make AWS=ON TFM=OFF` builds AWS WITHOUT the secure world; the key then falls back to a plain
# LittleFS file (aws_provision.c / aws_cli.c, gated on AMEBAD_TFM_STORAGE). So only force STORAGE=ON
# when TFM wasn't explicitly turned off on the command line.
# NOTE: no trailing comment on the STORAGE := ON assignment — a trailing-space value breaks the ifeq.
ifeq ($(origin TFM),command line)
ifeq ($(TFM),ON)
STORAGE    := ON
endif
else
STORAGE    := ON
endif
endif
# HW-GCM (HWGCM follows TFM above, so ON for these TF-M images) is verified end-to-end for BOTH QUIC
# (picotls/PSA AEAD: ~7.1 Mbps DL / 2.9 UL, vs 1.7 / 1.2 with software GCM) and AWS IoT (mbedTLS
# TLS1.3 handshake + MQTT telemetry). gcm_alt.c brings the IPSEC engine up itself, once, via the NS
# rtl_cryptoEngine_init alias 0x1010ea49. The long-misdiagnosed "HW-GCM hangs the first op" was
# gcm_alt calling the SECURE CRYPTO_Init alias (0x10104a49) from NS, which dead-spins in ROM
# BOOT_ROM_ResetVsr (reset handshake). Pass HWGCM=OFF to force software GCM.
ifeq ($(STORAGE),ON)
TFM        := ON           # secure storage lives in the TF-M SPE
endif
# NS-direct HW crypto reaches the secure-only crypto peripheral only once a TrustZone
# image has configured the SAU (target_cfg). Without TF-M there is no SAU -> the first HW
# crypto op hangs on the secure-attributed peripheral. Fail the build rather than hang.
ifeq ($(HWGCM),ON)
ifneq ($(TFM),ON)
$(error HWGCM=ON requires TFM=ON (NS-direct HW crypto needs TF-M's SAU; a non-TF-M image hangs))
endif
endif
VFSTEST    ?= OFF          # ON -> POSIX-fd VFS self-test (fopen/open/socket over LFS+sockets)
CPPTEST    ?= OFF          # ON -> C++ std::filesystem/iostream self-test (cout/cin over LPUART, stderr LOGUART)
SBOOT      ?= ON           # DEFAULT-ON: Ed25519-sign image2 (boot-ROM secure-boot format) every build,
                           #       so an image is always bootable on a secure-boot-FUSED board (a non-signed
                           #       image is rejected -> IMG2 SBOOT FAIL dead-loop). Signs with the private key
                           #       in build/sboot/ (gitignored); for a fused board that key MUST match the
                           #       eFuse public key (back it up - the seed is in doc/test_device_keys.csv).
                           #       SBOOT=OFF for an unsigned image (only safe on a non-fused board).
EFUSE_DEBUG ?= OFF         # ON -> DEBUG eFuse provisioning aids (NEVER ship): `efuse_burn` console cmd,
                           #       boot-dump of all eFuse keys + MACs, and Boot_Log_En (verbose bootloader
                           #       log). Default OFF; the signing (SBOOT) is independent and stays on.
RSIP       ?= ON           # DEFAULT-ON for the RSIP-FUSED bench board: EncTool-encrypt km0/km4 boot +
                           #       image2 + IMG3 with AMEBAD_RSIP_KEY/IV every build, so the image is
                           #       bootable (an unencrypted image on a fused board reads as garbage).
                           #       RSIP=OFF for a plain image (only boots on a board WITHOUT RSIP fused).
RSIP_KEY   ?= f7954f24be5a0e1756e852edb1ff2d24   # THIS board's key: the build key string is the REVERSE
                           #       byte order of eFuse 0x190 (=242dffb1…95f7, AN0405). It IS the flash
                           #       encryption key (in-repo like doc/test_device_keys.csv); override per board.
RSIP_IV    ?= 90acd34ec70a53fa6eba8ca400000000   # NDA Realtek FAE IV (same for all boards).
IMAGE      := $(REPO)/build/amebad/firmware.bin

all: image

# (Re)configure both cores with Ninja. CMake can't change a tree's generator in
# place, so wipe any non-Ninja tree first (lets you switch from the old Makefiles
# build). Re-running just updates the NIMBLE/HCI cache vars.
configure:
	@for c in km0 km4; do d=$(REPO)/build/$$c; \
	  [ -f $$d/build.ninja ] || rm -rf $$d; done
	@[ "$(SBOOT)" = "ON" ] && python3 $(REPO)/component/amebad/image_tool/sboot_keygen.py || true
	cd $(REPO) && cmake -S . -B build/km0 -G $(GEN) \
	  -DAMEBAD_CHIP=amebad-km0 \
	  $$([ -f build/km0/CMakeCache.txt ] || echo -DCMAKE_TOOLCHAIN_FILE=cmake/amebad-km0.cmake) \
	  -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
	  -DAMEBAD_EFUSE_DEBUG=$(EFUSE_DEBUG) \
	  -DAMEBAD_RSIP_ENABLE=$(RSIP) \
	  -DAMEBAD_RSIP_KEY=$(RSIP_KEY) -DAMEBAD_RSIP_IV=$(RSIP_IV)
	cd $(REPO) && cmake -S . -B build/km4 -G $(GEN) \
	  -DAMEBAD_CHIP=amebad-km4 \
	  $$([ -f build/km4/CMakeCache.txt ] || echo -DCMAKE_TOOLCHAIN_FILE=cmake/amebad-km4.cmake) \
	  -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
	  -DAMEBAD_ENABLE_NIMBLE=$(NIMBLE) -DAMEBAD_ENABLE_HCI=$(HCI) \
	  -DAMEBAD_ENABLE_QUIC=$(QUIC) -DAMEBAD_ENABLE_PKCS11=$(PKCS11) \
	  -DAMEBAD_ENABLE_CRYPTOBENCH=$(BENCH) -DAMEBAD_ENABLE_MINIMAL=$(MINIMAL) \
	  -DAMEBAD_ENABLE_ROM_KAT=$(ROMKAT) -DAMEBAD_ENABLE_IPERF=$(IPERF) \
	  -DAMEBAD_ENABLE_AWS_IOT=$(AWS) \
	  -DAMEBAD_FW_VERSION=$(FW_VERSION) \
	  -DAMEBAD_APP_BUILD=$(APP_BUILD) \
	  -DAMEBAD_TLS_BACKEND=$(TLS_BACKEND) \
	  -DAMEBAD_MBEDTLS_HW_GCM=$(HWGCM) \
	  -DAMEBAD_TFM_BOOT=$(TFM) \
	  -DAMEBAD_TFM_STORAGE=$(STORAGE) \
	  -DAMEBAD_VFS_SELFTEST=$(VFSTEST) \
	  -DAMEBAD_CPP_SELFTEST=$(CPPTEST) \
	  -DAMEBAD_SBOOT=$(SBOOT) \
	  -DAMEBAD_EFUSE_DEBUG=$(EFUSE_DEBUG) \
	  -DAMEBAD_RSIP_ENABLE=$(RSIP) \
	  -DAMEBAD_RSIP_KEY=$(RSIP_KEY) -DAMEBAD_RSIP_IV=$(RSIP_IV)

# build_image_all.py builds both cores (cmake --build, generator-agnostic — reuses
# the Ninja trees configured above) and assembles the flashable image.
image: configure
	python3 $(REPO)/component/amebad/image_tool/build_image_all.py --out $(IMAGE)
	@echo "==> $(IMAGE)"

clean:
	@[ -f $(REPO)/build/km0/build.ninja ] && cmake --build $(REPO)/build/km0 -t clean || true
	@[ -f $(REPO)/build/km4/build.ninja ] && cmake --build $(REPO)/build/km4 -t clean || true

distclean:
	rm -rf $(REPO)/build/km0 $(REPO)/build/km4 $(REPO)/build/amebad
