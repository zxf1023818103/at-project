# component/amebad/mbedtls_rom — call the AmebaD A-cut mask-ROM mbedTLS 2.4.0
# crypto as PURE SOFTWARE (zero flash: the code is already in ROM).
#
# Flat layout (everything in this dir):
#   rom_mbedtls_crypto.h  single self-contained header: the ROM-resident crypto
#                         primitives (AES / ARC4 / MD5 / SHA-1/256/512 / MD-HMAC)
#                         with exact ROM ABI structs (no mbedtls config, no
#                         mbedtls/*.h tree -> no clash with the upstream v3.6.4
#                         headers the image also carries). _Static_asserts lock the
#                         struct sizes (e.g. mbedtls_aes_context == 344, the
#                         RTL_HW_CRYPTO layout the ROM was built with) so a layout
#                         drift is a build error, not silent corruption.
#   rom_ssl_ram_map.h     ROM ssl ram-map struct (used by the glue).
#   rom_mbedtls.{c,h}     rom_mbedtls_init() glue (forces use_hw_crypto_func=0,
#                         wires the ram-map calloc/free -> FreeRTOS heap).
#   mbedtls_rom.ld        the ~299 ROM symbol addresses (mbedtls_* + ssl ram map),
#                         PROVIDE'd; app_km4/CMakeLists.txt cats it into the KM4
#                         image2 combined linker script.
#
# A consumer that calls ROM mbedtls adds rom_mbedtls.c to its own sources and links
# amebad_mbedtls_rom for the header (rom_mbedtls.c needs the SDK basic_types.h on
# its include path, which the app_km4 image already has).

# Header INTERFACE for the SHARED upstream Mbed TLS headers (mbedtls/version.h is
# pulled by rtl8721d_ota.h via ameba_soc.h on BOTH cores). Defined before the chip
# gate so it exists KM0+KM4; linked by amebad_sdk_includes instead of a raw path.
add_library(amebad_mbedtls_headers INTERFACE)
target_include_directories(amebad_mbedtls_headers SYSTEM INTERFACE
    "${AT_ROOT}/component/mbedtls_config/mbedtls/include")

if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    return()
endif()

add_library(amebad_mbedtls_rom INTERFACE)
target_include_directories(amebad_mbedtls_rom SYSTEM INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}")   # rom_mbedtls_crypto.h + rom_ssl_ram_map.h + rom_mbedtls.h
