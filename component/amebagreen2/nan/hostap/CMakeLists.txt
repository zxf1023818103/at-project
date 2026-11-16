add_library(green2_pasn_raw STATIC
    utils/common.c
    utils/wpabuf.c
    utils/wpa_debug.c
    common/sae.c
    common/dragonfly.c
    common/wpa_common.c
    common/ieee802_11_common.c
    common/ptksa_cache.c
    rsn_supp/pmksa_cache.c
    rsn_supp/wpa_ie.c
    ap/comeback_token.c
    ap/pmksa_cache_auth.c
    crypto/sha256-prf.c
    crypto/sha256-kdf.c
    crypto/sha384-prf.c
    crypto/sha384-kdf.c
    crypto/sha512-prf.c
    crypto/sha512-kdf.c
    crypto/sha1-prf.c
    crypto/aes-siv.c
    crypto/aes-wrap.c
    crypto/aes-unwrap.c
    crypto/aes-ctr.c
    crypto/dh_groups.c
    pasn/pasn_common.c
    pasn/pasn_initiator.c
    pasn/pasn_responder.c
    ../pasn_freertos.c ../pasn_crypto.c ../nan_pasn.c)
target_include_directories(green2_pasn_raw PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/utils ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_definitions(green2_pasn_raw PRIVATE CONFIG_PASN CONFIG_SAE CONFIG_IEEE80211W CONFIG_ECC CONFIG_NO_RANDOM_POOL CONFIG_NO_RADIUS CONFIG_NO_VLAN)
target_compile_options(green2_pasn_raw PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/../pasn_endian.h -Wno-unused-parameter)
target_link_libraries(green2_pasn_raw PRIVATE FreeRTOS mbedcrypto)
set(pasn_library ${CMAKE_CURRENT_BINARY_DIR}/libgreen2_pasn.a)
add_custom_command(OUTPUT ${pasn_library}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../namespace_hostap.py
        ${CMAKE_NM} ${CMAKE_OBJCOPY} $<TARGET_FILE:green2_pasn_raw> ${pasn_library}
    DEPENDS green2_pasn_raw ${CMAKE_CURRENT_SOURCE_DIR}/../namespace_hostap.py VERBATIM)
add_custom_target(green2_pasn_namespaced DEPENDS ${pasn_library})
add_library(green2_pasn STATIC IMPORTED GLOBAL)
set_target_properties(green2_pasn PROPERTIES IMPORTED_LOCATION ${pasn_library})
add_dependencies(green2_pasn green2_pasn_namespaced)

if(GREEN2_NAN_NATIVE_TESTS)
    # The test initiator needs a real PMKSA cache for pairing verification.
    target_compile_definitions(green2_pasn_raw PRIVATE IEEE8021X_EAPOL)
else()
    target_compile_definitions(green2_pasn_raw PRIVATE CONFIG_NO_STDOUT_DEBUG)
endif()
