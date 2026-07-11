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
#   make AWS=ON          # lean AWS IoT telemetry client (coreMQTT; aws_telemetry.c, same as bl602's AWS=ON) +
#                        # `aws` provisioning + `ota_http`; cert/endpoint->LFS, key->TF-M PS; implies STORAGE=ON
#   make AWSOTA=ON       # full AWS IoT agent image (implies AWS=ON): coreMQTT-Agent + FreeRTOS OTA v3.3.0
#                        # (aws_iot_demo.c) — same split as bl602's AWS/AWSOTA
#   make TLS_BACKEND=<fallback|picotls|mbedtls>  # TLS-over-TCP backend(s) for AWS. Default picotls:
#                        # links BOTH picotls (TLS 1.3) and mbedTLS (TLS 1.2), and each connection picks
#                        # via NetworkCredentials_t.backendMode (AWS MQTT/OTA force picotls; ota_http uses
#                        # fallback = picotls then mbedTLS). picotls = TLS 1.3 only, ~112 KB smaller flash
#                        # but no per-connection mode / no 1.2 fallback. mbedtls = TLS 1.2/1.3 only.
#                        # e.g. make AWS=ON TLS_BACKEND=picotls
#   make clean           # ninja clean both cores
#   make distclean       # delete build/km0 build/km4 build/amebad
#
# Output: build/amebad/firmware.bin  (intermediate, no LittleFS). `make -C project
#         amebad` merges lfs.bin into the final build/amebad/Image_All.bin
#         (flash @ 0x08000000; see project/amebad/flash.ps1).
# Prereqs: `git submodule update --init --recursive` + ASDK toolchain at toolchain/.

.PHONY: all image bootloader_km0 bootloader_km0_2nd app_km0 bootloader_km4 app_km4 clean distclean

# mcumgr SMP DFU rides the NimBLE host (smp_bt transport) and writes via ota_flash, so MCUMGR=ON
# forces NIMBLE=ON regardless of the command line (NIMBLE is ?= so := overrides it).
ifeq ($(MCUMGR),ON)
override NIMBLE := ON
endif

