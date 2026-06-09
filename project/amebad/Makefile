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
#   make TFM=ON          # in-tree TF-M secure world (SPE via ExternalProject; app_km4 = NSPE)
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
WOLFASM    ?= ON           # ON -> wolfCrypt Thumb2 symmetric/hash asm (faster; OFF = portable C)
TFM        ?= OFF          # ON -> build + boot in-tree TF-M SPE (app_km4 becomes the NS side)
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
	  -DAMEBAD_ENABLE_QUIC=$(QUIC) \
	  -DAMEBAD_WOLFSSL_ARMASM=$(WOLFASM) \
	  -DAMEBAD_TFM_BOOT=$(TFM)

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
