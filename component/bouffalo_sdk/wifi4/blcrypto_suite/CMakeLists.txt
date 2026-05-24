# wifi4_blcrypto_suite — Bouffalo's crypto glue for BL602: thin wrappers
# around the on-chip HACC accelerator (AES, SHA, ECC/secp256r1, bignum)
# behind a small mbedTLS-style API the supplicant uses. Extracted from
# bouffalo_sdk/components/wireless/wifi4/blcrypto_suite/.
#
# inc/ is the PUBLIC surface (blcrypto_suite_*.h consumed by wpa_supplicant);
# priv_inc/ stays PRIVATE (internal HACC register / glue headers).

if(NOT "${BOUFFALO_CHIP}" STREQUAL "bl602")
    message(FATAL_ERROR "wifi4_blcrypto_suite: only BL602 is supported (BOUFFALO_CHIP=${BOUFFALO_CHIP})")
endif()

add_library(wifi4_blcrypto_suite STATIC
    src/blcrypto_suite_bignum.c
    src/blcrypto_suite_ecp.c
    src/blcrypto_suite_ecp_curves.c
    src/blcrypto_suite_platform_util.c
    src/blcrypto_suite_porting.c
    src/blcrypto_suite_hacc.c
    src/blcrypto_suite_aes.c
    src/blcrypto_suite_hacc_glue.c
    src/blcrypto_suite_hacc_secp256r1_mul.c
    src/blcrypto_suite_supplicant_api.c
    src/blcrypto_suite_export_fw.c
)

target_include_directories(wifi4_blcrypto_suite
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/inc"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/priv_inc"
)

target_compile_definitions(wifi4_blcrypto_suite PUBLIC CFG_CHIP_BL602)

# HACC sits in the SoC peripheral block — register headers come from bl602_std,
# bus / IRQ glue from bflb_lhal, both pulled transitively through lhal.
# blcrypto_sec_pka.h lives in the adapter sublib (PKA-on-bouffalo wrapper);
# porting.c pulls FreeRTOS for mutex / heap.
target_link_libraries(wifi4_blcrypto_suite
    PUBLIC
        bflb_lhal
        wifi4_bflb_adapter
        freertos_kernel
    PRIVATE
        wifi4_internal_headers
)
