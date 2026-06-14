# component/amebad/mbedtls_port — AmebaD KM4 port of the upstream Mbed TLS v3.6.4 build.
#
# The upstream mbedcrypto/mbedx509/mbedtls targets themselves are built by
# component/mbedtls_config (add_subdirectory'd from component/CMakeLists.txt's AmebaD branch,
# BEFORE the amebad subtree). This dir supplies the AmebaD-specific pieces:
#   - the user config (mbedtls_amebad_user_config.h) + threading_alt.h, carried by the
#     `amebad_mbedtls_cfg` INTERFACE and attached PUBLIC onto the upstream targets so their
#     sources compile with our deltas (no FS/net/time; threading + hardware entropy);
#   - the runtime glue (FreeRTOS threading-alt + entropy hardware-poll), `amebad_mbedtls_port`.
#
# Consumers link the `amebad_mbedtls` umbrella (crypto + x509 + glue). This is the modern
# software crypto backend for corePKCS11 today and the planned picotls/ngtcp2 QUIC stack
# (replacing wolfSSL). Coexists with the ROM Mbed TLS 2.4.0 path (mbedtls_rom) — separate
# targets, never linked into the same image.
if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    return()
endif()

# Config surface: the MBEDTLS_USER_CONFIG_FILE define + this dir on the include path (for the
# user config AND threading_alt.h, which mbedtls/threading.h pulls by bare name).
add_library(amebad_mbedtls_cfg INTERFACE)
target_compile_definitions(amebad_mbedtls_cfg INTERFACE
    MBEDTLS_USER_CONFIG_FILE="mbedtls_amebad_user_config.h")
target_include_directories(amebad_mbedtls_cfg SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")

# Opt-in: route AES-GCM to the RTL8721D IPSEC HW engine (MBEDTLS_GCM_ALT -> gcm_alt.{h,c},
# NS ROM rtl_crypto_aes_gcm_*) — ~115x the software GCM; the QUIC AES-GCM bottleneck. The
# AMEBAD_MBEDTLS_HW_GCM define rides amebad_mbedtls_cfg so it reaches BOTH the mbedcrypto
# gcm.c compile (drops the SW impl via MBEDTLS_GCM_ALT in the user config) AND gcm_alt.c.
option(AMEBAD_MBEDTLS_HW_GCM "Route mbedTLS AES-GCM to the RTL8721D IPSEC HW engine" OFF)
if(AMEBAD_MBEDTLS_HW_GCM)
    target_compile_definitions(amebad_mbedtls_cfg INTERFACE AMEBAD_MBEDTLS_HW_GCM)
endif()

# Attach the config to the upstream targets (defined earlier by component/mbedtls_config).
# PUBLIC so it reaches both the targets' own sources AND every consumer (corePKCS11, picotls).
# Usage requirements resolve at generate, so attaching here (after the targets exist) is fine.
foreach(_t mbedcrypto mbedx509 mbedtls p256m everest)
    if(TARGET ${_t})
        target_link_libraries(${_t} PUBLIC amebad_mbedtls_cfg)
        target_compile_options(${_t} PRIVATE -w)   # silence vendor warnings under -Werror
    endif()
endforeach()

# Runtime glue: FreeRTOS threading-alt callbacks (amebad_mbedtls_port_init) + entropy poll.
# + gcm_alt.c (the MBEDTLS_GCM_ALT HW backend) when AMEBAD_MBEDTLS_HW_GCM is on — it provides
# the mbedtls_gcm_* symbols that gcm.c then omits, so it MUST be in the linked image.
set(_amebad_mbedtls_port_srcs mbedtls_amebad_port.c)
if(AMEBAD_MBEDTLS_HW_GCM)
    list(APPEND _amebad_mbedtls_port_srcs gcm_alt.c)
endif()
add_library(amebad_mbedtls_port STATIC EXCLUDE_FROM_ALL ${_amebad_mbedtls_port_srcs})
target_link_libraries(amebad_mbedtls_port PUBLIC amebad_mbedtls_cfg mbedcrypto FreeRTOS freertos_config)

# Umbrella (伞形): consumers link `amebad_mbedtls` for crypto + x509 + the glue.
add_library(amebad_mbedtls INTERFACE)
target_link_libraries(amebad_mbedtls INTERFACE mbedcrypto mbedx509 amebad_mbedtls_port amebad_mbedtls_cfg)
