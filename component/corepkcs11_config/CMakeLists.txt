# corepkcs11_config — FreeRTOS corePKCS11 (submodule component/corepkcs11_config/corepkcs11,
# + the nested OASIS PKCS #11 headers at source/dependency/3rdparty/pkcs11). A software
# PKCS #11 token: the standard C_* API (core_pkcs11.c) over an mbedTLS crypto backend
# (source/portable/mbedtls/core_pkcs11_mbedtls.c) + the PKI DER/PEM helpers
# (core_pki_utils.c, mbedtls_utils.c).
#
# HAND WRAPPER (like littlefs_config / freertos-posix_config), NOT the upstream
# add_subdirectory: corePKCS11's own CMake target (core_pkcs) bakes in the POSIX PAL
# (file-backed object storage) and a test/FetchContent flow — neither fits a cross-compile.
# Instead we `include(pkcsFilePaths.cmake)` to reuse the upstream file list (PKCS_SOURCES,
# PKCS_INCLUDE_PUBLIC_DIRS) and build our own archive. PKCS_SOURCES deliberately does NOT
# contain a PAL (the PAL sources are separate vars), so the lib is PAL-agnostic.
#
# Consumer MUST provide (this lib does not, by design):
#   1. A real mbedTLS (>= 2.28 / 3.x) built with MBEDTLS_THREADING_C — corePKCS11 uses
#      mbedtls_threading_mutex_t. Targets `mbedcrypto`/`mbedx509` are linked PUBLIC below for
#      their headers + symbols; the consumer's mbedTLS config decides the API. (AmebaD's ROM
#      mbedTLS 2.4.0 is too old; Bouffalo's upstream mbedTLS 3.6.4 fits but its config is
#      directory-scoped inside bouffalo_sdk — see component/mbedtls_config.)
#   2. A PKCS #11 PAL: core_pkcs11_pal.c implementing PKCS11_PAL_* (object persistence) over
#      the platform's storage (e.g. LittleFS). The bundled POSIX/Windows PALs are NOT built in.
#
# Because of (1)+(2) this component is NOT add_subdirectory'd from a platform branch yet — it
# is a staged definition. Wire it (with a configured mbedTLS + a PAL) from the consuming app.
#
# Targets produced:
#   corepkcs11         STATIC — PKCS_SOURCES + mbedtls_utils.c (EXCLUDE_FROM_ALL).
#   corepkcs11_config  INTERFACE umbrella over it.
set(_src "${CMAKE_CURRENT_SOURCE_DIR}/corepkcs11")
include("${_src}/pkcsFilePaths.cmake")   # PKCS_SOURCES, PKCS_INCLUDE_PUBLIC_DIRS (no PAL)

add_library(corepkcs11 STATIC EXCLUDE_FROM_ALL
    ${PKCS_SOURCES}
    "${_src}/source/dependency/3rdparty/mbedtls_utils/mbedtls_utils.c"   # PEM<->DER helpers
)

# PUBLIC (SYSTEM, to keep the vendor headers out of -Werror): the OASIS pkcs11 API headers +
# corePKCS11's own include/ (from pkcsFilePaths.cmake), the mbedtls_utils header, and our
# core_pkcs11_config.h. core_pkcs11.c pulls core_pkcs11_config_defaults.h -> core_pkcs11_config.h.
target_include_directories(corepkcs11 SYSTEM PUBLIC
    ${PKCS_INCLUDE_PUBLIC_DIRS}
    "${_src}/source/dependency/3rdparty/mbedtls_utils"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

# Crypto backend (declares the dependency; the consumer supplies a config-matched mbedTLS).
# mbedcrypto: pk / sha256 / cmac / ctr_drbg / entropy / threading. mbedx509: x509_crt.
target_link_libraries(corepkcs11 PUBLIC mbedcrypto mbedx509)
target_compile_options(corepkcs11 PRIVATE -w)

# Umbrella INTERFACE (伞形): consumers link `corepkcs11_config` to get the PKCS #11 token.
add_library(corepkcs11_config INTERFACE)
target_link_libraries(corepkcs11_config INTERFACE corepkcs11)