REPO       := $(abspath $(CURDIR)/../..)
GEN        := Ninja
BUILD_TYPE := MinSizeRel
NIMBLE     ?= OFF          # ON -> NimBLE host
MCUMGR     ?= OFF          # ON -> mcumgr SMP-over-BLE (DFU via img_mgmt -> ota_flash + os_mgmt echo/reset); implies NIMBLE=ON
HCI        ?= OFF          # ON -> bare HCI/DTM
QUIC       ?= OFF          # ON -> ngtcp2 QUIC client self-test (pulls full wolfSSL TLS1.3)
PKCS11     ?= OFF          # ON -> corePKCS11 token self-test (upstream mbedTLS backend + LittleFS PAL)
BENCH      ?= OFF          # ON -> upstream Mbed TLS v3.6.4 crypto microbenchmark ([MCB] scores)
MINIMAL    ?= OFF          # ON -> strip diagnostics (iperf / KAT / HW-crypto bench); WiFi-connect only (pairs with TFM=ON)
ROMKAT     ?= OFF          # ON -> ROM mbedTLS 2.4.0 / HW-crypto self-tests (no upstream mbedTLS; drops fs_cli md5sum/sha*sum)
# IPERF: default resolved AFTER the AWS/AWSOTA block below (linked by default except AWS images,
# matching bl602 -- iperf's ~16 KB .bss is reclaimed for the AWS TLS handshake heap there).
CARES      ?= ON           # DEFAULT-ON on BOTH platforms (aligned with project/bouffalo/Makefile): resolve DNS via c-ares (select event loop) instead of the FreeRTOS+TCP resolver (AWS/SNTP/OTA)
WPS        ?= OFF          # ON -> `wps` console command (WPS PBC/PIN connect test; compiles wifi_wps_config.c, links lib_wps.a)
TFM        ?= OFF          # ON -> build + boot in-tree TF-M SPE (app_km4 becomes the NS side)
TLS_BACKEND ?= picotls     # AWS TLS-over-TCP backend. DEFAULT picotls (TLS 1.3) on BOTH platforms (aligned with project/bouffalo/Makefile): AWS IoT + S3 are TLS 1.3, picotls = ~112 KB smaller flash + ~24 KB less runtime heap than fallback. `fallback` (co-link picotls/TLS1.3 + mbedTLS/TLS1.2, per-connection NetworkCredentials_t.backendMode: force picotls for AWS MQTT/OTA, picotls->mbedTLS for ota_http to a non-1.3 server) and `mbedtls` (TLS 1.2/1.3) stay selectable. Switch wiring: component/network_transport/transport_tls.h.
ifeq ($(filter $(TLS_BACKEND),mbedtls picotls fallback),)
$(error TLS_BACKEND must be 'mbedtls', 'picotls', or 'fallback')
endif
# fallback (opt-in): try picotls (TLS 1.3) first, fall back to mbedTLS (TLS 1.2/1.3) for a non-1.3
# server. Links both backends (transport_fallback.c dispatches). e.g. make AWS=ON TLS_BACKEND=fallback
TLS_HEAP_TRACE ?= OFF      # ON = compile the gated picotls handshake heap trace (transport_picotls.c TLS_HEAP_MARK); logs free heap at connect/handshake/disconnect. Debug/measurement only.
HWGCM      ?= $(TFM)       # default = TFM: route AES-GCM + ChaCha20-Poly1305 to the RTL8721D IPSEC HW engine (needs TFM=ON's SAU, so on iff TFM is on). Both TLS backends use it: mbedTLS via the classic one-shot mbedtls_gcm; picotls via the encrypt-coalesce shim in transport_picotls.c that reduces its PSA multi-part AEAD to the engine's one-shot path.
STORAGE    ?= OFF          # ON -> TF-M PSA secure storage (ITS + PS) + NS storage self-test; implies TFM=ON
# AWS/AWSOTA split, aligned with project/bouffalo: AWS=ON alone = the lean telemetry-only client
# (aws_telemetry.c; publishes by default) + `aws` provisioning + `ota_http`. AWSOTA=ON = the full
# coreMQTT-Agent + FreeRTOS OTA v3.3.0 agent image (aws_iot_demo.c); implies AWS=ON.
AWSOTA     ?= OFF          # ON -> full AWS IoT agent + OTA image (implies AWS=ON)
AWS        ?= OFF          # ON -> lean AWS IoT telemetry client (coreMQTT); cert/endpoint->LFS, private key->TF-M PS
ifeq ($(AWSOTA),ON)
override AWS := ON
endif
ifeq ($(filter $(AWSOTA),ON OFF),)
$(error AWSOTA must be 'ON' or 'OFF')
endif
# iperf: manual `iperf server|tx|udp|udptx` console command (no auto-start), linked by default
# except in AWS images (heap) and MINIMAL images (WiFi-connect only) -- same polarity + UX as bl602.
ifeq ($(AWS),ON)
override IPERF := OFF
else ifeq ($(MINIMAL),ON)
override IPERF := OFF
else
IPERF      ?= ON
endif
# CRT_BUNDLE: verify the TLS server cert against the embedded compressed Mozilla CA root bundle
# (esp_crt_bundle, ~121 roots), falling back to the `ca/` folder (project/resource/ca -> LittleFS)
# for roots the bundle lacks (self-signed / niche CAs). ALWAYS ON for AWS images (not
# user-overridable); forced OFF for non-AWS images (no TLS path). Set below from $(AWS).
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
# CRT_BUNDLE only matters for the TLS path, which exists only in the AWS image. Default ON there
# (the embedded bundle is the primary trust source + the ca/ folder fallback); OFF everywhere else.
# ?= so it is user-overridable (e.g. make AWS=ON CRT_BUNDLE=OFF to pin a single provisioned CA),
# matching the bouffalo Makefile.
ifeq ($(AWS),ON)
CRT_BUNDLE ?= ON
else
CRT_BUNDLE ?= OFF
endif
# CRYPTO_SELFTESTS umbrella (same flag + form as bl602): bench + crt_bundle + pkcs11 as CONSOLE
# COMMANDS (no boot auto-run). PKCS11=ON / BENCH=ON keep the boot-auto-run diagnostic form.
CRYPTO_SELFTESTS ?= OFF
ifeq ($(filter $(CRYPTO_SELFTESTS),ON OFF),)
$(error CRYPTO_SELFTESTS must be 'ON' or 'OFF')
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
RSIP       ?= OFF          # DEFAULT-OFF: plain image for a board WITHOUT RSIP fused. This bench board's
                           #       eFuse is blank (verified 2026-07-08: the plain image boots + connects to
                           #       AWS; an RSIP-encrypted image reads as garbage and hangs silently at boot).
                           #       RSIP=ON EncTool-encrypts km0/km4 boot + image2 + IMG3 with AMEBAD_RSIP_KEY/
                           #       IV every build so the image is bootable on an RSIP-FUSED board (an
                           #       unencrypted image on a fused board reads as garbage).
