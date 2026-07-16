# Shared feature-flag contract for project/bouffalo and project/amebad.
#
# These defaults, implies, and validations used to be duplicated verbatim in
# both Makefiles (each side's comments said "aligned with <the other>"); this
# file is the single source of truth so one-sided edits can't drift. Only
# platform-neutral control flow lives here — defaults whose value encodes a
# platform-specific measured result (e.g. HWGCM, which means a different HW
# engine on each platform) stay in the per-platform Makefile, next to the
# comment explaining why.
#
# Include AFTER the platform Makefile's error-free preamble and BEFORE any
# logic that reads AWS/AWSOTA/NIMBLE (the implies below must run first).

# mcumgr SMP-over-BLE DFU rides the NimBLE host (smp_bt transport) and writes
# via ota_flash, so MCUMGR=ON forces NIMBLE=ON regardless of the command line
# (NIMBLE is ?= so := overrides it).
ifeq ($(MCUMGR),ON)
override NIMBLE := ON
endif
NIMBLE ?= OFF
MCUMGR ?= OFF

# AWS/AWSOTA split: AWS=ON alone = the lean telemetry-only client
# (aws_telemetry.c) + `aws` provisioning + `ota_http`. AWSOTA=ON = the full
# coreMQTT-Agent + FreeRTOS OTA v3.3.0 agent image (aws_iot_demo.c); it builds
# on the same TLS transport, so it implies AWS=ON.
AWSOTA ?= OFF
ifeq ($(AWSOTA),ON)
override AWS := ON
endif
AWS ?= OFF

# AWS TLS-over-TCP backend (component/network_transport picks the transport):
#   picotls  = TLS 1.3 only — DEFAULT on both platforms (AWS IoT + S3 are 1.3,
#              smallest flash/heap; the only backend that connects on bl602).
#   fallback = co-link picotls + mbedTLS, per-connection dispatch.
#   mbedtls  = mbedTLS TLS 1.2/1.3 only.
# Per-platform rationale/measurements live in each Makefile's comment block.
# Switch requires a clean configure (the CMake cache is sticky).
TLS_BACKEND ?= picotls
ifeq ($(filter $(TLS_BACKEND),mbedtls picotls fallback),)
$(error TLS_BACKEND must be 'mbedtls', 'picotls', or 'fallback')
endif

# c-ares async DNS resolver (component/c-ares_config): DEFAULT-ON on both
# platforms — resolve via c-ares (connected-UDP shim + select loop) instead of
# the FreeRTOS+TCP resolver (AWS/SNTP/OTA call sites).
CARES ?= ON

# CRT_BUNDLE: verify the TLS server cert against the embedded compressed
# Mozilla CA root bundle (esp_crt_bundle, ~121 roots). Only meaningful for the
# TLS path, which exists only in AWS images: default ON there, OFF elsewhere.
# ?= so a user can pin a single provisioned CA with `make AWS=ON CRT_BUNDLE=OFF`.
ifeq ($(AWS),ON)
CRT_BUNDLE ?= ON
else
CRT_BUNDLE ?= OFF
endif

# CRYPTO_SELFTESTS umbrella: bench + crt_bundle + pkcs11 self-test CLIs.
CRYPTO_SELFTESTS ?= OFF

# MINIMAL=ON: WiFi-connect-only image — strip the boot diagnostics + iperf.
MINIMAL ?= OFF

# Firmware version string + appFirmwareVersion build number stamped into the
# AWS image (printed + published in telemetry). The FreeRTOS OTA agent only
# commits an update whose version EXCEEDS the running image, so bump both for
# an OTA target build: make ... AWSOTA=ON FW_VERSION=ota-B APP_BUILD=2
FW_VERSION ?= ota-A
APP_BUILD  ?= 1

# ON/OFF validation for every shared flag.
ifeq ($(filter $(NIMBLE),ON OFF),)
$(error NIMBLE must be 'ON' or 'OFF')
endif
ifeq ($(filter $(MCUMGR),ON OFF),)
$(error MCUMGR must be 'ON' or 'OFF')
endif
ifeq ($(filter $(AWSOTA),ON OFF),)
$(error AWSOTA must be 'ON' or 'OFF')
endif
ifeq ($(filter $(AWS),ON OFF),)
$(error AWS must be 'ON' or 'OFF')
endif
ifeq ($(filter $(CARES),ON OFF),)
$(error CARES must be 'ON' or 'OFF')
endif
ifeq ($(filter $(CRT_BUNDLE),ON OFF),)
$(error CRT_BUNDLE must be 'ON' or 'OFF')
endif
ifeq ($(filter $(CRYPTO_SELFTESTS),ON OFF),)
$(error CRYPTO_SELFTESTS must be 'ON' or 'OFF')
endif
ifeq ($(filter $(MINIMAL),ON OFF),)
$(error MINIMAL must be 'ON' or 'OFF')
endif
