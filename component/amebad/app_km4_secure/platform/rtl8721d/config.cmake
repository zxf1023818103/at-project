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

# Platform PSA-RoT service: the first real secure partition for this port. It is
# crypto-free (system reset / IOCTL / NV-counter) — so it exercises the full PSA
# round-trip (NS psa_call -> SG veneer -> SPM -> partition SFN -> reply) without
# pulling mbedcrypto. SFN model, stateless service (handle 6) => the NS side calls
# it with psa_call and no psa_connect. The HAL (tfm_platform_hal_system_reset /
# _ioctl) is our port's services/src/tfm_platform_system.c.
set(TFM_PARTITION_PLATFORM        ON          CACHE BOOL   "Enable the Platform PSA-RoT service")

# ---- PSA secure storage (make STORAGE=ON -> -DAMEBAD_TFM_STORAGE) ----
# Internal Trusted Storage (crypto-free) + Protected Storage (encrypted-at-rest,
# which pulls the Crypto partition / mbedcrypto into the SPE — secure-only; the NS
# side keeps wolfSSL). All four partitions are SFN-model (connection_based=false),
# so this stays on our SFN backend + isolation L1 (no IPC backend needed). Crypto
# entropy on this no-TRNG part uses mbedcrypto's NV seed (CRYPTO_NV_SEED), stored
# in ITS; the initial seed + HUK/ROTPK come from the default OTP + dummy
# provisioning. Sizes/configs follow TF-M's profile_medium (CCM AEAD = smaller).
if(AMEBAD_TFM_STORAGE)
    set(TFM_PARTITION_INTERNAL_TRUSTED_STORAGE ON CACHE BOOL "Enable ITS")
    set(TFM_PARTITION_CRYPTO          ON          CACHE BOOL   "Enable Crypto (mbedcrypto) — needed by PS encryption")
    set(TFM_PARTITION_PROTECTED_STORAGE ON        CACHE BOOL   "Enable Protected Storage")
    set(PS_ENCRYPTION                 ON          CACHE BOOL   "PS encrypts at rest")
    set(PS_CRYPTO_AEAD_ALG            PSA_ALG_CCM CACHE STRING "PS AEAD (CCM = smaller than GCM)")
    set(CRYPTO_NV_SEED                ON          CACHE BOOL   "mbedcrypto NV-seed entropy (no TRNG); seed stored in ITS")
    set(CRYPTO_HW_ACCELERATOR         OFF         CACHE BOOL   "Software crypto (no HW accel)")
    # Default platform HALs that back the storage/crypto chain (all flash-backed via
    # our real Driver_FLASH0 + the flash_layout OTP/NV areas):
    set(PLATFORM_DEFAULT_OTP          ON          CACHE BOOL   "Flash-backed OTP (entropy seed / HUK / ROTPK)")
    set(PLATFORM_DEFAULT_NV_COUNTERS  ON          CACHE BOOL   "Flash-backed NV counters (PS rollback)")
    # HUK from eFuse, not the dummy flash OTP: provide our OWN crypto_keys.c
    # (services/src/crypto_keys.c, added to platform_crypto_keys by the port
    # CMakeLists below) whose tfm_plat_get_huk reads the TrustZone-secure user
    # eFuse. So set the VAR OFF (skip the template crypto_keys.c) but keep the
    # DEFINE global (tfm_builtin_key_loader.h gates the HUK slot enum on it, and
    # the crypto partition must see the same enum).
    set(PLATFORM_DEFAULT_CRYPTO_KEYS  OFF         CACHE BOOL   "Use our eFuse-backed crypto_keys.c, not the template")
    add_definitions(-DPLATFORM_DEFAULT_CRYPTO_KEYS)
    set(PLATFORM_DEFAULT_PROVISIONING ON          CACHE BOOL   "Provision dummy OTP (seed/HUK) at boot")
    set(TFM_DUMMY_PROVISIONING        ON          CACHE BOOL   "Dev provisioning (NOT for production)")
    # Reuse profile_medium's crypto config TRIO (they must match each other):
    #  - PROJECT_CONFIG_HEADER_FILE: the TF-M config header that sets the crypto
    #    partition's CRYPTO_*_MODULE_ENABLED to match the mbedcrypto config (cipher
    #    + asym-encrypt OFF, AEAD/MAC/hash/key-derivation ON for PS). Without it the
    #    modules default ON and the build errors "missing CIPHER/asym algorithms".
    #  - the two mbedcrypto config headers (PSA_WANT_* + MBEDTLS_*), CCM AEAD.
    # We keep our own isolation L1 + SFN backend (the headers don't force those).
    set(PROJECT_CONFIG_HEADER_FILE
        "${CMAKE_SOURCE_DIR}/config/profile/config_profile_medium.h"
        CACHE FILEPATH "TF-M config header (crypto module gating to match mbedcrypto config)")
    # config_profile_medium.h is IPC-oriented and force-enables the doorbell API
    # (#ifndef -> 1), which config_spm.h rejects with our SFN backend. Override it
    # off via -D (wins over the header's #ifndef). Keep SFN + isolation L1.
    add_definitions(-DCONFIG_TFM_DOORBELL_API=0)
    # Raise the per-object PS cap 2 KB -> 4 KB so a single asset can hold an
    # AWS-IoT credential of any common key type: device cert (~1.2 KB PEM), Amazon
    # root CA (~1.2 KB), and the private key incl. RSA-4096 (~3.2 KB PEM) all fit
    # one object; also allows a small cert+key bundle. PS_MAX_ASSET_SIZE is a plain
    # C macro (#ifndef in config_profile_medium.h), not a CMake cache var, so it
    # must be forced via -D (same as DOORBELL above). The PS flash block (20 KB,
    # SPB=5 in flash_layout.h) must exceed PS_MAX_OBJECT_SIZE (~4.2 KB) — it does.
    add_definitions(-DPS_MAX_ASSET_SIZE=0x1000)
    set(TFM_MBEDCRYPTO_CONFIG_PATH
        "${CMAKE_SOURCE_DIR}/lib/ext/mbedcrypto/mbedcrypto_config/tfm_mbedcrypto_config_profile_medium.h"
        CACHE PATH "mbedcrypto config")
    set(TFM_MBEDCRYPTO_PSA_CRYPTO_CONFIG_PATH
        "${CMAKE_SOURCE_DIR}/lib/ext/mbedcrypto/mbedcrypto_config/crypto_config_profile_medium.h"
        CACHE PATH "PSA crypto config")
else()
    # No storage: keep the NV-counter path disabled (Platform service needs none).
    set(PLATFORM_DEFAULT_NV_COUNTERS  OFF         CACHE BOOL   "No default NV-counter backend")
    add_definitions(-DPLATFORM_NV_COUNTER_MODULE_DISABLED=1)
endif()
