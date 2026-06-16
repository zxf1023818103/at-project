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
#   make TFM=ON          # in-tree TF-M secure world (SPE via ExternalProject; app_km4 = NSPE)
#   make STORAGE=ON      # TF-M PSA secure storage (ITS + PS) + NS storage self-test (implies TFM=ON)
#   make AWS=ON          # AWS IoT Core MQTT/TLS demo (coreMQTT + mbedTLS; cert/endpoint->LFS, key->TF-M PS; implies STORAGE=ON)
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
TFM        ?= OFF          # ON -> build + boot in-tree TF-M SPE (app_km4 becomes the NS side)
HWGCM      ?= $(TFM)       # default = TFM: route mbedTLS AES-GCM + ChaCha20-Poly1305 to the RTL8721D IPSEC HW engine (needs TFM=ON's SAU, so on iff TFM is on)
STORAGE    ?= OFF          # ON -> TF-M PSA secure storage (ITS + PS) + NS storage self-test; implies TFM=ON
AWS        ?= OFF          # ON -> AWS IoT Core MQTT/TLS demo (coreMQTT + mbedTLS); cert/endpoint->LFS, private key->TF-M PS
ifeq ($(AWS),ON)
# AWS=ON: the MQTT client private key lives in TF-M secure storage (implies STORAGE/TFM).
# NOTE: no trailing comment on the assignment — a trailing-space value breaks the STORAGE ifeq below.
STORAGE    := ON
# Force software AES-GCM for AWS. History: gcm_alt omitted the one-time CRYPTO_Init the HW KAT does,
# so the ROM rtl_crypto_aes_gcm_init returned "io driver parameters error" (now fixed in gcm_alt.c).
# A follow-on "HW-GCM hangs the AWS handshake" symptom turned out to be a MISATTRIBUTION — it was the
# mbedTLS VARIABLE_BUFFER_LENGTH realloc-during-handshake race (present in the software build too;
# fixed by disabling that macro), not the GCM op. So HW-GCM for a live AWS session is currently
# UNTESTED post-fix; keep it OFF for AWS until a clean re-test (software GCM is plenty for MQTT).
# `:=` overrides the lazy `HWGCM ?= $(TFM)` default below (which would expand to ON once TFM:=ON).
HWGCM      := OFF
endif
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
IMAGE      := $(REPO)/build/amebad/firmware.bin

all: image

# (Re)configure both cores with Ninja. CMake can't change a tree's generator in
# place, so wipe any non-Ninja tree first (lets you switch from the old Makefiles
# build). Re-running just updates the NIMBLE/HCI cache vars.
configure:
	@for c in km0 km4; do d=$(REPO)/build/$$c; \
	  [ -f $$d/build.ninja ] || rm -rf $$d; done
	cd $(REPO) && cmake -S . -B build/km0 -G $(GEN) \
	  -DAMEBAD_CHIP=amebad-km0 \
	  $$([ -f build/km0/CMakeCache.txt ] || echo -DCMAKE_TOOLCHAIN_FILE=cmake/amebad-km0.cmake) \
	  -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)
	cd $(REPO) && cmake -S . -B build/km4 -G $(GEN) \
	  -DAMEBAD_CHIP=amebad-km4 \
	  $$([ -f build/km4/CMakeCache.txt ] || echo -DCMAKE_TOOLCHAIN_FILE=cmake/amebad-km4.cmake) \
	  -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
	  -DAMEBAD_ENABLE_NIMBLE=$(NIMBLE) -DAMEBAD_ENABLE_HCI=$(HCI) \
	  -DAMEBAD_ENABLE_QUIC=$(QUIC) -DAMEBAD_ENABLE_PKCS11=$(PKCS11) \
	  -DAMEBAD_ENABLE_CRYPTOBENCH=$(BENCH) -DAMEBAD_ENABLE_MINIMAL=$(MINIMAL) \
	  -DAMEBAD_ENABLE_AWS_IOT=$(AWS) \
	  -DAMEBAD_MBEDTLS_HW_GCM=$(HWGCM) \
	  -DAMEBAD_TFM_BOOT=$(TFM) \
	  -DAMEBAD_TFM_STORAGE=$(STORAGE) \
	  -DAMEBAD_VFS_SELFTEST=$(VFSTEST) \
	  -DAMEBAD_CPP_SELFTEST=$(CPPTEST)

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
