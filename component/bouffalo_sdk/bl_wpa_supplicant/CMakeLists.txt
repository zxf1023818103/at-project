# bl_wpa_supplicant — Bouffalo's port of wpa_supplicant for the
# wl80211 / macsw stack. Extracted from
# bouffalo_sdk/components/wireless/bl_wpa_supplicant/.
#
# Direct rewrite of the upstream sdk_library_add_sources list to native
# CMake. Disabled (commented-out) sources are kept matching upstream so
# any future flip just needs uncommenting.
#
# Depends on mbedcrypto for ECC + bignum + AES helpers — the
# `crypto_mbedtls-*` sources in src/crypto/ go straight through
# mbedtls's API.

add_library(bl_wpa_supplicant STATIC
    port/os_bl.c
    src/ap/ap_config.c
    src/ap/wpa_auth_ie.c
    src/ap/wpa_auth_rsn_ccmp_only.c
    src/bl_supplicant/bl_hostap.c
    src/bl_supplicant/bl_wpa3.c
    src/bl_supplicant/bl_wpa_main.c
    src/bl_supplicant/bl_wpas_glue.c
    src/common/sae.c
    src/common/wpa_common.c
    src/crypto/aes-omac1.c
    src/crypto/aes-unwrap.c
    src/crypto/aes-wrap.c
    src/crypto/dh_group5.c
    src/crypto/dh_groups.c
    src/crypto/rc4.c
    src/crypto/sha1-pbkdf2-bl.c
    src/crypto/sha256-prf.c
    src/crypto/crypto_mbedtls-ec.c
    src/crypto/crypto_mbedtls-bignum.c
    src/crypto/crypto_mbedtls_misc.c
    src/rsn_supp/wpa.c
    src/rsn_supp/wpa_ie.c
    src/utils/common.c
    src/utils/wpa_debug.c
    src/utils/wpabuf.c
)

target_include_directories(bl_wpa_supplicant PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/bl_supplicant"
)
target_include_directories(bl_wpa_supplicant PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/port/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    # wl80211/include/supplicant.h is pulled in via
    # src/common/wpa_common.h and src/bl_supplicant/bl_wifi_driver.h —
    # header-only dep (no link needed; wl80211 links us, not vice versa)
    "${CMAKE_CURRENT_SOURCE_DIR}/../wl80211/include"
)

target_compile_definitions(bl_wpa_supplicant PUBLIC
    CONFIG_NEWLIB_NANO_FORMAT=0
    CONFIG_WPA_WAPI_PSK=0
    PSK_SUPPLICANT=1
    WPA3_SUPPORTED=1
    CONFIG_ECC
    USE_MBEDTLS_CRYPTO
)

target_compile_options(bl_wpa_supplicant PRIVATE
    -Wno-unused-function
    -Wno-unused-variable
)

# Crypto routines: USE_MBEDTLS_CRYPTO means we lean on upstream mbedtls
# for bignum / ECC / AES primitives. mbedcrypto is built by the mbedtls
# block in component/thirdparty/CMakeLists.txt.
target_link_libraries(bl_wpa_supplicant PUBLIC
    bflb_lhal
    bflb_freertos_port
    mbedcrypto
    # wl80211/include/wl80211.h pulls "queue/queue.h" + "tree/tree.h"
    bflb_queue
    bflb_tree
)
