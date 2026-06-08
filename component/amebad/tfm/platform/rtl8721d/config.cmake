#-------------------------------------------------------------------------------
# RTL8721D (Ameba-D) KM4 — TF-M platform default config.
#
# Architecture decisions vs the Arm AN521 reference:
#  - BL2 OFF: Ameba-D's ROM secure-boot authenticates IMG1 (bootloader) via the
#    eFuse public key, and the KM4 bootloader loads the images. TF-M is built as
#    the TrustZone secure image (IMG3), loaded by the Ameba boot chain — NOT via
#    MCUboot/BL2. (See AN0410 Secure Boot / the SDK's BOOT_IMG3 NS_ENTRY flow.)
#  - TrustZone ON, single secure core (KM4); KM0 is a separate CPU, not an SPE
#    multi-core topology here.
#  - Start at isolation level 1 to bring the port up; raise to L2/L3 later once
#    PPC/MPU partitioning is in place in target_cfg.c.
#
# NOTE: this is the bring-up baseline for the from-scratch RTL8721D port; knobs
# will tighten as region_defs.h / target_cfg.c / drivers land.
#-------------------------------------------------------------------------------

# All TF-M code that includes tfm_hal_device_header.h gets OUR device header
# (SAU/core_cm33 config); avoids the generic CMSIS-pack cmsis.h shadowing ours.
add_definitions(-DCMSIS_device_header="rtl8721d_device.h")

# M33 core feature config — set GLOBALLY so core_cm33.h always sees them before
# it is first included (else it defaults __SAUREGION_PRESENT=0 and drops SAU).
add_definitions(
    -D__SAUREGION_PRESENT=1U
    -D__MPU_PRESENT=1U
    -D__FPU_PRESENT=1U
    -D__DSP_PRESENT=1U
    -D__NVIC_PRIO_BITS=3U
    -D__VTOR_PRESENT=1U
)

set(CONFIG_TFM_USE_TRUSTZONE      ON          CACHE BOOL   "Enable use of TrustZone")
set(TFM_MULTI_CORE_TOPOLOGY       OFF         CACHE BOOL   "KM4 single secure core")

# Boot: no MCUboot BL2 — Ameba-D ROM/bootloader own the secure-boot chain.
set(BL2                           OFF         CACHE BOOL   "Ameba-D ROM/bootloader handle boot; TF-M is IMG3")

# Bring-up baseline: isolation L1; default profile.
set(TFM_ISOLATION_LEVEL           1           CACHE STRING "Isolation level (raise to 2/3 after PPC/MPU split)")

# No PSRAM in the QUIC target board flow — keep IMG3 in SRAM.
set(CONFIG_IMG3_SRAM              ON          CACHE BOOL   "Place TF-M (IMG3) in SRAM, not PSRAM")

# Enable the Crypto secure service (pulls mbedcrypto + ITS for key storage) —
# validates the partition framework + a real PSA-RoT service builds for rtl8721d.
# Crypto partition intentionally OFF: TF-M's PSA Crypto service is backed by
# mbedcrypto (mbed-TLS), and this project must NOT introduce mbedtls (its crypto
# stack is wolfSSL). A TF-M crypto service for this port would need a wolfSSL-
# backed PSA crypto core — separate, large work. Keep the SPE to SPM + non-crypto
# partitions for now.
# set(TFM_PARTITION_CRYPTO ON ...)   # would pull mbedcrypto — do not enable
