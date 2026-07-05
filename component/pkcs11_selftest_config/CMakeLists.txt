# component/pkcs11_selftest_config — cross-platform corePKCS11 token self-test (pkcs11_selftest_run()
# -> [PK11] keygen/sign/verify) + the `pkcs11` console command + the upstream POSIX PAL that gives
# the token object persistence on LittleFS. Pure corePKCS11 + mbedcrypto PSA; no platform code.
#
# The PAL (core_pkcs11_pal.c + core_pkcs11_pal_utils.c) is the upstream fopen/remove implementation
# from the corepkcs11 submodule, reused verbatim over the platform's newlib VFS + LittleFS (same on
# AmebaD and bl602). corepkcs11 itself is PAL-agnostic (component/corepkcs11_config), so the PAL is
# supplied here, by the consumer, as designed.
set(_pal "${CMAKE_CURRENT_LIST_DIR}/../corepkcs11_config/corepkcs11/source/portable/os")

add_library(pkcs11_selftest STATIC EXCLUDE_FROM_ALL
    pkcs11_selftest.c
    pkcs11_cli.c
    "${_pal}/core_pkcs11_pal_utils.c"      # label <-> filename
    "${_pal}/posix/core_pkcs11_pal.c"      # fopen-based PAL over the newlib VFS / LittleFS
)
# Silence the vendor PAL's warnings (would trip -Werror in the consuming image).
set_source_files_properties(
    "${_pal}/core_pkcs11_pal_utils.c" "${_pal}/posix/core_pkcs11_pal.c"
    PROPERTIES COMPILE_OPTIONS "-w")

target_include_directories(pkcs11_selftest PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")   # pkcs11_selftest.h
target_include_directories(pkcs11_selftest PRIVATE "${_pal}")                      # core_pkcs11_pal_utils.h

# corepkcs11: the token + core_pkcs11_config.h/core_pkcs11.h. mbedtls_config: the platform's mbedTLS
# config context (PSA + threading-alt + mbedcrypto/mbedx509) as transitive usage requirements, so the
# selftest + PAL compile identically to the library on BOTH platforms. FreeRTOS + CLI + rxi log.
target_link_libraries(pkcs11_selftest PUBLIC
    corepkcs11
    mbedtls_config
    FreeRTOS
    freertos_cli
    rxi_log
)