RSIP_KEY   ?= f7954f24be5a0e1756e852edb1ff2d24   # THIS board's key: the build key string is the REVERSE
                           #       byte order of eFuse 0x190 (=242dffb1…95f7, AN0405). It IS the flash
                           #       encryption key (in-repo like doc/test_device_keys.csv); override per board.
RSIP_IV    ?= 90acd34ec70a53fa6eba8ca400000000   # NDA Realtek FAE IV (same for all boards).
IMAGE      := $(REPO)/build/amebad/firmware.bin

all: image

# Per-image standalone builds: each project/amebad/<app> is its own CMake project, configured into
# its own build/<app> tree with the per-core toolchain (which FORCE-sets AMEBAD_CHIP). The km0 images
# + bootloaders take only the common flags; only app_km4 reads the full feature set (and pulls in
# app_km4_secure, the TF-M SPE, as a sub-build).
TC0 := $(REPO)/cmake/amebad-km0.cmake
TC4 := $(REPO)/cmake/amebad-km4.cmake
_COMMON := -G $(GEN) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DAMEBAD_EFUSE_DEBUG=$(EFUSE_DEBUG) \
           -DAMEBAD_RSIP_ENABLE=$(RSIP) -DAMEBAD_RSIP_KEY=$(RSIP_KEY) -DAMEBAD_RSIP_IV=$(RSIP_IV)
_KM4 := -DAMEBAD_ENABLE_NIMBLE=$(NIMBLE) -DAT_ENABLE_MCUMGR=$(MCUMGR) -DAMEBAD_ENABLE_HCI=$(HCI) -DAT_ENABLE_QUIC=$(QUIC) \
        -DAMEBAD_ENABLE_PKCS11=$(PKCS11) -DAMEBAD_ENABLE_CRYPTOBENCH=$(BENCH) -DAMEBAD_ENABLE_MINIMAL=$(MINIMAL) \
        -DAMEBAD_ENABLE_ROM_KAT=$(ROMKAT) -DAMEBAD_ENABLE_IPERF=$(IPERF) -DAMEBAD_ENABLE_AWS_IOT=$(AWS) -DAMEBAD_ENABLE_AWS_OTA=$(AWSOTA) -DAMEBAD_ENABLE_CARES=$(CARES) \
        -DAT_ENABLE_CRT_BUNDLE=$(CRT_BUNDLE) -DAT_CRYPTO_SELFTESTS=$(CRYPTO_SELFTESTS) \
        -DAMEBAD_FW_VERSION=$(FW_VERSION) -DAMEBAD_APP_BUILD=$(APP_BUILD) -DTLS_BACKEND=$(TLS_BACKEND) \
        -DAT_TLS_HEAP_TRACE=$(TLS_HEAP_TRACE) \
        -DAMEBAD_MBEDTLS_HW_GCM=$(HWGCM) -DAMEBAD_TFM_BOOT=$(TFM) -DAMEBAD_TFM_STORAGE=$(STORAGE) \
        -DAT_VFS_SELFTEST=$(VFSTEST) -DAMEBAD_CPP_SELFTEST=$(CPPTEST) -DAMEBAD_SBOOT=$(SBOOT) \
        -DAMEBAD_ENABLE_WPS=$(WPS)

