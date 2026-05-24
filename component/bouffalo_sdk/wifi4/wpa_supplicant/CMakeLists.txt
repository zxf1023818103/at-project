# wifi4_wpa_supplicant — Bouffalo's stripped wpa_supplicant fork
# (the "bl_supplicant" subset: AP+CCMP only, with SAE/WPA3, WPS).
# Extracted from bouffalo_sdk/components/wireless/wifi4/wpa_supplicant/.
#
# Public surface: include/ (high-level supplicant API, includes
# include/bl_supplicant/ for the bl_*.h glue). Internals stay private
# under port/include and src/.

if(NOT "${BOUFFALO_CHIP}" STREQUAL "bl602")
    message(FATAL_ERROR "wifi4_wpa_supplicant: only BL602 is supported (BOUFFALO_CHIP=${BOUFFALO_CHIP})")
endif()

add_library(wifi4_wpa_supplicant STATIC
    port/os_bl.c
    src/ap/ap_config.c
    src/ap/wpa_auth_ie.c
    src/ap/wpa_auth_rsn_ccmp_only.c
    src/bl_supplicant/bl_hostap.c
    src/bl_supplicant/bl_wpa3.c
    src/bl_supplicant/bl_wpa_main.c
    src/bl_supplicant/bl_wpas_glue.c
    src/bl_supplicant/bl_wps.c
    src/common/sae.c
    src/common/wpa_common.c
    src/crypto/aes-cbc.c
    src/crypto/aes-internal-bl.c
    src/crypto/aes-omac1.c
    src/crypto/aes-unwrap.c
    src/crypto/aes-wrap.c
    src/crypto/crypto_internal-modexp.c
    src/crypto/dh_group5.c
    src/crypto/dh_groups.c
    src/crypto/md5-internal.c
    src/crypto/md5.c
    src/crypto/rc4.c
    src/crypto/sha1-internal.c
    src/crypto/sha1-pbkdf2-bl.c
    src/crypto/sha1.c
    src/crypto/sha256-internal.c
    src/crypto/sha256-prf.c
    src/crypto/sha256.c
    src/eap_peer/eap_common.c
    src/rsn_supp/pmksa_cache.c
    src/rsn_supp/wpa.c
    src/rsn_supp/wpa_ie.c
    src/utils/common.c
    src/utils/wpa_debug.c
    src/utils/wpabuf.c
    src/wps/wps.c
    src/wps/wps_attr_build.c
    src/wps/wps_attr_parse.c
    src/wps/wps_attr_process.c
    src/wps/wps_common.c
    src/wps/wps_dev_attr.c
    src/wps/wps_enrollee.c
    src/wps/wps_registrar.c
    src/wps/wps_validate.c
    test/test_crypto-bl.c
)

target_include_directories(wifi4_wpa_supplicant
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/bl_supplicant"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/port/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_definitions(wifi4_wpa_supplicant PUBLIC BL602)

# Supplicant code pulls CFG_*/RW_* defines + bl_phy_api.h from
# wifi4_firmware, the bl60x driver's structs from wifi4_manager,
# and FreeRTOS for os_bl.c.
target_link_libraries(wifi4_wpa_supplicant
    PUBLIC
        wifi4_firmware
        wifi4_manager
        freertos_kernel
    PRIVATE
        wifi4_internal_headers
)