bootloader_km0:
	cd $(REPO) && cmake -S project/amebad/bootloader_km0 -B build/bootloader_km0 $(_COMMON) \
	  $$([ -f build/bootloader_km0/CMakeCache.txt ] || echo -DCMAKE_TOOLCHAIN_FILE=$(TC0))
	cmake --build $(REPO)/build/bootloader_km0 -t target_loader_km0

bootloader_km0_2nd:
	cd $(REPO) && cmake -S project/amebad/bootloader_km0_2nd -B build/bootloader_km0_2nd $(_COMMON) \
	  $$([ -f build/bootloader_km0_2nd/CMakeCache.txt ] || echo -DCMAKE_TOOLCHAIN_FILE=$(TC0))
	cmake --build $(REPO)/build/bootloader_km0_2nd -t target_loader_2nd

app_km0:
	cd $(REPO) && cmake -S project/amebad/app_km0 -B build/app_km0 $(_COMMON) \
	  $$([ -f build/app_km0/CMakeCache.txt ] || echo -DCMAKE_TOOLCHAIN_FILE=$(TC0))
	cmake --build $(REPO)/build/app_km0 -t target_img2_km0

bootloader_km4:
	cd $(REPO) && cmake -S project/amebad/bootloader_km4 -B build/bootloader_km4 $(_COMMON) $(_KM4) \
	  $$([ -f build/bootloader_km4/CMakeCache.txt ] || echo -DCMAKE_TOOLCHAIN_FILE=$(TC4))
	cmake --build $(REPO)/build/bootloader_km4 -t target_loader_km4

app_km4:
	cd $(REPO) && cmake -S project/amebad/app_km4 -B build/app_km4 $(_COMMON) $(_KM4) \
	  $$([ -f build/app_km4/CMakeCache.txt ] || echo -DCMAKE_TOOLCHAIN_FILE=$(TC4))
	cmake --build $(REPO)/build/app_km4 -t target_img2_km4

# Assemble the flashable image from the per-app build dirs. SBOOT keygen first (the script signs km4
# image2 with build/sboot/sboot_priv.pem); build_image_all.py reads SBOOT/RSIP/TFM/STORAGE from
# build/app_km4's cache and only assembles (--no-build; the app targets above did the building).
image: bootloader_km0 app_km0 bootloader_km4 app_km4
	@[ "$(SBOOT)" = "ON" ] && python3 $(REPO)/component/amebad/image_tool/sboot_keygen.py || true
	python3 $(REPO)/component/amebad/image_tool/build_image_all.py --no-build --build-root $(REPO)/build --out $(IMAGE)
	@echo "==> $(IMAGE)"

clean:
	@for d in bootloader_km0 bootloader_km0_2nd app_km0 bootloader_km4 app_km4; do \
	  [ -f $(REPO)/build/$$d/build.ninja ] && cmake --build $(REPO)/build/$$d -t clean || true; done

distclean:
	rm -rf $(REPO)/build/bootloader_km0 $(REPO)/build/bootloader_km0_2nd $(REPO)/build/app_km0 \
	       $(REPO)/build/bootloader_km4 $(REPO)/build/app_km4 $(REPO)/build/amebad
